#include "recomp_persistence/slot_store.hpp"
#include "recomp_persistence/sha256.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <atomic>
using namespace recomp::persistence;
namespace fs=std::filesystem;
namespace state=retro::state;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);++failures;}}while(0)
int main() {
    state::StateIdentity identity;identity.core_id="synthetic";identity.core_sha256="build-one";identity.content_sha256="content-one";
    identity.sim_options["simulation.mode"]="native";
    std::vector<unsigned char> machine(128*1024,0x37);const auto snapshot=machine;
    unsigned restores=0;const auto producer=std::this_thread::get_id();
    Serializer callbacks;
    callbacks.size=[&]{CHECK(std::this_thread::get_id()==producer);return machine.size();};
    callbacks.capture=[&](void* p,size_t n,std::string&){CHECK(std::this_thread::get_id()==producer);std::memcpy(p,machine.data(),n);return true;};
    callbacks.restore=[&](const void* p,size_t n,std::string&){CHECK(std::this_thread::get_id()==producer);machine.assign((const unsigned char*)p,(const unsigned char*)p+n);++restores;return true;};
    SlotStore slots("slots",identity,callbacks,Limits{256*1024,3});std::string error;
    CHECK(slots.submit(0,7,{},Compression::zlib,error));CHECK(!slots.submit(1,8,{},Compression::none,error));
    machine[0]=99;const auto result=slots.wait();CHECK(result.ok && result.bytes==snapshot.size());
    state::StateHeader header;CHECK(slots.header(0,header,nullptr,error));
    CHECK(header.payload_codec=="zlib" && header.frame_number==7 && header.decoded_size==snapshot.size());
    CHECK(header.state_size<snapshot.size());CHECK(slots.load(0,error));CHECK(machine==snapshot && restores==1);
    CHECK(slots.submit(0,8,{},Compression::none,error));CHECK(slots.wait().ok); // replace existing Windows slot
    CHECK(slots.load(0,error));CHECK(restores==2);
    auto other=identity;other.core_sha256="build-two";SlotStore wrong_build("slots",other,callbacks);
    CHECK(!wrong_build.load(0,error));CHECK(restores==2 && error.find("core build")!=std::string::npos);
    other=identity;other.content_sha256="other-content";SlotStore wrong_content("slots",other,callbacks);
    CHECK(!wrong_content.load(0,error));CHECK(restores==2);
    const auto slot_path=slots.path(0);auto file_size=fs::file_size(slot_path);
    {std::fstream file(slot_path,std::ios::binary|std::ios::in|std::ios::out);file.seekp((std::streamoff)file_size-1);char b=0;file.write(&b,1);}
    CHECK(!slots.load(0,error));CHECK(restores==2);
    CHECK(!slots.submit(3,0,{},Compression::none,error));CHECK(!slots.load(3,error));
    CHECK(!slots.submit(0,0,{1},Compression::none,error));
    SlotStore limited("slots",identity,callbacks,Limits{16,3});CHECK(!limited.submit(0,0,{},Compression::none,error));
    fs::create_directories("blocked");std::ofstream("blocked/not-directory")<<"keep";
    SlotStore blocked("blocked/not-directory",identity,callbacks);CHECK(blocked.submit(0,0,{},Compression::none,error));CHECK(!blocked.wait().ok);
    // Corrupt lengths must be rejected before large allocation/decompression.
    state::StateHeader malicious;malicious.identity=identity;malicious.payload_codec="zlib";malicious.decoded_size=UINT64_MAX;
    unsigned char bytes[]={1,2,3};CHECK(state::write_state(slots.path(1),malicious,{},bytes,3,&error));
    CHECK(!slots.load(1,error));CHECK(restores==2);
    RewindStore rewind(6);CHECK(rewind.push(1,{1,2,3}));CHECK(rewind.push(2,{4,5,6}));CHECK(rewind.push(3,{7,8,9}));
    CHECK(rewind.count()==2 && rewind.bytes()==6);std::vector<unsigned char> got;std::uint64_t frame=0;
    CHECK(rewind.pop(frame,got));CHECK(frame==3 && got[0]==7);CHECK(rewind.pop(frame,got));CHECK(frame==2 && got[0]==4);
    CHECK(!rewind.pop(frame,got));CHECK(!rewind.push(4,std::vector<unsigned char>(7)));rewind.clear();CHECK(!rewind.count());
    RewindStore counted(16,2);
    CHECK(counted.push(10,{1}));CHECK(counted.push(11,{2,3}));CHECK(counted.push(12,{4,5,6}));
    CHECK(counted.count()==2 && counted.bytes()==5);
    CHECK(counted.pop(frame,got) && frame==12 && got.size()==3);
    CHECK(counted.pop(frame,got) && frame==11 && got.size()==2);
    CHECK(!counted.pop(frame,got));
    RewindStore unlimited(16,0);
    for(unsigned i=0;i<5;++i)CHECK(unlimited.push(i,{1}));
    CHECK(unlimited.count()==5 && unlimited.bytes()==5);
    RewindStore both(4,3);
    CHECK(both.push(1,{1,2,3}));CHECK(both.push(2,{4,5}));
    CHECK(both.count()==1 && both.bytes()==2);
    CHECK(!both.push(3,std::vector<unsigned char>(5)));
    CHECK(both.count()==1 && both.bytes()==2);
    CHECK(both.pop(frame,got) && frame==2 && got[0]==4);
    CHECK(retro::runner::sha256_hex("abc",3)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    return failures?1:0;
}
