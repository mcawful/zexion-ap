// src/core.cpp alone: build ids, the socket's name, the replies and the token's folder.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

// A stand-in for this process's memory: 0x1000 to 0x1010 holds "zexion-ap memory", writable
// from 0x1000 to 0x1008.
std::string g_memory = "zexion-ap memory";
bool fake_read(uint64_t address, size_t size, std::string& out) {
    if (address < 0x1000 || address + size > 0x1000 + g_memory.size()) {
        out = "Bad_address";
        return false;
    }
    out = g_memory.substr(address - 0x1000, size);
    return true;
}

bool fake_write(uint64_t address, const std::string& data, std::string& out) {
    if (address < 0x1000 || address + data.size() > 0x1008) {
        out = "not_in_the_game's_writable_data";
        return false;
    }
    g_memory.replace(address - 0x1000, data.size(), data);
    return true;
}

std::string reply_(std::string_view request, const State& state, const Reader& read) {
    return reply(request, state, read, fake_write);
}

void test_writes() {
    State s;
    check(reply_("write 0x1000 5a45", s, fake_read) == "written 2", "a write in hex");
    check(reply_("read 0x1000 4", s, fake_read) == "data ZExi", "it reads back",
          reply_("read 0x1000 4", s, fake_read));
    check(reply_("write 0x1008 00", s, fake_read) == "error not_in_the_game's_writable_data",
          "a write outside the writable data is the writer's refusal");
    for (const char* bad : {"write", "write 0x1000", "write 0x1000 5", "write 0x1000 5g",
                            "write x 00", "write 0x1000 "}) {
        check(reply_(bad, s, fake_read) == "error write takes an address and hex bytes",
              "a write not parsed", bad);
    }
    std::string big(2 * (kMaxWrite + 1), '0');
    check(reply_("write 0x1000 " + big, s, fake_read).starts_with("error write size must be"),
          "a write too big");
    check(reply_("write 0xffffffffffffffff 0000", s, fake_read) ==
              "error write past the end of memory",
          "a write wrapping around");
    g_memory = "zexion-ap memory";
}

// The token's gate (core.h): `hello` and the right `auth` pass, nothing else does.
void test_gate() {
    State s;
    s.build = "B1";
    s.hooked = true;
    const std::string token(2 * kTokenBytes, 'a');
    const unsigned char bytes[3] = {0x00, 0x0f, 0xa5};
    check(hex_text(bytes, 3) == "000fa5", "hex of bytes", hex_text(bytes, 3));
    check(is_token_text(token), "a token is 64 hex digits");
    check(!is_token_text(token.substr(1)) && !is_token_text(token + "a") &&
              !is_token_text(std::string(2 * kTokenBytes, 'A')) &&
              !is_token_text(std::string(2 * kTokenBytes, 'g')) && !is_token_text(""),
          "a token is exactly 64 lower-case hex digits");
    check(same_secret("abc", "abc") && !same_secret("abc", "abd") && !same_secret("abc", "ab") &&
              !same_secret("ab", "abc") && same_secret("", ""),
          "secrets compare whole");
    Gate hello = gate("hello", token, s);
    check(hello.answer == reply_("hello", s, fake_read) && !hello.authed && !hello.drop,
          "hello is answered before the token and does not authenticate", hello.answer);
    Gate good = gate("auth " + token, token, s);
    check(good.answer == "ok" && good.authed && !good.drop, "the right token passes", good.answer);
    Gate wrong = gate("auth " + std::string(2 * kTokenBytes, 'b'), token, s);
    check(wrong.answer == "error auth refused" && !wrong.authed && wrong.drop,
          "another token is refused and dropped", wrong.answer);
    Gate empty = gate("auth ", token, s);
    check(!empty.authed && empty.drop, "an empty token is refused");
    Gate unset = gate("auth " + token, "", s);
    check(!unset.authed && unset.drop, "no token held: no one passes");
    for (const char* asked : {"ping", "read 0x1000 4", "write 0x1000 00", "config", "callers", "auth"}) {
        Gate refused = gate(asked, token, s);
        check(refused.answer == "error auth required" && !refused.authed && refused.drop,
              "nothing else before the token", asked);
    }
}

// The ranges a client may write (core.h): offsets from gGameState, whole or not at all.
void test_write_ranges() {
    const uint64_t g = 0x1000000;
    auto ranges = client_write_ranges(g);
    check(ranges.size() == 5, "five ranges: two runs of supply numbers, items, cells, grid");
    check(within_ranges(ranges, g + 0x1CD50 + 0x108E, 1), "the first inventory byte");
    check(within_ranges(ranges, g + 0x1CD50 + 0x108E + 49, 1), "the last writable item");
    check(!within_ranges(ranges, g + 0x1CD50 + 0x108E + 50, 1), "not past the items");
    check(!within_ranges(ranges, g + 0x1CD50 + 0x108E + 49, 2), "not across the end");
    const uint64_t p = g + 0x1CD50;
    for (uint64_t field : {0xCBC, 0xCBE, 0xCC0, 0xCC2, 0xCEA, 0xCEC}) {
        check(within_ranges(ranges, p + field, 2), "a supply number", std::to_string(field));
    }
    check(within_ranges(ranges, p + 0xCBC, 8) && within_ranges(ranges, p + 0xCEA, 4),
          "the runs of neighbors");
    check(!within_ranges(ranges, p + 0xCBB, 2), "not before the first");
    check(!within_ranges(ranges, p + 0xCC4, 2) && !within_ranges(ranges, p + 0xCD0, 8) &&
              !within_ranges(ranges, p + 0xCE8, 2) && !within_ranges(ranges, p + 0xCC2, 4),
          "not the unrecorded fields between the numbers");
    check(!within_ranges(ranges, p + 0xCEE, 2), "not past the last");
    check(!within_ranges(ranges, g + 0x1CD50 + 0x168, 8), "not the player's move procedure");
    check(!within_ranges(ranges, g + 0x1CD50 + 0xFE8, 8), "not the player's shield link");
    check(within_ranges(ranges, g + 0x15B61 + 8, 1), "a cell's item state");
    check(within_ranges(ranges, g + 0x14FFD + 54 * 54 - 1, 1), "the grid's last byte");
    check(!within_ranges(ranges, g + 0x14FFD - 1, 1), "not before the grid");
    check(!within_ranges(ranges, g + 0x15B61 + 54 * 54 * 9, 1), "not past the cell states");
    check(!within_ranges(ranges, g, 1), "not gGameState's start");
    check(!within_ranges(ranges, g + 0x1CD50 + 0x108E, 0), "an empty write");
    check(!within_ranges(ranges, UINT64_MAX, 2), "a wrapping write");
}

// The filter that builds the real list (helper.cpp, set_client_writes) against the layout
// docs/re/linux.md records for B1: gGameState at 0x9E7840, 0x8CCCF8 bytes, in .bss.
void test_keeping_ranges() {
    const SymbolSpan state{0x9E7840, 0x8CCCF8};
    auto in_bss = [](uint64_t at, uint64_t size) {
        return at >= 0x9E7000 && at + size <= 0x9E7840 + 0x8CCCF8 + 0x1000;
    };
    std::vector<Range> dropped;
    auto kept = keep_ranges(client_write_ranges(state.value), state, in_bss, dropped);
    check(kept.size() == 5 && dropped.empty(), "the recorded layout keeps all five");
    check(within_ranges(kept, 0x9E7840 + 0x1CD50 + 0x108E, 1), "an item byte, as B1's address");
    check(within_ranges(kept, 0x9FC83D, 1) && within_ranges(kept, 0x9FD3A1, 1),
          "the map grid's and the cell states' first bytes, as the client's targets record them");
    dropped.clear();
    kept = keep_ranges(client_write_ranges(state.value), {state.value, 0x1000}, in_bss,
                       dropped);
    check(kept.empty() && dropped.size() == 5, "a symbol too short for them keeps none");
    dropped.clear();
    kept = keep_ranges(client_write_ranges(state.value), {state.value, 0}, in_bss, dropped);
    check(kept.size() == 5, "a symbol with no size is not a reason to drop");
    dropped.clear();
    kept = keep_ranges(client_write_ranges(0x400000), {0x400000, 0x8CCCF8},
                       [](uint64_t, uint64_t) { return false; }, dropped);
    check(kept.empty() && dropped.size() == 5, "none outside the game's data");
}

void test_replies() {
    State s;
    check(reply_("hello", s, fake_read) ==
              std::string("zexion-ap ") + kVersion + " build=unknown hooked=0 platform=linux folder=0",
          "hello for an unknown build", reply_("hello", s, fake_read));
    s.build = "B1";
    s.hooked = true;
    s.folder = true;
    s.frames = 1234;
    check(reply_("hello", s, fake_read) ==
              std::string("zexion-ap ") + kVersion + " build=B1 hooked=1 platform=linux folder=1",
          "hello for B1", reply_("hello", s, fake_read));
    check(reply_("ping", s, fake_read) == "pong frames=1234", "ping", reply_("ping", s, fake_read));
    check(reply_("callers", s, fake_read) == "callers", "callers before any frame call",
          reply_("callers", s, fake_read));
    s.callers = {{0x652557, 3}, {0x4caf9f, 1231}};
    check(reply_("callers", s, fake_read) == "callers 0x4caf9f=1231 0x652557=3",
          "callers, lowest address first", reply_("callers", s, fake_read));
    s.other_callers = 2;
    check(reply_("callers", s, fake_read) == "callers 0x4caf9f=1231 0x652557=3 other=2",
          "callers past the table's room", reply_("callers", s, fake_read));
    check(reply_("unload", s, fake_read) == "error unknown request", "a request it does not serve");
    check(reply_(std::string_view("hello\0", 6), s, fake_read) == "error unknown request",
          "a request is the whole message");
}

void test_reads() {
    check(reply_("read 0x1000 9", State{}, fake_read) == "data zexion-ap", "a read in hex",
          reply_("read 0x1000 9", State{}, fake_read));
    check(reply_("read 4106 6", State{}, fake_read) == "data memory", "a read in decimal",
          reply_("read 4106 6", State{}, fake_read));
    check(reply_("read 0x1000 20", State{}, fake_read) == "error Bad_address",
          "a read past what is mapped is the reader's error");
    for (const char* bad : {"read", "read 0x1000", "read 0x1000 x", "read 0xg 4", "read -1 4",
                            "read 0x1000 4 5", "read 0x 4", "read 0x10000000000000000 4"}) {
        check(reply_(bad, State{}, fake_read) == "error read takes an address and a size",
              "a read not parsed", bad);
    }
    check(reply_("read 0x1000 0", State{}, fake_read).starts_with("error read size must be 1 to"),
          "a read of nothing");
    check(reply_("read 0x1000 " + std::to_string(kMaxRead + 1), State{}, fake_read)
              .starts_with("error read size must be 1 to"),
          "a read too big for one reply");
    check(reply_("read 0xffffffffffffffff 2", State{}, fake_read) ==
              "error read past the end of memory",
          "a read wrapping around");
    check(kMaxRead + sizeof kDataPrefix < kMaxRequest, "the largest read's reply fits");
}

void test_own_symbols() {
    std::FILE* f = std::fopen("/proc/self/exe", "rb");
    std::string elf;
    char chunk[65536];
    for (size_t n; f != nullptr && (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) elf.append(chunk, n);
    if (f != nullptr) std::fclose(f);
    auto found = find_symbols(elf, {"main", "zap_no_such_symbol"});
    check(found.count("main") == 1 && found["main"].value != 0 && found["main"].size != 0,
          "main is found in this program's own symbols");
    check(found.count("zap_no_such_symbol") == 0, "a missing name is left out");
    check(find_symbols("not an ELF file", {"main"}).empty(), "a file not ELF finds nothing");
}

}  // namespace

// The folder the token goes in (make_private_folders): made 0700, a wider one narrowed, a link
// refused (a link above it is fine), another user's refused (not tried here: no second user).
void test_private_folders() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("zap_core_private_" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    auto mode = [](const fs::path& p) {
        struct stat st {};
        return stat(p.c_str(), &st) == 0 ? (st.st_mode & 0777) : 0xFFFFu;
    };
    const fs::path made = root / "a" / "b" / "ZexionAP";
    check(make_private_folders(made.string()) && mode(made) == 0700 && mode(root / "a") == 0700,
          "a folder and its parents made, for the user alone");
    check(make_private_folders(made.string()) && mode(made) == 0700, "there already: still so");
    chmod(made.c_str(), 0755);
    check(make_private_folders(made.string()) && mode(made) == 0700,
          "one that is wider is narrowed to the user's alone");
    // A link at the folder itself is refused, and nothing is changed through it.
    const fs::path target = root / "target";
    fs::create_directories(target);
    chmod(target.c_str(), 0755);
    const fs::path link = root / "link" / "ZexionAP";
    fs::create_directories(link.parent_path());
    fs::create_symlink(target, link);
    std::string why;
    check(!make_private_folders(link.string(), &why) && mode(target) == 0755 &&
              why == "it is a link",
          "a link in the folder's place is refused, as a link, and its target left as it is", why);
    // A link above it is the user's own (a dotfiles manager's ~/.local/share).
    const fs::path above = root / "above";
    fs::create_symlink(target, above);
    check(make_private_folders((above / "ZexionAP").string()) && mode(target / "ZexionAP") == 0700,
          "a link above the folder is followed");
    // A file in its place is not a folder.
    const fs::path file = root / "file";
    std::FILE* f = std::fopen(file.c_str(), "w");
    if (f != nullptr) std::fclose(f);
    why.clear();
    check(!make_private_folders(file.string(), &why) && why == "it is not a folder",
          "a file is not a folder", why);
    fs::remove_all(root);
}

// A call through a wrapper runs frame work once a game frame (core.h, wrapper_call_is_due): the
// first of a frame's calls, apart from the last one's return by the gap, or past the backstop.
void test_a_wrapper_call_is_due_after_the_gap_or_the_backstop() {
    const int64_t t = 10'000'000;
    check(wrapper_call_is_due(t, kBeforeAnyCall, kBeforeAnyCall),
          "the first call through a wrapper is due, the clock having no call before it");
    check(!wrapper_call_is_due(t + 5, t, t - 1000),
          "a call 5 us after the last returned is the same frame's again: not due");
    check(!wrapper_call_is_due(t + kWrapperGapUs - 1, t, t - 1000), "just inside the gap: not due");
    check(wrapper_call_is_due(t + kWrapperGapUs, t, t - 1000), "the gap passed: the next frame's");
    // A tight loop of calls (nothing between a wrapper's calls) is never apart by the gap: the
    // backstop runs the work once the last run began kWrapperBackstopUs ago.
    check(!wrapper_call_is_due(t + 1, t, t + 1 - (kWrapperBackstopUs - 1)),
          "just inside the backstop: not due");
    check(wrapper_call_is_due(t + 1, t, t + 1 - kWrapperBackstopUs), "the backstop: due");
    // Every call through a wrapper stamps its return: a third and fourth call follow the
    // second's, however long the real swap waited between the first and the second.
    const int64_t first_returned = t + 16'000;  // the real swap waited for the refresh
    check(!wrapper_call_is_due(first_returned + 3, first_returned, t),
          "the second call of a frame, after a long swap: not due");
    check(!wrapper_call_is_due(first_returned + 6, first_returned + 3, t),
          "the third follows the second's return: not due");
    check(wrapper_call_is_due(first_returned + 16'000, first_returned + 3, t),
          "the next frame's first call, a refresh later: due");
}

int main() {
    test_a_wrapper_call_is_due_after_the_gap_or_the_backstop();
    test_private_folders();
    test_builds();
    test_notes();
    test_replies();
    test_gate();
    test_write_ranges();
    test_keeping_ranges();
    test_reads();
    test_writes();
    test_own_symbols();
    check(socket_name(4242) == "zexion-ap-4242", "the socket's name");
    if (g_failures == 0) std::printf("linux core: all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
