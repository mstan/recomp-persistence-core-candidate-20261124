#include "recomp_persistence/native_writer.h"
#include "recomp_persistence/atomic_file.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
static std::atomic<int> failures{0};
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);++failures;}}while(0)
static std::mutex gate_mutex;
static std::condition_variable gate;
static bool entered=false,released=false;
static std::thread::id producer;
static unsigned complete_count=0;
static int write_gated(const char* path,const void* data,size_t bytes) {
    {std::unique_lock<std::mutex> lock(gate_mutex);entered=true;gate.notify_all();gate.wait(lock,[]{return released;});}
    return recomp_atomic_file_store(path,data,bytes);
}
static int write_fail(const char*,const void*,size_t){return 0;}
static void completed(const char*,const void*,size_t,void*) {
    CHECK(std::this_thread::get_id()==producer);++complete_count;
}
static std::vector<unsigned char> read(const char* path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};
}
int main() {
    producer=std::this_thread::get_id();
    const unsigned char initial[]={1,2,3,4};unsigned char latest[]={5,6,7,8};
    CHECK(recomp_atomic_file_store("native.sav",initial,4));
    auto* writer=recomp_persist_create("native.sav",initial,4,write_gated);CHECK(writer);
    recomp_persist_on_completed(writer,completed,nullptr);
    CHECK(recomp_persist_submit(writer,initial)==1);CHECK(complete_count==0);
    CHECK(recomp_persist_submit(writer,latest)==1);
    {std::unique_lock<std::mutex> lock(gate_mutex);gate.wait(lock,[]{return entered;});}
    latest[0]=99;CHECK(recomp_persist_submit(writer,latest)==0);
    CHECK(read("native.sav")==std::vector<unsigned char>(initial,initial+4));
    {std::lock_guard<std::mutex> lock(gate_mutex);released=true;}gate.notify_all();
    CHECK(recomp_persist_wait(writer));CHECK(complete_count==1);
    CHECK(read("native.sav")==std::vector<unsigned char>({5,6,7,8}));
    CHECK(recomp_persist_flush(writer,latest));CHECK(complete_count==2);
    CHECK(recomp_persist_destroy(writer));CHECK(read("native.sav")[0]==99);
    auto* failing=recomp_persist_create("native.sav",initial,4,write_fail);
    const int accepted=recomp_persist_submit(failing,latest);CHECK(accepted==1 || accepted==-1);
    CHECK(!recomp_persist_wait(failing));CHECK(!recomp_persist_poll(failing));CHECK(!recomp_persist_destroy(failing));
    CHECK(read("native.sav")[0]==99);
#ifdef _WIN32
    // Let the temporary write succeed, then deny replacement of an open target.
    HANDLE locked=CreateFileW(L"native.sav",GENERIC_READ,FILE_SHARE_READ,nullptr,
                              OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    CHECK(locked!=INVALID_HANDLE_VALUE);
    if(locked!=INVALID_HANDLE_VALUE) {
        CHECK(!recomp_atomic_file_store("native.sav",initial,4));
        CHECK(read("native.sav")[0]==99);
        CloseHandle(locked);
    }
    CHECK(recomp_atomic_file_store("shareable.sav",initial,4));
    HANDLE shareable=CreateFileW(L"shareable.sav",GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,nullptr);
    CHECK(shareable!=INVALID_HANDLE_VALUE);
    if(shareable!=INVALID_HANDLE_VALUE){
        CHECK(recomp_atomic_file_store("shareable.sav",latest,4));
        CHECK(read("shareable.sav")==std::vector<unsigned char>(latest,latest+4));
        unsigned char retired[4]={};DWORD received=0;
        CHECK(ReadFile(shareable,retired,4,&received,nullptr)&&received==4);
        CHECK(!std::memcmp(retired,initial,4));
        CloseHandle(shareable);
    }
#endif
    std::filesystem::create_directory("destination-directory");
    std::ofstream("destination-directory/keep.txt")<<"preserved";
    CHECK(!recomp_atomic_file_store("destination-directory",initial,4));
    CHECK(std::filesystem::exists("destination-directory/keep.txt"));
    // Exclusive temporary names permit independent same-target writers.
    std::thread a([&]{CHECK(recomp_atomic_file_store("concurrent.sav",initial,4));});
    std::thread b([&]{CHECK(recomp_atomic_file_store("concurrent.sav",latest,4));});a.join();b.join();
    const auto concurrent=read("concurrent.sav");CHECK(concurrent.size()==4);
    CHECK(concurrent==std::vector<unsigned char>(initial,initial+4) || concurrent==std::vector<unsigned char>(latest,latest+4));
    // An accepted prefix is never published when a later write exceeds its
    // budget, or when its serializer aborts. Failed writes are sticky.
    auto* stream=recomp_atomic_file_begin("native.sav",4);CHECK(stream);
    CHECK(recomp_atomic_file_write(stream,initial,2));
    CHECK(recomp_atomic_file_flush(stream));
    CHECK(!recomp_atomic_file_write(stream,latest,3));
    CHECK(!recomp_atomic_file_write(stream,initial,1));
    CHECK(!recomp_atomic_file_commit(stream));CHECK(read("native.sav")[0]==99);
    stream=recomp_atomic_file_begin("native.sav",4);CHECK(stream);
    CHECK(recomp_atomic_file_write(stream,initial,2));
    recomp_atomic_file_abort(stream);CHECK(read("native.sav")[0]==99);
    stream=recomp_atomic_file_begin("native.sav",4);CHECK(stream);
    CHECK(!recomp_atomic_file_write(stream,nullptr,1));
    CHECK(!recomp_atomic_file_commit(stream));CHECK(read("native.sav")[0]==99);
    CHECK(!recomp_atomic_file_begin("missing/target",4));
    stream=recomp_atomic_file_begin("empty.sav",0);CHECK(stream);
    CHECK(recomp_atomic_file_write(stream,nullptr,0));
    CHECK(recomp_atomic_file_commit(stream));CHECK(read("empty.sav").empty());
    // Both transactions can remain open with interleaved writes. Only commit
    // exposes their complete bytes, and aborting one never removes the other.
    auto* left=recomp_atomic_file_begin("concurrent.sav",4);
    auto* right=recomp_atomic_file_begin("concurrent.sav",4);CHECK(left&&right);
    CHECK(recomp_atomic_file_write(left,initial,2));
    CHECK(recomp_atomic_file_write(right,latest,2));
    CHECK(recomp_atomic_file_write(left,initial+2,2));
    CHECK(recomp_atomic_file_commit(left));
    CHECK(read("concurrent.sav")==std::vector<unsigned char>(initial,initial+4));
    CHECK(recomp_atomic_file_write(right,latest+2,2));
    CHECK(recomp_atomic_file_commit(right));
    CHECK(read("concurrent.sav")==std::vector<unsigned char>(latest,latest+4));
    std::ofstream("foreign.tmp")<<"untouched";
    stream=recomp_atomic_file_begin("native.sav",0);CHECK(stream);
    recomp_atomic_file_abort(stream);
    CHECK(read("foreign.tmp")==std::vector<unsigned char>({'u','n','t','o','u','c','h','e','d'}));
    for(const auto& entry:std::filesystem::directory_iterator("."))
        CHECK(entry.path().filename().string().find(".tmp-")==std::string::npos);
    return failures?1:0;
}
