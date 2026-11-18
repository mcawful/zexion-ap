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
