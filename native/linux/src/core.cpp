#include "core.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

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

std::string socket_name(long pid) { return "zexion-ap-" + std::to_string(pid); }

std::string reply(std::string_view request, const State& state, const Reader& read) {
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
    return "error unknown request";
}

}  // namespace zap::linux_helper
