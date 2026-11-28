#pragma once
#include "state_envelope.hpp"
#include <future>
#include <functional>
#include <deque>

namespace recomp::persistence {
enum class Compression { none, zlib };
struct Limits {
    std::uint64_t max_snapshot_bytes = 512ull * 1024 * 1024;
    unsigned slot_count = 10;
};
struct Serializer {
    // Invoked only on the owning producer thread at its chosen safe boundary.
    std::function<std::uint64_t()> size;
    std::function<bool(void*, std::size_t, std::string&)> capture;
    // The engine owns transactional machine restore; failure must not damage
    // live machine state. Storage validates identity/data before this callback.
    std::function<bool(const void*, std::size_t, std::string&)> restore;
};
struct Result {
    bool ok = false;
    unsigned slot = 0;
    std::uint64_t bytes = 0;
    std::string detail;
};
class SlotStore {
public:
    SlotStore(std::filesystem::path directory, retro::state::StateIdentity identity,
              Serializer serializer, Limits limits = {});
    ~SlotStore();
    SlotStore(const SlotStore&) = delete;
    SlotStore& operator=(const SlotStore&) = delete;
    // Copies the captured snapshot/header/thumbnail before starting disk work.
    // Rejects a second submit while the previous write is uncollected.
    bool submit(unsigned slot, std::uint64_t frame, std::vector<std::uint8_t> thumbnail,
                Compression compression, std::string& error);
    bool busy() const;
    // false means no finished result yet; wait() drains without a wall cutoff.
    bool poll(Result& result);
    Result wait();
    bool load(unsigned slot, std::string& error);
    bool header(unsigned slot, retro::state::StateHeader& header,
                std::vector<std::uint8_t>* thumbnail, std::string& error) const;
    std::filesystem::path path(unsigned slot) const;
private:
    std::filesystem::path directory_;
    retro::state::StateIdentity identity_;
    Serializer serializer_;
    Limits limits_;
    std::future<Result> writing_;
};
// A bounded in-memory store for engine-owned snapshots, without serialization
// or machine callbacks. Evicts oldest complete snapshots to honor byte budget.
class RewindStore {
public:
    explicit RewindStore(std::size_t byte_budget) : RewindStore(byte_budget, 0) {}
    // Zero count keeps the original byte-budget-only policy.
    RewindStore(std::size_t byte_budget, std::size_t max_entries)
        : budget_(byte_budget), max_entries_(max_entries) {}
    bool push(std::uint64_t frame, std::vector<std::uint8_t> snapshot);
    bool pop(std::uint64_t& frame, std::vector<std::uint8_t>& snapshot);
    std::size_t bytes() const { return bytes_; }
    std::size_t count() const { return states_.size(); }
    void clear();
private:
    struct Entry { std::uint64_t frame;std::vector<std::uint8_t> bytes; };
    std::size_t budget_, max_entries_, bytes_ = 0;
    std::deque<Entry> states_;
};
}
