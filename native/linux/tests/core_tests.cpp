// src/core.cpp alone: build ids, the socket's name and the replies.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core.h"

using namespace zap::linux_helper;

namespace {

int g_failures = 0;

void check(bool ok, const char* what, const std::string& detail = "") {
    if (ok) return;
    std::printf("FAIL: %s %s\n", what, detail.c_str());
    ++g_failures;
}

void put32(std::vector<unsigned char>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(v >> (8 * i)));
}

void note(std::vector<unsigned char>& out, const char* name, uint32_t type,
          std::vector<unsigned char> desc) {
    uint32_t namesz = static_cast<uint32_t>(std::strlen(name) + 1);
    put32(out, namesz);
    put32(out, static_cast<uint32_t>(desc.size()));
    put32(out, type);
    for (uint32_t i = 0; i < namesz; ++i) out.push_back(static_cast<unsigned char>(name[i]));
    while (out.size() % 4) out.push_back(0);
    out.insert(out.end(), desc.begin(), desc.end());
    while (out.size() % 4) out.push_back(0);
}

void test_builds() {
    check(identify_build("4c4416eed9e8a1fa1b373ae3b1badec33510fc90") != nullptr &&
              std::strcmp(identify_build("4c4416eed9e8a1fa1b373ae3b1badec33510fc90"), "B1") == 0,
          "B1's Linux binary is B1");
    check(identify_build("4c4416eed9e8a1fa1b373ae3b1badec33510fc91") == nullptr,
          "another build id is not known");
    check(identify_build("") == nullptr, "no build id is not known");
}

void test_notes() {
    std::vector<unsigned char> notes;
    note(notes, "GNU", 1, {0, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0});  // ABI tag first
    note(notes, "GNU", 3, {0x4c, 0x44, 0x16, 0xee, 0x00, 0xff});
    auto id = note_build_id(notes.data(), notes.size());
    check(id && *id == "4c4416ee00ff", "the build id note is found after another",
          id ? *id : "none");

    std::vector<unsigned char> other;
    note(other, "Go", 3, {1, 2, 3, 4});
    check(!note_build_id(other.data(), other.size()), "a type-3 note not GNU's is not it");

    std::vector<unsigned char> cut = notes;
    cut.resize(cut.size() - 4);
    check(!note_build_id(cut.data(), cut.size()) || *note_build_id(cut.data(), cut.size()) == "",
          "a note cut short is not read past its end");
    std::vector<unsigned char> huge;
    put32(huge, 4);
    put32(huge, 0xFFFFFFF0u);
    put32(huge, 3);
    for (char c : {'G', 'N', 'U', '\0'}) huge.push_back(static_cast<unsigned char>(c));
    check(!note_build_id(huge.data(), huge.size()), "a note claiming more than there is fails");
}

// A stand-in for this process's memory: 0x1000 to 0x1010 holds "zexion-ap memory".
bool fake_read(uint64_t address, size_t size, std::string& out) {
    static const std::string memory = "zexion-ap memory";
    if (address < 0x1000 || address + size > 0x1000 + memory.size()) {
        out = "Bad_address";
        return false;
    }
    out = memory.substr(address - 0x1000, size);
    return true;
}

void test_replies() {
    State s;
    check(reply("hello", s, fake_read) ==
              std::string("zexion-ap ") + kVersion + " build=unknown hooked=0 platform=linux folder=0",
          "hello for an unknown build", reply("hello", s, fake_read));
    s.build = "B1";
    s.hooked = true;
    s.folder = true;
    s.frames = 1234;
    check(reply("hello", s, fake_read) ==
              std::string("zexion-ap ") + kVersion + " build=B1 hooked=1 platform=linux folder=1",
          "hello for B1", reply("hello", s, fake_read));
    check(reply("ping", s, fake_read) == "pong frames=1234", "ping", reply("ping", s, fake_read));
    check(reply("unload", s, fake_read) == "error unknown request", "a request it does not serve");
    check(reply(std::string_view("hello\0", 6), s, fake_read) == "error unknown request",
          "a request is the whole message");
}

void test_reads() {
    check(reply("read 0x1000 9", State{}, fake_read) == "data zexion-ap", "a read in hex",
          reply("read 0x1000 9", State{}, fake_read));
    check(reply("read 4106 6", State{}, fake_read) == "data memory", "a read in decimal",
          reply("read 4106 6", State{}, fake_read));
    check(reply("read 0x1000 20", State{}, fake_read) == "error Bad_address",
          "a read past what is mapped is the reader's error");
    for (const char* bad : {"read", "read 0x1000", "read 0x1000 x", "read 0xg 4", "read -1 4",
                            "read 0x1000 4 5", "read 0x 4", "read 0x10000000000000000 4"}) {
        check(reply(bad, State{}, fake_read) == "error read takes an address and a size",
              "a read not parsed", bad);
    }
    check(reply("read 0x1000 0", State{}, fake_read).starts_with("error read size must be 1 to"),
          "a read of nothing");
    check(reply("read 0x1000 " + std::to_string(kMaxRead + 1), State{}, fake_read)
              .starts_with("error read size must be 1 to"),
          "a read too big for one reply");
    check(reply("read 0xffffffffffffffff 2", State{}, fake_read) ==
              "error read past the end of memory",
          "a read wrapping around");
    check(kMaxRead + sizeof kDataPrefix < kMaxRequest, "the largest read's reply fits");
}

}  // namespace

int main() {
    test_builds();
    test_notes();
    test_replies();
    test_reads();
    check(socket_name(4242) == "zexion-ap-4242", "the socket's name");
    if (g_failures == 0) std::printf("linux core: all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
