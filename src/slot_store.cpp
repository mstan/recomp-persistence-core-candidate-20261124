#include "recomp_persistence/slot_store.hpp"
#include "recomp_persistence/sha256.hpp"
#include <zlib.h>
#include <chrono>
#include <ctime>
#include <limits>
#include <cstdio>
#include <stdexcept>

namespace recomp::persistence {
SlotStore::SlotStore(std::filesystem::path directory, retro::state::StateIdentity identity,
                     Serializer serializer, Limits limits)
    : directory_(std::move(directory)), identity_(std::move(identity)),
      serializer_(std::move(serializer)), limits_(limits) {
    if(!limits_.max_snapshot_bytes || limits_.max_snapshot_bytes > std::numeric_limits<uLong>::max() ||
       !limits_.slot_count || limits_.slot_count>1000 || !serializer_.size ||
       !serializer_.capture || !serializer_.restore)
        throw std::invalid_argument("invalid persistence limits or serializer");
    // New storage uses build-specific disposable snapshots, never an implicit
    // compatibility promise from a previous core/version.
    identity_.state_compat_id.reset();
    if(identity_.core_id.empty() || identity_.core_sha256.empty() || identity_.content_sha256.empty())
        throw std::invalid_argument("snapshot identity requires core, build and content");
}
SlotStore::~SlotStore() { if(writing_.valid()) writing_.wait(); }
std::filesystem::path SlotStore::path(unsigned slot) const {
    if(slot>=limits_.slot_count) throw std::out_of_range("snapshot slot");
    char name[32];std::snprintf(name,sizeof(name),"slot%03u.rstate",slot+1);
    return directory_/name;
}
bool SlotStore::busy() const {
    return writing_.valid() && writing_.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}
bool SlotStore::submit(unsigned slot,std::uint64_t frame,std::vector<std::uint8_t> thumbnail,
                       Compression compression,std::string& error) {
    error.clear();
    if(writing_.valid()) {error="collect the previous snapshot write first";return false;}
    if(slot>=limits_.slot_count) {error="snapshot slot out of range";return false;}
    if(compression!=Compression::none && compression!=Compression::zlib) {error="unsupported snapshot codec";return false;}
    try {
        const auto size=serializer_.size();
        if(!size || size>limits_.max_snapshot_bytes) {error="snapshot size exceeds limits";return false;}
        if(!thumbnail.empty() && thumbnail.size()!=retro::state::kThumbWidth*retro::state::kThumbHeight*4u) {
            error="invalid thumbnail size";return false;
        }
        std::vector<std::uint8_t> snapshot((std::size_t)size);
        if(!serializer_.capture(snapshot.data(),snapshot.size(),error)) return false;
        retro::state::StateHeader header;
        header.identity=identity_;header.frame_number=frame;header.saved_unix=(std::int64_t)std::time(nullptr);
        const auto destination=path(slot);
        writing_=std::async(std::launch::async,
            [destination,header=std::move(header),snapshot=std::move(snapshot),
             thumbnail=std::move(thumbnail),compression,slot]() mutable {
                Result result;result.slot=slot;result.bytes=snapshot.size();
                try {
                    header.decoded_size=snapshot.size();
                    header.decoded_sha256=retro::runner::sha256_hex(snapshot.data(),snapshot.size());
                    if(compression==Compression::zlib) {
                        uLongf size=compressBound((uLong)snapshot.size());
                        std::vector<std::uint8_t> packed(size);
                        if(compress2(packed.data(),&size,snapshot.data(),(uLong)snapshot.size(),Z_DEFAULT_COMPRESSION)!=Z_OK) {
                            result.detail="snapshot compression failed";return result;
                        }
                        packed.resize(size);snapshot=std::move(packed);header.payload_codec="zlib";
                    } else header.payload_codec="raw";
                    result.ok=retro::state::write_state(destination,header,thumbnail,snapshot.data(),snapshot.size(),&result.detail);
                } catch(const std::exception& e) {result.detail=e.what();}
                return result;
            });
        return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}
bool SlotStore::poll(Result& result) {
    if(!writing_.valid() || busy()) return false;
    result=writing_.get();return true;
}
Result SlotStore::wait() {
    if(!writing_.valid()) return Result{false,0,0,"no pending snapshot"};
    return writing_.get();
}
bool SlotStore::header(unsigned slot,retro::state::StateHeader& header,
                      std::vector<std::uint8_t>* thumbnail,std::string& error) const {
    if(slot>=limits_.slot_count) {error="snapshot slot out of range";return false;}
    try { return retro::state::read_state_header(path(slot),header,thumbnail,&error); }
    catch(const std::exception& e) {error=e.what();return false;}
}
bool SlotStore::load(unsigned slot,std::string& error) {
    error.clear();
    if(writing_.valid()) {error="collect the snapshot write before loading";return false;}
    if(slot>=limits_.slot_count) {error="snapshot slot out of range";return false;}
    try {
        retro::state::StateHeader header;
        std::vector<std::uint8_t> stored;
        const auto max_stored=(std::uint64_t)compressBound((uLong)limits_.max_snapshot_bytes);
        if(!retro::state::read_state(path(slot),header,stored,&error,max_stored)) return false;
        if(!(error=retro::state::check_state(header,identity_,stored)).empty()) return false;
        if(header.decoded_size>limits_.max_snapshot_bytes) {error="decoded snapshot exceeds limit";return false;}
        if(header.payload_codec=="zlib") {
            if(!header.decoded_size) {error="empty decoded snapshot";return false;}
            std::vector<std::uint8_t> decoded((std::size_t)header.decoded_size);
            uLongf size=(uLongf)decoded.size();uLong source=(uLong)stored.size();
            if(uncompress2(decoded.data(),&size,stored.data(),&source)!=Z_OK ||
               size!=decoded.size() || source!=stored.size()) {error="invalid compressed snapshot";return false;}
            stored=std::move(decoded);
        } else if(header.payload_codec!="raw") {error="unsupported snapshot codec";return false;}
        if(stored.empty() || stored.size()>limits_.max_snapshot_bytes ||
           (header.decoded_size && stored.size()!=header.decoded_size) ||
           (!header.decoded_sha256.empty() && retro::runner::sha256_hex(stored.data(),stored.size())!=header.decoded_sha256)) {
            error="decoded snapshot checksum/size mismatch";return false;
        }
        return serializer_.restore(stored.data(),stored.size(),error);
    } catch(const std::exception& e) {error=e.what();return false;}
}
bool RewindStore::push(std::uint64_t frame,std::vector<std::uint8_t> snapshot) {
    if(snapshot.empty() || snapshot.size()>budget_) return false;
    // Allocate the new deque entry before evicting accepted history. If
    // allocation fails the existing complete snapshots are unchanged.
    const auto added=snapshot.size();
    states_.push_back({frame,std::move(snapshot)});
    while(bytes_>budget_-added || (max_entries_ && states_.size()>max_entries_)) {
        bytes_-=states_.front().bytes.size();states_.pop_front();
    }
    bytes_+=added;return true;
}
bool RewindStore::pop(std::uint64_t& frame,std::vector<std::uint8_t>& snapshot) {
    if(states_.empty()) return false;
    frame=states_.back().frame;bytes_-=states_.back().bytes.size();snapshot=std::move(states_.back().bytes);states_.pop_back();return true;
}
void RewindStore::clear() {states_.clear();bytes_=0;}
}
