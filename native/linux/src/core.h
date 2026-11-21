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

// Where `write` may change the game when a client does: what client/game.py writes and no more
// (its inventory bytes, its six supply numbers each by its own two bytes, a map cell's item
// state, the map grid), as
// offsets from gGameState (docs/re/linux.md, "Lookup: targets"). Not the sprite records around
// them, whose fields hold procedures the game calls (the player's is at gGameState + 0x1CD50).
// tests/test_linux_helper.py checks these against client/game.py's own.
struct Range {
    uint64_t start = 0;
    uint64_t end = 0;  // one past
};
std::vector<Range> client_write_ranges(uint64_t game_state);

// Whether [address, address + size) lies wholly in one of `ranges`.
bool within_ranges(const std::vector<Range>& ranges, uint64_t address, uint64_t size);

// The build a GNU build id (hex) is, or nullptr for one not known.
const char* identify_build(std::string_view build_id_hex);

// The GNU build id in a PT_NOTE segment's bytes, as hex, or nothing.
std::optional<std::string> note_build_id(const unsigned char* notes, size_t size);

// A defined symbol of an ELF file's .symtab: its address (an ET_EXEC's load address) and size.
struct SymbolSpan {
    uint64_t value = 0;
    uint64_t size = 0;
};

// The ranges of `wanted` to keep: each wholly inside the symbol `base` (its extent, when it has
// one) and where `in_data` says the game's writable data is. The others are put in `dropped`,
// for the helper to say so: a range the client needs that is dropped makes every write there
// refused. helper.cpp's set_client_writes calls this with the game's own symbol and segments.
std::vector<Range> keep_ranges(const std::vector<Range>& wanted, const SymbolSpan& base,
                               const std::function<bool(uint64_t, uint64_t)>& in_data,
                               std::vector<Range>& dropped);

// `names`' symbols in the 64-bit little-endian ELF file `elf` (the game's own binary, read from
// /proc/self/exe): each found once, defined, with one value; a name missing or found with two
// different values is left out.
std::map<std::string, SymbolSpan> find_symbols(std::string_view elf,
                                               const std::vector<std::string>& names);

// A folder and the ones above it made if they are not there yet, each only for the user (mode
// 0700); false if it is not there after, or if the folder itself (the token's, which no one else
// may read) is a link, is another user's, or has a mode wider than the user's alone and cannot be
// made so (chmod 0700, as the ones this makes). The ones above it may be links (a dotfiles
// manager's ~/.local/share); the folder itself is checked with lstat, never through a link.
bool make_private_folders(const std::string& path);

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
