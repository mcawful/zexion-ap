#include "findaddr.h"

#include <cstdio>
#include <cstring>
#include <set>

#include "findaddr_patterns.h"

#ifdef _WIN32
#include "build.h"
#include "game_mem.h"
#endif

namespace zap::find {

namespace {

// A pattern as bytes and which of them count (not "??"); `anchor` is the first that does.
struct Compiled {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> fixed;  // 1 where the byte must match
    size_t anchor = 0;
};

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// False for a text that is not tokens of two hex digits or "??" one space apart, or that fixes no
// byte (nothing to search for).
bool compile(const char* text, Compiled& out) {
    out = {};
    const size_t n = std::strlen(text);
    if (n % 3 != 2) return false;  // k tokens: 3k - 1 characters
    bool any = false;
    for (size_t i = 0; i < n; i += 3) {
        if (i + 2 < n && text[i + 2] != ' ') return false;
        if (text[i] == '?' && text[i + 1] == '?') {
            out.bytes.push_back(0);
            out.fixed.push_back(0);
            continue;
        }
        const int high = hex_digit(text[i]), low = hex_digit(text[i + 1]);
        if (high < 0 || low < 0) return false;
        if (!any) out.anchor = out.bytes.size();
        any = true;
        out.bytes.push_back(static_cast<uint8_t>(high * 16 + low));
        out.fixed.push_back(1);
    }
    return any;
}

// How many starts of `code` match `p`, overlapping ones counted, counting no past `limit`; the
// first in `first`. As client/game.py's compile_pattern finds every start: a match lies wholly
// in `code`.
size_t count_matches(std::span<const uint8_t> code, const Compiled& p, size_t limit,
                     size_t& first) {
    const size_t n = p.bytes.size();
    if (n == 0 || code.size() < n) return 0;
    const size_t last_start = code.size() - n;
    const uint8_t anchor_byte = p.bytes[p.anchor];
    size_t count = 0;
    // The anchor byte's position runs from p.anchor to last_start + p.anchor.
    for (size_t from = p.anchor; from <= last_start + p.anchor;) {
        const void* hit = std::memchr(code.data() + from, anchor_byte, last_start + p.anchor + 1 - from);
        if (hit == nullptr) break;
        const size_t at = static_cast<size_t>(static_cast<const uint8_t*>(hit) - code.data());
        const size_t start = at - p.anchor;
        bool match = true;
        for (size_t i = 0; i < n && match; ++i) {
            match = p.fixed[i] == 0 || code[start + i] == p.bytes[i];
        }
        if (match) {
            if (count == 0) first = start;
            if (++count >= limit) break;
        }
        from = at + 1;
    }
    return count;
}

std::string hex(uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "%#llx", static_cast<unsigned long long>(value));
    return text;
}

// The RVA `p` leads to: its only match, as client/game.py's resolve reads it. "" or why not.
std::string resolve_pattern(std::span<const uint8_t> code, uint64_t code_rva, const Pattern& p,
                            uint64_t& rva) {
    Compiled compiled;
    if (!compile(p.text, compiled)) return "is not readable";
    if (p.kind == Kind::kRip &&
        (p.disp_at < 0 || static_cast<size_t>(p.disp_at) + 4 > compiled.bytes.size())) {
        return "is not readable";
    }
    size_t start = 0;
    const size_t matches = count_matches(code, compiled, 2, start);
    if (matches != 1) return "matched " + std::to_string(matches) + " times";
    int64_t address = static_cast<int64_t>(code_rva + start);
    if (p.kind == Kind::kRip) {
        int32_t field = 0;
        std::memcpy(&field, code.data() + start + static_cast<size_t>(p.disp_at), sizeof(field));
        address += p.rip_end + static_cast<int64_t>(field);
    }
    address += p.addend;
    if (address <= 0) return "leads below the image";
    rva = static_cast<uint64_t>(address);
    return "";
}

}  // namespace

std::span<const Target> game_targets() { return kTargets; }

std::string find_target(std::span<const uint8_t> code, uint64_t code_rva, const Target& target,
                        uint64_t& rva) {
    std::set<uint64_t> found;
    for (int i = 0; i < target.count; ++i) {
        uint64_t one = 0;
        std::string why = resolve_pattern(code, code_rva, target.patterns[i], one);
        if (!why.empty()) return "pattern " + std::to_string(i + 1) + " " + why;
        found.insert(one);
    }
    if (found.size() != 1) {
        std::string listed;
        for (uint64_t address : found) listed += (listed.empty() ? "" : ", ") + hex(address);
        return "patterns disagree (" + listed + ")";
    }
    rva = *found.begin();
    return "";
}

Found find_all(std::span<const uint8_t> code, uint64_t code_rva,
               std::span<const Target> targets) {
    Found out;
    for (const Target& target : targets) {
        uint64_t rva = 0;
        std::string why = find_target(code, code_rva, target, rva);
        if (why.empty()) {
            out.rvas[target.name] = rva;
        } else {
            out.failed.push_back(std::string(target.name) + ": " + why);
        }
    }
    return out;
}

std::string differing_keys(const std::map<std::string, uint64_t>& client,
                           const std::map<std::string, uint64_t>& own) {
    std::string keys;
    auto add = [&](const std::string& key) { keys += (keys.empty() ? "" : ",") + key; };
    for (const auto& [key, rva] : client) {
        auto found = own.find(key);
        if (found == own.end() || found->second != rva) add(key);
    }
    for (const auto& entry : own) {
        if (client.find(entry.first) == client.end()) add(entry.first);
    }
    return keys;
}

std::string failure_of(const Found& found, const std::string& refusal) {
    constexpr char kMissing[] = "missing ";
    if (refusal.rfind(kMissing, 0) == 0) {
        const std::string prefix = refusal.substr(sizeof(kMissing) - 1) + ":";
        for (const std::string& failure : found.failed) {
            if (failure.rfind(prefix, 0) == 0) return failure;
        }
    }
    return refusal;
}

std::string failed_names(const Found& found) {
    std::string names;
    for (const std::string& failure : found.failed) {
        names += (names.empty() ? "" : ",") + failure.substr(0, failure.find(':'));
    }
    return names;
}

#ifdef _WIN32
std::string find_in_image(HMODULE image, Found& found) {
    found = {};
    if (identify_build(image) == nullptr) return "not a build the helper knows";
    const IMAGE_NT_HEADERS64* nt = nt_headers(image);
    if (nt == nullptr) return "the game is not an image";
    const IMAGE_SECTION_HEADER* text = nullptr;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::strncmp(reinterpret_cast<const char*>(section->Name), ".text",
                         IMAGE_SIZEOF_SHORT_NAME) == 0) {
            text = section;
            break;
        }
    }
    if (text == nullptr || text->Misc.VirtualSize == 0) return "no .text section";
    // A copy, read fault-safe: the scan itself never touches the game's memory.
    std::vector<uint8_t> code(text->Misc.VirtualSize);
    if (!safe_read(reinterpret_cast<const BYTE*>(image) + text->VirtualAddress, code.data(),
                   code.size())) {
        return ".text could not be read";
    }
    found = find_all(code, text->VirtualAddress, game_targets());
    return "";
}
#endif

}  // namespace zap::find
