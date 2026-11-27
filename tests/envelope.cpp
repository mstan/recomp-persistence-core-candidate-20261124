// Envelope cases extracted from Retro-Runtime tests/overlay_test.cpp; MIT.
#include "recomp_persistence/state_envelope.hpp"
#include <cstdio>
#include <fstream>
#include <filesystem>
namespace fs=std::filesystem;
namespace state=retro::state;
static int g_failures=0;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);++g_failures;}}while(0)
void test_envelope(const fs::path& dir) {
    state::StateIdentity id;
    id.abi_major = 0;
    id.core_id = "fake";
    id.core_sha256 = "c0";
    id.package_sha256 = "p0";
    id.content_sha256 = "r0";
    id.accessories.push_back({0, 0, "n64.transfer_pak", "g0"});
    // A data accessory (rev 7): no content, so its hash is empty and its
    // header line ends in the comma -- `accessory=1,0,n64.vru,`.
    id.accessories.push_back({1, 0, "n64.vru", ""});
    id.sim_options["cpu.overclock"] = "1";
    id.sim_options["region"] = std::nullopt;
    id.sim_options["note"] = "a=b\nc\\d"; // escaping survives
    state::StateHeader h;
    h.identity = id;
    h.frame_number = 77;
    h.saved_unix = 1790000000;
    const std::vector<std::uint8_t> bytes = {1, 2, 3, 4, 5};
    const fs::path p = dir / "env.rstate";
    std::string err;
    CHECK(state::write_state(p, h, {}, bytes.data(), bytes.size(), &err));
    CHECK(state::is_envelope(p));
    state::StateHeader back;
    std::vector<std::uint8_t> got;
    CHECK(state::read_state(p, back, got, &err));
    CHECK(got == bytes && back.frame_number == 77 && back.thumb_w == 0);
    CHECK(back.identity.sim_options == id.sim_options);
    CHECK(back.identity.accessories.size() == 2 && back.identity.accessories[0] == id.accessories[0]);
    CHECK(back.identity.accessories.size() == 2 && back.identity.accessories[1] == id.accessories[1]);
    CHECK(back.identity.accessories.size() == 2 && back.identity.accessories[1].content_sha256.empty());
    CHECK(state::check_state(back, id, got).empty());

    // Native Unicode paths must use UTF-8 bytes at the C boundary under both
    // C++17's std::string and C++20's std::u8string representation.
    const auto unicode_path = dir / fs::u8path("env-\xCE\xBB-\xE7\x8C\xAB.rstate");
    CHECK(state::write_state(unicode_path, h, {}, bytes.data(), bytes.size(), &err));
    state::StateHeader unicode_header;
    std::vector<std::uint8_t> unicode_bytes;
    CHECK(state::read_state(unicode_path, unicode_header, unicode_bytes, &err));
    CHECK(unicode_bytes == bytes && unicode_header.frame_number == h.frame_number);

    auto refused = [&](state::StateIdentity running, const std::string& want) {
        const std::string why = state::check_state(back, running, got);
        if (why.find(want) != 0) {
            std::fprintf(stderr, "  wanted \"%s...\", got \"%s\"\n", want.c_str(), why.c_str());
            ++g_failures;
        }
    };
    state::StateIdentity r = id;
    r.abi_major = 1;
    refused(r, "rcore ABI major differs");
    r = id;
    r.core_id = "other";
    refused(r, "core id differs: the state has fake, this session has other");
    r = id;
    r.core_sha256 = "c1";
    refused(r, "core build");
    r = id;
    r.state_compat_id = "v1"; // the running core promises; the state predates it
    refused(r, "state_compat_id differs");
    r = id;
    r.package_sha256 = "p1";
    refused(r, "game package SHA-256 differs");
    r = id;
    r.content_sha256 = "r1";
    refused(r, "content SHA-256 differs");
    r = id;
    r.accessories.clear();
    refused(r, "accessories differ");
    r = id;
    r.accessories.pop_back(); // the VRU unplugged: a state taken with it is refused
    refused(r, "accessories differ: the state has seat 0 slot 0 n64.transfer_pak g0; seat 1 slot 0 n64.vru, "
               "this session has seat 0 slot 0 n64.transfer_pak g0");
    r = id;
    r.accessories[1].seat = 2; // the VRU on another seat
    refused(r, "accessories differ");
    r = id;
    r.sim_options["cpu.overclock"] = "0";
    refused(r, "option cpu.overclock differs: the state has 1, this session has 0");
    r = id;
    r.sim_options.erase("region");
    refused(r, "option region differs");
    std::vector<std::uint8_t> corrupt = got;
    corrupt[2] ^= 1;
    CHECK(state::check_state(back, id, corrupt).find("corrupt") != std::string::npos);

    // The compat id, when both carry it, replaces the file hash.
    state::StateHeader promised = back;
    promised.identity.state_compat_id = "v1";
    r = id;
    r.state_compat_id = "v1";
    r.core_sha256 = "a rebuild";
    CHECK(state::check_state(promised, r, got).empty());

    // ... and so does the game package's hash: a promising core's own load
    // check decides whether a state survives a regenerated package. Content
    // is still compared.
    r.package_sha256 = "a regenerated package";
    CHECK(state::check_state(promised, r, got).empty());
    r.content_sha256 = "r1";
    CHECK(state::check_state(promised, r, got).find("content SHA-256 differs") == 0);

    // A bare state is not an envelope; a truncated one is refused, not trusted.
    const fs::path bare = dir / "bare.state";
    std::ofstream(bare, std::ios::binary) << "raw core bytes";
    CHECK(!state::is_envelope(bare));
    CHECK(!state::read_state_header(bare, back, nullptr, &err));
    fs::resize_file(p, fs::file_size(p) - 2);
    CHECK(!state::read_state(p, back, got, &err));
}


int main(){const auto dir=fs::current_path();test_envelope(dir);return g_failures?1:0;}
