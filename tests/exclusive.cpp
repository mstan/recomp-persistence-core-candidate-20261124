// SPDX-License-Identifier: MIT
#include "recomp_persistence/atomic_file.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#endif
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::string read(const fs::path &p) {
    std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};
}
static std::string utf8(const fs::path &p) {
    auto u=p.u8string();return {u.begin(),u.end()};
}
int main() {
    const auto working=fs::current_path();
    const auto root=working/("exclusive-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(fs::create_directory(root));
    const auto path=root/fs::u8path(u8"native-\u03bb-\u732b.bin");
    const auto name=utf8(path);const std::string original(131072,'A'),other(131072,'B');
    CHECK(recomp_atomic_file_create(name.c_str(),original.data(),original.size()));
    CHECK(read(path)==original);
    CHECK(!recomp_atomic_file_create(name.c_str(),other.data(),other.size()));
    CHECK(read(path)==original);
    // An empty file and a directory are still existing destinations.
    const auto empty=root/"empty";const auto empty_name=utf8(empty);
    CHECK(recomp_atomic_file_create(empty_name.c_str(),nullptr,0));
    CHECK(!recomp_atomic_file_create(empty_name.c_str(),other.data(),other.size()));
    CHECK(fs::file_size(empty)==0);
    const auto dir=root/"directory";CHECK(fs::create_directory(dir));
    CHECK(!recomp_atomic_file_create(utf8(dir).c_str(),other.data(),other.size()));CHECK(fs::is_directory(dir));
    // A target arriving after begin wins; commit never replaces it.
    const auto late=root/"late";auto *staged=recomp_atomic_file_begin_create(utf8(late).c_str(),other.size());CHECK(staged);
    CHECK(recomp_atomic_file_write(staged,other.data(),other.size()));
    CHECK(recomp_atomic_file_store(utf8(late).c_str(),original.data(),original.size()));
    CHECK(!recomp_atomic_file_commit(staged));CHECK(read(late)==original);
    const auto aborted=root/"aborted";staged=recomp_atomic_file_begin_create(utf8(aborted).c_str(),1);CHECK(staged);
    CHECK(!recomp_atomic_file_write(staged,other.data(),other.size()));
    CHECK(!recomp_atomic_file_commit(staged));CHECK(!fs::exists(aborted));
    staged=recomp_atomic_file_begin_create(utf8(aborted).c_str(),other.size());CHECK(staged);
    CHECK(recomp_atomic_file_write(staged,other.data(),other.size()));recomp_atomic_file_abort(staged);CHECK(!fs::exists(aborted));
    // Relative paths bind once, independently of later CWD changes.
    fs::current_path(root);staged=recomp_atomic_file_begin_create("relative",original.size());CHECK(staged);
    CHECK(recomp_atomic_file_write(staged,original.data(),original.size()));fs::current_path(working);
    CHECK(recomp_atomic_file_commit(staged));CHECK(read(root/"relative")==original);
    // Two complete creators race at the final operation, with exactly one
    // winner; the winner's complete data is the only visible final content.
    const auto race=root/"race";const auto race_name=utf8(race);
    std::atomic<int> ready{0};std::atomic<bool> go{false};int a=0,b=0;
    auto create=[&](const std::string &bytes,int &out){
        auto *f=recomp_atomic_file_begin_create(race_name.c_str(),bytes.size());CHECK(f);
        CHECK(recomp_atomic_file_write(f,bytes.data(),bytes.size()));++ready;
        while(!go.load())std::this_thread::yield();out=recomp_atomic_file_commit(f);
    };
    std::thread first(create,std::cref(original),std::ref(a)),second(create,std::cref(other),std::ref(b));
    while(ready.load()!=2)std::this_thread::yield();go=true;first.join();second.join();
    CHECK(a+b==1);CHECK(read(race)==(a?original:other));
    const auto native=root/"native-exclusive";const auto native_name=utf8(native);
    CHECK(recomp_file_create_exclusive(native_name.c_str(),original.data(),original.size()));
    CHECK(read(native)==original);
    CHECK(!recomp_file_create_exclusive(native_name.c_str(),other.data(),other.size()));
    CHECK(read(native)==original);
    CHECK(!recomp_file_create_exclusive(empty_name.c_str(),other.data(),other.size()));
    CHECK(fs::file_size(empty)==0);
    CHECK(!recomp_file_create_exclusive(utf8(dir).c_str(),other.data(),other.size()));
    const auto link=root/"existing-link";std::error_code link_error;
    fs::create_symlink(path,link,link_error);
    if(!link_error){CHECK(!recomp_atomic_file_create(utf8(link).c_str(),other.data(),other.size()));CHECK(!recomp_file_create_exclusive(utf8(link).c_str(),other.data(),other.size()));CHECK(read(path)==original);CHECK(fs::is_symlink(link));CHECK(fs::remove(link));}
    else std::printf("symlink fixture unavailable on this host: %s\n",link_error.message().c_str());
#ifndef _WIN32
    struct stat mode{};CHECK(stat(name.c_str(),&mode)==0);CHECK((mode.st_mode&0777)==0600);
#endif
    // Successful and failed transactions leave no staging names behind.
    for(auto &entry:fs::directory_iterator(root))CHECK(utf8(entry.path().filename()).find(".tmp-")==std::string::npos);
    for(const auto &p:{path,empty,late,root/"relative",race,dir,native})CHECK(fs::remove(p));
    CHECK(fs::remove(root));
    std::puts("exclusive publication: UTF8, existing/late targets, quota/abort, CWD, racing complete creators and cleanup pass");
}
