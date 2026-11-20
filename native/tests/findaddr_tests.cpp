// Host tests for the helper's own address scan (findaddr.h): the matching rules on synthetic code
// (exactly once, overlapping matches, wildcards, both kinds, the addend), a missing target, the
// generated table, an unknown build, and, where ZEXION_EXE names a copy of the game's exe, the
// real patterns on its .text (never in CI: no game files there).

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "findaddr.h"

namespace {

using zap::find::Kind;
using zap::find::Pattern;
using zap::find::Target;

constexpr uint64_t kCodeRva = 0x1000;

// 512 bytes of 0xCC, with `bytes` put at `at`.
struct Code {
    std::vector<uint8_t> data = std::vector<uint8_t>(512, 0xCC);
    Code& put(size_t at, std::initializer_list<uint8_t> bytes) {
        std::copy(bytes.begin(), bytes.end(), data.begin() + static_cast<ptrdiff_t>(at));
        return *this;
    }
    Code& field(size_t at, int32_t value) {
        std::memcpy(data.data() + at, &value, sizeof(value));
        return *this;
    }
};

std::string find(const Code& code, const Target& target, uint64_t& rva) {
    return zap::find::find_target(code.data, kCodeRva, target, rva);
}

void test_a_function_is_where_its_pattern_matches() {
    Code code;
    code.put(0x40, {0x48, 0x89, 0x5C, 0x24, 0x08});
    const Pattern at[] = {{"48 89 5C 24 08", 0, Kind::kAt, 0, 0}};
    uint64_t rva = 0;
    check(find(code, {"fn", at, 1}, rva).empty() && rva == kCodeRva + 0x40, "kAt: its own bytes");
    // The match may start before the target (an instruction found by the code just before it).
    const Pattern before[] = {{"48 89 5C 24 08", 0, Kind::kAt, 0, 3}};
    check(find(code, {"fn", before, 1}, rva).empty() && rva == kCodeRva + 0x43, "kAt: addend");
    const Pattern back[] = {{"89 5C 24", 0, Kind::kAt, 0, -1}};
    check(find(code, {"fn", back, 1}, rva).empty() && rva == kCodeRva + 0x40, "a negative addend");
}

void test_a_rip_pattern_follows_its_field() {
    Code code;
    // lea rax, [rip + 0x100] at 0x20: 48 8D 05 <field>, 7 bytes, then the rest of the pattern.
    code.put(0x20, {0x48, 0x8D, 0x05}).field(0x23, 0x100).put(0x27, {0x41, 0xFF, 0xC7});
    const Pattern rip[] = {{"48 8D 05 ?? ?? ?? ?? 41 FF C7", 3, Kind::kRip, 7, 0}};
    uint64_t rva = 0;
    check(find(code, {"data", rip, 1}, rva).empty() && rva == kCodeRva + 0x20 + 7 + 0x100,
          "kRip: the instruction's end plus the field");
    const Pattern plus[] = {{"48 8D 05 ?? ?? ?? ?? 41 FF C7", 3, Kind::kRip, 7, -0x10}};
    check(find(code, {"data", plus, 1}, rva).empty() && rva == kCodeRva + 0x20 + 7 + 0x100 - 0x10,
          "kRip: and the addend");
    // A negative field reaches back.
    code.field(0x23, -0x20);
    check(find(code, {"data", rip, 1}, rva).empty() && rva == kCodeRva + 0x20 + 7 - 0x20,
          "kRip: a negative field");
    // One that leads below the image is no address.
    code.field(0x23, -0x2000);
    check(find(code, {"data", rip, 1}, rva) == "pattern 1 leads below the image",
          "an address at or below 0 is refused");
}

void test_a_pattern_must_match_exactly_once() {
    Code code;
    code.put(0x10, {0xAA, 0xBB, 0xCC, 0xDD}).put(0x90, {0xAA, 0xBB, 0xCC, 0xDD});
    uint64_t rva = 0;
    const Pattern twice[] = {{"AA BB CC DD", 0, Kind::kAt, 0, 0}};
    check(find(code, {"t", twice, 1}, rva) == "pattern 1 matched 2 times", "twice is refused");
    const Pattern none[] = {{"AA BB CC EE", 0, Kind::kAt, 0, 0}};
    check(find(code, {"t", none, 1}, rva) == "pattern 1 matched 0 times", "none is refused");
    // A wildcard stands for any byte, so it can make a match twice.
    const Pattern wild[] = {{"AA ?? CC DD", 0, Kind::kAt, 0, 0}};
    check(find(code, {"t", wild, 1}, rva) == "pattern 1 matched 2 times", "a wildcard matches");
    // Overlapping matches count, as the client's lookahead counts them: 3 starts in 5 bytes.
    Code runs;
    runs.put(0x30, {0x11, 0x11, 0x11, 0x11, 0x11});
    const Pattern overlap[] = {{"11 11 11", 0, Kind::kAt, 0, 0}};
    check(find(runs, {"t", overlap, 1}, rva) == "pattern 1 matched 2 times",
          "overlapping matches count");
    // The second of two patterns is named by its place.
    const Pattern second[] = {{"AA BB CC EE", 0, Kind::kAt, 0, 0},
                              {"AA BB CC DD", 0, Kind::kAt, 0, 0}};
    check(find(code, {"t", second, 2}, rva) == "pattern 1 matched 0 times", "the first one fails");
}

void test_the_ends_of_the_code_count() {
    Code code;
    code.put(0, {0x01, 0x02, 0x03}).put(509, {0x04, 0x05, 0x06});
    uint64_t rva = 0;
    const Pattern first[] = {{"01 02 03", 0, Kind::kAt, 0, 0}};
    check(find(code, {"t", first, 1}, rva).empty() && rva == kCodeRva, "a match at the start");
    const Pattern last[] = {{"04 05 06", 0, Kind::kAt, 0, 0}};
    check(find(code, {"t", last, 1}, rva).empty() && rva == kCodeRva + 509,
          "a match at the very end");
    const Pattern past[] = {{"05 06 07", 0, Kind::kAt, 0, 0}};
    check(find(code, {"t", past, 1}, rva) == "pattern 1 matched 0 times",
          "one that would run past the end is no match");
    const Pattern lead[] = {{"?? ?? 01 02 03", 0, Kind::kAt, 0, 0}};
    check(find(code, {"t", lead, 1}, rva) == "pattern 1 matched 0 times",
          "nor one that would start before it");
    const Pattern wild[] = {{"?? 02 03", 0, Kind::kAt, 0, 0}};
    check(find(code, {"t", wild, 1}, rva).empty() && rva == kCodeRva,
          "a leading wildcard anchors on the first fixed byte");
    uint64_t unused = 0;
    check(zap::find::find_target(std::vector<uint8_t>(2, 0), kCodeRva, {"t", first, 1}, unused) ==
              "pattern 1 matched 0 times",
          "code shorter than the pattern");
}

void test_every_pattern_of_a_target_must_lead_to_one_address() {
    Code code;
    code.put(0x20, {0x48, 0x8D, 0x05}).field(0x23, 0x100).put(0x27, {0x41, 0xFF, 0xC7});
    code.put(0x80, {0xDE, 0xAD, 0xBE, 0xEF});
    uint64_t target = kCodeRva + 0x20 + 7 + 0x100;
    // Two patterns, one to the lea's target and one to the bytes at 0x80: they disagree.
    const Pattern two[] = {{"48 8D 05 ?? ?? ?? ?? 41 FF C7", 3, Kind::kRip, 7, 0},
                           {"DE AD BE EF", 0, Kind::kAt, 0, 0}};
    uint64_t rva = 0;
    char expect[96];
    std::snprintf(expect, sizeof(expect), "patterns disagree (%#llx, %#llx)",
                  static_cast<unsigned long long>(kCodeRva + 0x80),
                  static_cast<unsigned long long>(target));
    check(find(code, {"t", two, 2}, rva) == expect, "they disagree", find(code, {"t", two, 2}, rva));
    // Moved so they agree (the second's addend makes up the difference).
    const Pattern agree[] = {{"48 8D 05 ?? ?? ?? ?? 41 FF C7", 3, Kind::kRip, 7, 0},
                             {"DE AD BE EF", 0, Kind::kAt, 0, static_cast<int64_t>(target - kCodeRva - 0x80)}};
    check(find(code, {"t", agree, 2}, rva).empty() && rva == target, "they agree");
}

void test_a_pattern_the_helper_cannot_read_is_refused() {
    Code code;
    uint64_t rva = 0;
    for (const char* text : {"", "4", "48 8", "48  89", "4G 89", "48-89", "?? ??", "48 89 "}) {
        const Pattern bad[] = {{text, 0, Kind::kAt, 0, 0}};
        check(find(code, {"t", bad, 1}, rva) == "pattern 1 is not readable", "a bad text", text);
    }
    // A rip field the pattern does not hold.
    const Pattern field[] = {{"48 8D 05 AA", 1, Kind::kRip, 5, 0}};
    check(find(code, {"t", field, 1}, rva) == "pattern 1 is not readable", "a field past the end");
    const Pattern negative[] = {{"48 8D 05 AA BB CC DD", -1, Kind::kRip, 5, 0}};
    check(find(code, {"t", negative, 1}, rva) == "pattern 1 is not readable", "a field before it");
}

void test_find_all_keeps_what_it_found_and_names_what_it_did_not() {
    Code code;
    code.put(0x10, {0xAA, 0xBB, 0xCC, 0xDD});
    const Pattern ok[] = {{"AA BB CC DD", 0, Kind::kAt, 0, 0}};
    const Pattern missing[] = {{"AA BB CC EE", 0, Kind::kAt, 0, 0}};
    const Pattern again[] = {{"AA BB CC DD", 0, Kind::kAt, 0, 8}};
    const Target targets[] = {{"one", ok, 1}, {"gone", missing, 1}, {"two", again, 1}};
    zap::find::Found found = zap::find::find_all(code.data, kCodeRva, targets);
    check(found.rvas.size() == 2 && found.rvas["one"] == kCodeRva + 0x10 &&
              found.rvas["two"] == kCodeRva + 0x18,
          "the targets found");
    check(found.failed.size() == 1 && found.failed[0] == "gone: pattern 1 matched 0 times",
          "the one not found, by name and why");
}

void test_the_generated_table_is_readable_and_has_every_name_once() {
    std::span<const Target> targets = zap::find::game_targets();
    check(targets.size() > 50, "a table of the game's targets");
    std::set<std::string> names;
    int bad = 0;
    for (const Target& target : targets) {
        check(names.insert(target.name).second, "a name once", target.name);
        check(target.count >= 2, "two patterns or more", target.name);
        // On code with none of it, each pattern on its own (find_target stops at a target's
        // first failure): it reads, so it is refused by "matched 0 times", not as unreadable.
        for (int i = 0; i < target.count; ++i) {
            const Target one{target.name, target.patterns + i, 1};
            uint64_t rva = 0;
            std::string why =
                zap::find::find_target(std::vector<uint8_t>(64, 0xCC), kCodeRva, one, rva);
            if (why != "pattern 1 matched 0 times") ++bad;
        }
    }
    check(bad == 0, "every pattern in the table is readable", std::to_string(bad));
    // The helper's own table has the targets that make its title lines and map markers run.
    for (const char* name : {"inventory", "save_loaded", "menu_state", "map_grid", "flags",
                             "text_new_fn", "text_cut_code", "seen_marker_code", "ending_unlocks"}) {
        check(names.count(name) == 1, "a target the helper needs", name);
    }
    check(names.count("cell_state") == 0 && names.count("pickup_counter") == 0,
          "the client's own targets are not in the helper's table");
}

void test_this_program_is_not_a_known_build_so_nothing_is_scanned() {
    zap::find::Found found;
    found.rvas["stale"] = 1;
    std::string why = zap::find::find_in_image(GetModuleHandleW(nullptr), found);
    check(why == "not a build the helper knows", "an unknown build is not scanned", why);
    check(found.rvas.empty() && found.failed.empty(), "and nothing is held from it");
}

void test_a_clients_addresses_are_compared_with_the_scans() {
    const std::map<std::string, uint64_t> own = {{"a", 1}, {"b", 2}, {"c", 3}};
    check(zap::find::differing_keys({{"a", 1}, {"b", 2}, {"c", 3}}, own).empty(), "agreeing");
    check(zap::find::differing_keys({{"a", 1}, {"b", 9}, {"c", 3}}, own) == "b", "one differs");
    check(zap::find::differing_keys({{"a", 1}, {"b", 2}}, own) == "c", "one the client lacks");
    check(zap::find::differing_keys({{"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}}, own) == "d",
          "one the scan lacks");
    check(zap::find::differing_keys({{"a", 5}, {"d", 4}}, own) == "a,d,b,c",
          "the client's first, in order, then the scan's: each once");
    check(zap::find::differing_keys({}, {}).empty(), "nothing to compare");
}

void test_what_a_scan_lacked_is_named() {
    zap::find::Found found;
    found.failed = {"map_grid: pattern 2 matched 0 times", "flags: patterns disagree (0x1, 0x2)"};
    check(zap::find::failed_names(found) == "map_grid,flags", "the names");
    check(zap::find::failed_names({}).empty(), "none");
    check(zap::find::failure_of(found, "missing flags") == found.failed[1],
          "a refusal for a missing key is the scan's failure of it");
    check(zap::find::failure_of(found, "missing fade") == "missing fade",
          "one the scan has no failure for stays as it is");
    check(zap::find::failure_of(found, "inventory is not in code") == "inventory is not in code",
          "any other refusal stays as it is");
    check(zap::find::failure_of(found, "missing map") == "missing map",
          "a name is matched whole, not by its start");
}

std::string env(const char* name) {
    char value[MAX_PATH * 4];
    DWORD n = GetEnvironmentVariableA(name, value, sizeof(value));
    return n > 0 && n < sizeof(value) ? std::string(value, n) : std::string();
}

// find_in_image's known-build branch, as it runs in the game: the exe copy mapped as an image
// (its sections at their RVAs, as a loaded module's), whose headers identify_build and the section
// walk read, and whose .text safe_read copies. `file_found` is what find_all found on the file's
// bytes: the same addresses must come out.
void test_the_exe_mapped_as_an_image(const std::string& path, const zap::find::Found& file_found) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    check(file != INVALID_HANDLE_VALUE, "the exe opens for mapping");
    if (file == INVALID_HANDLE_VALUE) return;
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY | SEC_IMAGE, 0, 0, nullptr);
    void* view = mapping != nullptr ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : nullptr;
    check(view != nullptr, "the exe maps as an image");
    if (view != nullptr) {
        zap::find::Found found;
        const auto begin = std::chrono::steady_clock::now();
        std::string why = zap::find::find_in_image(static_cast<HMODULE>(view), found);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - begin)
                            .count();
        std::printf("findaddr tests: find_in_image on the mapped exe: %zu found, in %lld ms\n",
                    found.rvas.size(), static_cast<long long>(ms));
        check(why.empty(), "a known build mapped as an image is scanned", why);
        check(found.failed.empty() && found.rvas == file_found.rvas,
              "the same addresses as the file's bytes give");
        UnmapViewOfFile(view);
    }
    if (mapping != nullptr) CloseHandle(mapping);
    CloseHandle(file);
}

// The real exe, if ZEXION_EXE names a copy of it: the table scanned on its .text, as the helper
// scans it in the game. Prints `key=0xRVA` lines to ZEXION_SCAN_OUT if that is set, for
// tests/test_native_scan.py, which compares them with the client's own scan.
void test_the_real_exe_if_there_is_one() {
    const std::string path = env("ZEXION_EXE");
    if (path.empty()) {
        std::printf("findaddr tests: ZEXION_EXE not set, the real-exe check skipped\n");
        return;
    }
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> exe((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    check(exe.size() > 0x1000, "the exe is read", path);
    if (exe.size() <= 0x1000) return;
    uint32_t nt = 0;
    std::memcpy(&nt, exe.data() + 0x3C, sizeof(nt));
    const auto* header = reinterpret_cast<const IMAGE_NT_HEADERS64*>(exe.data() + nt);
    const auto* section = IMAGE_FIRST_SECTION(header);
    std::vector<uint8_t> code;
    uint64_t text_rva = 0;
    for (WORD i = 0; i < header->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::strncmp(reinterpret_cast<const char*>(section->Name), ".text", 8) != 0) continue;
        text_rva = section->VirtualAddress;
        code.assign(section->Misc.VirtualSize, 0);  // the loaded section: zeros past the file's
        const size_t raw = std::min<size_t>(section->Misc.VirtualSize, section->SizeOfRawData);
        std::memcpy(code.data(), exe.data() + section->PointerToRawData, raw);
    }
    check(!code.empty(), "a .text section");
    const auto begin = std::chrono::steady_clock::now();
    zap::find::Found found = zap::find::find_all(code, text_rva, zap::find::game_targets());
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - begin)
                        .count();
    std::printf("findaddr tests: the real exe: %zu found, %zu not, in %lld ms\n", found.rvas.size(),
                found.failed.size(), static_cast<long long>(ms));
    for (const std::string& failure : found.failed) std::printf("  not found: %s\n", failure.c_str());
    check(found.failed.empty(), "every target is found in the real exe");
    test_the_exe_mapped_as_an_image(path, found);
    if (const std::string out = env("ZEXION_SCAN_OUT"); !out.empty()) {
        std::ofstream lines(out, std::ios::trunc);
        for (const auto& [key, rva] : found.rvas) lines << key << "=" << std::hex << rva << "\n";
    }
}

}  // namespace

int main() {
    test_a_function_is_where_its_pattern_matches();
    test_a_rip_pattern_follows_its_field();
    test_a_pattern_must_match_exactly_once();
    test_the_ends_of_the_code_count();
    test_every_pattern_of_a_target_must_lead_to_one_address();
    test_a_pattern_the_helper_cannot_read_is_refused();
    test_find_all_keeps_what_it_found_and_names_what_it_did_not();
    test_the_generated_table_is_readable_and_has_every_name_once();
    test_this_program_is_not_a_known_build_so_nothing_is_scanned();
    test_a_clients_addresses_are_compared_with_the_scans();
    test_what_a_scan_lacked_is_named();
    test_the_real_exe_if_there_is_one();
    return report("findaddr tests");
}
