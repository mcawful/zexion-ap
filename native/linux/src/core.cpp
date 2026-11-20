#include "core.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace zap::linux_helper {

namespace {

struct KnownBuild {
    const char* name;
    const char* build_id;  // GNU build id, hex
};

// Keep in step with KNOWN_LINUX_BUILDS in client/patterns.py; tests/test_cross_checks.py checks it.
constexpr KnownBuild kKnownBuilds[] = {
    {"B1", "4c4416eed9e8a1fa1b373ae3b1badec33510fc90"},
};

constexpr uint32_t kNtGnuBuildId = 3;

uint32_t read_u32(const unsigned char* at) {
    uint32_t value;
    std::memcpy(&value, at, sizeof value);
    return value;  // little-endian, as the x86-64 host
}

size_t align4(size_t n) { return (n + 3) & ~size_t{3}; }

// `text` as a number: decimal, or hex after 0x. Nothing for anything else, or one too big.
std::optional<uint64_t> number(std::string_view text) {
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty() || text.size() > 16) return std::nullopt;
    uint64_t value = 0;
    for (char c : text) {
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            return std::nullopt;
        }
        if (value > (UINT64_MAX - static_cast<uint64_t>(digit)) / static_cast<uint64_t>(base)) {
            return std::nullopt;
        }
        value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
    }
    return value;
}

// `callers`' reply: "callers", then each call site's return address and count, lowest address
// first, then other=N if any came from sites past the table's room.
std::string callers_reply(const State& state) {
    auto sites = state.callers;
    std::sort(sites.begin(), sites.end());
    std::string out = "callers";
    char hex[32];
    for (const auto& [address, count] : sites) {
        std::snprintf(hex, sizeof hex, " 0x%llx=", static_cast<unsigned long long>(address));
        out += hex + std::to_string(count);
    }
    if (state.other_callers != 0) out += " other=" + std::to_string(state.other_callers);
    return out;
}

// `write ADDRESS HEX`'s reply.
std::string write_reply(std::string_view args, const Writer& write) {
    size_t space = args.find(' ');
    if (space == std::string_view::npos) return "error write takes an address and hex bytes";
    auto address = number(args.substr(0, space));
    std::string_view hex = args.substr(space + 1);
    if (!address || hex.empty() || hex.size() % 2 != 0) {
        return "error write takes an address and hex bytes";
    }
    if (hex.size() / 2 > kMaxWrite) {
        return "error write size must be 1 to " + std::to_string(kMaxWrite);
    }
    std::string data;
    for (size_t i = 0; i < hex.size(); i += 2) {
        auto byte = number(std::string("0x") + std::string(hex.substr(i, 2)));
        if (!byte) return "error write takes an address and hex bytes";
        data += static_cast<char>(*byte);
    }
    if (*address > UINT64_MAX - data.size()) return "error write past the end of memory";
    std::string out;
    if (!write(*address, data, out)) return "error " + out;
    return "written " + std::to_string(data.size());
}

// `read ADDRESS SIZE`'s reply.
std::string read_reply(std::string_view args, const Reader& read) {
    size_t space = args.find(' ');
    if (space == std::string_view::npos) return "error read takes an address and a size";
    auto address = number(args.substr(0, space));
    auto size = number(args.substr(space + 1));
    if (!address || !size) return "error read takes an address and a size";
    if (*size == 0 || *size > kMaxRead) {
        return "error read size must be 1 to " + std::to_string(kMaxRead);
    }
    if (*address > UINT64_MAX - *size) return "error read past the end of memory";
    std::string out;
    if (!read(*address, static_cast<size_t>(*size), out)) return "error " + out;
    return kDataPrefix + out;
}

}  // namespace

std::string hex_text(const unsigned char* bytes, size_t size) {
    static const char kHex[] = "0123456789abcdef";
    std::string hex;
    for (size_t i = 0; i < size; ++i) {
        hex += kHex[bytes[i] >> 4];
        hex += kHex[bytes[i] & 0xF];
    }
    return hex;
}

bool is_token_text(std::string_view text) {
    if (text.size() != 2 * kTokenBytes) return false;
    for (char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool same_secret(std::string_view a, std::string_view b) {
    unsigned char difference = a.size() == b.size() ? 0 : 1;
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        difference |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return difference == 0;
}

Gate gate(std::string_view request, std::string_view token, const State& state) {
    Gate out;
    if (request == "hello") {
        out.answer = reply(request, state, Reader(), Writer());
    } else if (request.starts_with(kAuthRequest) &&
               is_token_text(token) &&
               same_secret(request.substr(sizeof kAuthRequest - 1), token)) {
        out.answer = "ok";
        out.authed = true;
    } else if (request.starts_with(kAuthRequest)) {
        out.answer = "error auth refused";
        out.drop = true;
    } else {
        out.answer = "error auth required";
        out.drop = true;
    }
    return out;
}

// client/game.py's writes: its INVENTORY_IN_PLAYER_STATE and SUPPLY_FIELDS (six 16-bit numbers,
// each by its own two bytes) in the player state at gGameState + 0x1CD50, WRITABLE_ITEMS (50 bytes), and the map's
// cells (54 by 54: the item state in 9-byte records at +0x15B61, the grid's bytes at +0x14FFD).
constexpr uint64_t kPlayerState = 0x1CD50;
constexpr uint64_t kSupplyFields[] = {0xCBC, 0xCBE, 0xCC0, 0xCC2, 0xCEA, 0xCEC};
constexpr uint64_t kInventoryAt = 0x108E, kWritableItems = 50;
constexpr uint64_t kCellStates = 0x15B61, kCellStride = 9, kMapGrid = 0x14FFD, kMapCells = 54 * 54;

std::vector<Range> client_write_ranges(uint64_t game_state) {
    std::vector<Range> ranges;
    for (uint64_t field : kSupplyFields) {  // the six 16-bit numbers alone: +0xCC4 to +0xCE9 is
        const uint64_t at = game_state + kPlayerState + field;  // not recorded, and not written
        if (!ranges.empty() && ranges.back().end == at) {
            ranges.back().end = at + 2;  // one run of neighbors
        } else {
            ranges.push_back({at, at + 2});
        }
    }
    ranges.push_back({game_state + kPlayerState + kInventoryAt,
                      game_state + kPlayerState + kInventoryAt + kWritableItems});
    ranges.push_back({game_state + kCellStates, game_state + kCellStates + kMapCells * kCellStride});
    ranges.push_back({game_state + kMapGrid, game_state + kMapGrid + kMapCells});
    return ranges;
}

std::vector<Range> keep_ranges(const std::vector<Range>& wanted, const SymbolSpan& base,
                               const std::function<bool(uint64_t, uint64_t)>& in_data,
                               std::vector<Range>& dropped) {
    std::vector<Range> kept;
    for (const Range& range : wanted) {
        const bool in_symbol = range.start >= base.value && range.end >= range.start &&
                               (base.size == 0 || range.end <= base.value + base.size);
        if (in_symbol && in_data(range.start, range.end - range.start)) {
            kept.push_back(range);
        } else {
            dropped.push_back(range);
        }
    }
    return kept;
}

bool within_ranges(const std::vector<Range>& ranges, uint64_t address, uint64_t size) {
    if (size == 0 || address + size < address) return false;
    for (const Range& range : ranges) {
        if (address >= range.start && address + size <= range.end) return true;
    }
    return false;
}

const char* identify_build(std::string_view build_id_hex) {
    for (const auto& build : kKnownBuilds) {
        if (build_id_hex == build.build_id) return build.name;
    }
    return nullptr;
}

std::optional<std::string> note_build_id(const unsigned char* notes, size_t size) {
    size_t at = 0;
    while (at + 12 <= size) {
        uint32_t namesz = read_u32(notes + at);
        uint32_t descsz = read_u32(notes + at + 4);
        uint32_t type = read_u32(notes + at + 8);
        size_t name_at = at + 12;
        size_t desc_at = name_at + align4(namesz);
        size_t next = desc_at + align4(descsz);
        if (desc_at < name_at || next < desc_at || next > size) return std::nullopt;  // cut short
        if (type == kNtGnuBuildId && namesz == 4 && std::memcmp(notes + name_at, "GNU", 4) == 0) {
            static const char kHex[] = "0123456789abcdef";
            std::string hex;
            for (size_t i = 0; i < descsz; ++i) {
                hex += kHex[notes[desc_at + i] >> 4];
                hex += kHex[notes[desc_at + i] & 0xF];
            }
            return hex;
        }
        at = next;
    }
    return std::nullopt;
}

std::map<std::string, SymbolSpan> find_symbols(std::string_view elf,
                                               const std::vector<std::string>& names) {
    std::map<std::string, SymbolSpan> out;
    auto u16 = [&](size_t at) { uint16_t v; std::memcpy(&v, elf.data() + at, 2); return v; };
    auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, elf.data() + at, 4); return v; };
    auto u64 = [&](size_t at) { uint64_t v; std::memcpy(&v, elf.data() + at, 8); return v; };
    if (elf.size() < 64 || elf.substr(0, 4) != "\x7f" "ELF" || elf[4] != 2 || elf[5] != 1) return out;
    uint64_t shoff = u64(40);
    uint16_t shentsize = u16(58), shnum = u16(60);
    if (shentsize != 64 || shoff > elf.size() || shnum > (elf.size() - shoff) / 64) return out;
    std::map<std::string, std::vector<SymbolSpan>> seen;
    for (const std::string& name : names) seen[name];
    for (uint16_t i = 0; i < shnum; ++i) {
        size_t sh = shoff + 64u * i;
        if (u32(sh + 4) != 2) continue;  // SHT_SYMTAB
        uint64_t offset = u64(sh + 24), size = u64(sh + 32), entsize = u64(sh + 56);
        uint32_t link = u32(sh + 40);
        if (entsize != 24 || link >= shnum || offset > elf.size() || size > elf.size() - offset) {
            continue;
        }
        size_t str = shoff + 64u * link;
        uint64_t str_offset = u64(str + 24), str_size = u64(str + 32);
        if (str_offset > elf.size() || str_size > elf.size() - str_offset) continue;
        std::string_view strings = elf.substr(str_offset, str_size);
        for (uint64_t at = offset; at + 24 <= offset + size; at += 24) {
            uint32_t name_at = u32(at);
            if (u16(at + 6) == 0 || name_at >= strings.size()) continue;  // undefined
            std::string_view name = strings.substr(name_at);
            name = name.substr(0, name.find('\0'));
            auto wanted = seen.find(std::string(name));
            if (wanted != seen.end()) wanted->second.push_back({u64(at + 8), u64(at + 16)});
        }
    }
    for (const auto& [name, spans] : seen) {
        if (spans.empty()) continue;
        bool same = true;
        for (const SymbolSpan& s : spans) same = same && s.value == spans[0].value;
        if (same) out[name] = spans[0];
    }
    return out;
}

std::string socket_name(long pid) { return "zexion-ap-" + std::to_string(pid); }

std::string reply(std::string_view request, const State& state, const Reader& read,
                  const Writer& write) {
    if (request == "hello") {
        return std::string("zexion-ap ") + kVersion + " build=" +
               (state.build ? state.build : "unknown") + " hooked=" + (state.hooked ? "1" : "0") +
               " platform=linux folder=" + (state.folder ? "1" : "0");
    }
    if (request == "ping") return "pong frames=" + std::to_string(state.frames);
    if (request == "callers") return callers_reply(state);
    if (request == "read" || request.starts_with("read ")) {
        return read_reply(request.substr(request.size() > 5 ? 5 : request.size()), read);
    }
    if (request == "write" || request.starts_with("write ")) {
        return write_reply(request.substr(request.size() > 6 ? 6 : request.size()), write);
    }
    return "error unknown request";
}

}  // namespace zap::linux_helper
