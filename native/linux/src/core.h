// The Linux helper's logic that needs no game: which build the host is, the socket's name, and
// the replies to requests (native/README.md, "The Linux helper"). helper.cpp wires it into the
// game; tests/core_tests.cpp tests it alone.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zap::linux_helper {

inline constexpr char kVersion[] = "1.2.0";  // the release's: native/src/dllmain.cpp's kVersion
inline constexpr char kDisabledName[] = "zexion_ap.disabled";
inline constexpr size_t kMaxRequest = 64 * 1024;  // as the Windows pipe's buffer
inline constexpr size_t kMaxRead = 32 * 1024;     // one `read`'s bytes, its reply under kMaxRequest
inline constexpr char kDataPrefix[] = "data ";    // a `read`'s reply: this, then the bytes
inline constexpr size_t kMaxWrite = 16 * 1024;    // one `write`'s bytes (hex: twice that sent)

// The client's token (native/README.md, "The Linux helper", "Who may connect"): kTokenBytes
// random bytes made when the helper starts, written as hex into kTokenFile of the helper's data
// folder (mode 0600, in a folder of 0700), which the client reads and sends as its first request
// but `hello`'s: "auth <hex>". A connection serves nothing until it has.
inline constexpr size_t kTokenBytes = 32;
inline constexpr char kTokenFile[] = "token";
inline constexpr char kAuthRequest[] = "auth ";

// `bytes` as lower-case hex.
std::string hex_text(const unsigned char* bytes, size_t size);

// Whether `text` is a token as written: exactly 2 * kTokenBytes lower-case hex digits.
bool is_token_text(std::string_view text);

// Whether two secrets are equal, comparing every byte of the shorter one whatever the first
// difference (so the time a refusal takes tells nothing of the token).
bool same_secret(std::string_view a, std::string_view b);

// What a connection that has not shown the token may ask, and the helper's answer: `hello` (the
// build and version, so a client can tell an old helper from a wrong token; nothing of the
// game's memory), and `auth <token>`. Anything else, and a token that is not the helper's, is
// refused and the connection dropped (`drop`); a token that is ends `authed`.
struct Gate {
    std::string answer;
    bool authed = false;
    bool drop = false;
};
struct State;
Gate gate(std::string_view request, std::string_view token, const State& state);

// The build a GNU build id (hex) is, or nullptr for one not known.
const char* identify_build(std::string_view build_id_hex);

// The GNU build id in a PT_NOTE segment's bytes, as hex, or nothing.
std::optional<std::string> note_build_id(const unsigned char* notes, size_t size);

// A defined symbol of an ELF file's .symtab: its address (an ET_EXEC's load address) and size.
struct SymbolSpan {
    uint64_t value = 0;
    uint64_t size = 0;
};

// `names`' symbols in the 64-bit little-endian ELF file `elf` (the game's own binary, read from
// /proc/self/exe): each found once, defined, with one value; a name missing or found with two
// different values is left out.
std::map<std::string, SymbolSpan> find_symbols(std::string_view elf,
                                               const std::vector<std::string>& names);

// The abstract socket's name, without its leading NUL: "zexion-ap-<pid>", the pid as the game
// sees itself (in a Flatpak or the Steam Runtime's container, not the host's).
std::string socket_name(long pid);

struct State {
    const char* build = nullptr;  // identify_build's, nullptr when unknown
    bool hooked = false;          // a known build: it counts the frame calls and serves
    bool folder = false;          // loaded from the game's own folder
    uint64_t frames = 0;          // frame calls since it loaded
    // The frame calls by call site: each return address with its count, and the calls from
    // sites past the table's room.
    std::vector<std::pair<uint64_t, uint64_t>> callers;
    uint64_t other_callers = 0;
};

// Copies `size` bytes of this process's memory at `address` into `out`; false (with why in
// `out`) when they can't all be read. helper.cpp's reads its own memory without faulting.
using Reader = std::function<bool(uint64_t address, size_t size, std::string& out)>;

// Writes `data` into this process's memory at `address`; false (with why in `out`) when it
// isn't all written. helper.cpp's writes only the game binary's own writable data.
using Writer = std::function<bool(uint64_t address, const std::string& data, std::string& out)>;

// The reply to one request.
std::string reply(std::string_view request, const State& state, const Reader& read,
                  const Writer& write);

}  // namespace zap::linux_helper
