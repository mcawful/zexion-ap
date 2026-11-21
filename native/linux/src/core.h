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
// `why`, when given, says in a few words why it is not (a refusal's own words: "it is a link",
// "it is another user's", "its mode is wider than 0700 and cannot be changed").
bool make_private_folders(const std::string& path, std::string* why = nullptr);

// The abstract socket's name, without its leading NUL: "zexion-ap-<pid>", the pid as the game
// sees itself (in a Flatpak or the Steam Runtime's container, not the host's).
std::string socket_name(long pid);

// The frame work for a call through a wrapper (helper.cpp, game_frame).
// A wrapper may call the helper's swap more than once for one call of its own from the frame
// function (a frame pacer, a duplicating layer), and frame work should run once a game frame, as
// its frame-counted timers (the popups' gap, the file select's, the Steam retry) assume. The
// helper cannot tell the game's frames apart from the wrapper's calls: the game's frame_clock
// (`gGameState+0xB0`, docs/re/linux.md) is recorded, but stands still while a menu pauses the
// game, so it cannot key frame work. So a call through a wrapper runs the work only if
// kWrapperGapUs had passed, when it began, since the last call through a wrapper, of any kind,
// returned from the real swap: every such call stamps its return, so a wrapper's third and fourth
// calls follow the second's return as the second followed the first's, however long the real swap
// waits for the refresh (inferred from the game's measured call rate, not seen). The premise, an
// assumption: the game's work for a frame, from one swap's return to the next call, takes longer
// than kWrapperGapUs. Backstop: a tight loop of calls through a wrapper (a game loop with no
// work between swaps) would never be due, so the work runs anyway once kWrapperBackstopUs have
// passed since the last run began. Limits, both timing: a wrapper that does something slow
// between its repeated calls still has the work run for each; and two real frames closer than
// the gap (`loadState`'s swap, then the frame's) lose the second's work, which waits one frame.
// The direct path has neither.
inline constexpr int64_t kWrapperGapUs = 100;
inline constexpr int64_t kWrapperBackstopUs = 50000;
inline constexpr int64_t kBeforeAnyCall = INT64_MIN / 2;  // a time before any call: no overflow

// Whether a call through a wrapper that began at `start_us` runs frame work: `last_return_us` is
// when the last call through a wrapper returned from the real swap, `last_run_us` when the last
// run through one began (kBeforeAnyCall for none yet). All the same clock, in microseconds.
bool wrapper_call_is_due(int64_t start_us, int64_t last_return_us, int64_t last_run_us);

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
