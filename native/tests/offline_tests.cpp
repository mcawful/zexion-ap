// Host tests for AP saves without the client (offline.h): the seed file, its match and disk
// count, the folder read, and a stand-in game dressing a save of the seed and undressing it.

#include <windows.h>

#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "check.h"
#include "game_calls.h"
#include "offline.h"
#include "offline_seeds.h"
#include "stand_in.h"
#include "stand_in_menu.h"
#include "fake_gl.h"

using namespace stand_in;

namespace {

// A cached config: `first` (the `config` request), keys k0 to k7, and a check line for each.
std::string cached_config(const std::string& first) {
    std::string text = first;
    for (int i = 0; i < 8; ++i) text += " k" + std::to_string(i) + "=0x" + std::to_string(i + 1);
    text += "\n";
    for (int i = 0; i < 8; ++i) {
        text += "check k" + std::to_string(i) + " " + std::string(32, '0') + "\n";
    }
    return text;
}

// A folder of the tests' own under %TEMP%, as a failed earlier run may have left it: removed.
std::wstring temp_folder(const wchar_t* name) {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(folder);
    return folder.wstring();
}

void test_the_file() {
    for (int i = 0; i < 154; ++i) g_table[i] = static_cast<uint8_t>(i % 47);
    zap::OfflineSeed s;
    check(zap::parse_offline_seed(seed_text(), s).empty(), "a seed file parses");
    check(s.intel && s.lock_assists && s.discs && s.count == 3 && s.required == 12 &&
              s.scan_line == "COLLECT" && s.unfound.size() == 2 && s.unfound[0] == 1312,
          "its fields");
    check(s.labels.rfind("ap_labels\nlabel 23 14 ", 0) == 0, "the labels as a request");
    check(s.locations == 154 && zap::offline_matches(s, seed_of(g_table, g_start)),
          "matches its own table and start");
    uint8_t other[154];
    std::memcpy(other, g_table, sizeof(other));
    other[5] ^= 1;
    check(!zap::offline_matches(s, seed_of(other, g_start)), "another table does not");
    uint8_t moved[6] = {6, 0, 191, 0, 0x4A, 1};
    check(!zap::offline_matches(s, seed_of(g_table, moved)), "another start does not");

    // Secret Shuffle's 155 locations: a table of 155, matched only by a section of 155.
    uint8_t secret[155];
    std::memcpy(secret, g_table, sizeof(g_table));
    secret[154] = 0;
    std::string text = seed_text();
    text.replace(text.find("table ") + 6, 2 * 154, hex(secret, sizeof(secret)));
    zap::OfflineSeed shuffled;
    check(zap::parse_offline_seed(text, shuffled).empty() && shuffled.locations == 155,
          "a table of 155 parses");
    check(zap::offline_matches(shuffled, seed_of(secret, g_start, 155)), "matches its own");
    check(!zap::offline_matches(shuffled, s) && !zap::offline_matches(s, shuffled),
          "a seed of 154 does not match one of 155, the 155th entry 0 or not");
    text.replace(text.find("table ") + 6, 1, "");
    zap::OfflineSeed short_table;
    check(!zap::parse_offline_seed(text, short_table).empty(), "a table of 154.5: refused");
    std::string longer = seed_text();
    longer.insert(longer.find("table ") + 6, "0000");
    check(!zap::parse_offline_seed(longer, short_table).empty(), "a table of 156: refused");
    // A table of 155, then one of 154: refused, lest the 154 keep the first's 155th entry.
    const size_t first = seed_text().find("table ");
    std::string twice = seed_text();
    twice.insert(first, "table " + hex(secret, sizeof(secret)) + "\n");
    check(!zap::parse_offline_seed(twice, short_table).empty(), "a second table line: refused");

    uint8_t grid[54 * 54] = {};
    check(zap::offline_disk_count(s, grid) == 3, "the count as the client left it");
    grid[1312] = 0x1B;
    check(zap::offline_disk_count(s, grid) == 4, "one more for a disk found offline");
    grid[100] = 0x1B;
    check(zap::offline_disk_count(s, grid) == 4, "other cells do not count");

    zap::OfflineSeed none;
    check(zap::parse_offline_seed(seed_text("discs -", "unfound -"), none).empty() && !none.discs &&
              none.unfound.empty(),
          "no disks");
    check(!zap::parse_offline_seed("hello\n", none).empty(), "not a seed file");
    check(!zap::parse_offline_seed("zexion-ap offline 1\nintel 1\n", none).empty(),
          "no table or start");
    check(zap::parse_offline_seed(seed_text("discs 3 12 41"), none).empty(),
          "a one-letter scanner line is fine");
    check(!zap::parse_offline_seed(seed_text("discs 3 0 41"), none).empty(), "none required");
    check(!zap::parse_offline_seed(seed_text("discs 3 12 4"), none).empty(), "odd hex");
    check(!zap::parse_offline_seed(seed_text("discs 3 12 41", "unfound 99999"), none).empty(),
          "a cell off the map");

    // This player's own traps: a cell and a trap's item each.
    check(s.traps.empty(), "a file from before the traps lists none");
    zap::OfflineSeed traps;
    check(zap::parse_offline_seed(seed_text("discs -", "unfound -\ntraps 1312:102,40:103"), traps)
                  .empty() &&
              traps.traps.size() == 2 && traps.traps[0].cell == 1312 &&
              traps.traps[0].item == 102 && traps.traps[1].cell == 40 &&
              traps.traps[1].item == 103,
          "the own traps: each one's cell and item");
    check(zap::parse_offline_seed(seed_text("discs -", "unfound -\ntraps -"), traps).empty() &&
              traps.traps.empty(),
          "none");
    for (const char* bad : {"traps", "traps 1312", "traps 1312:44", "traps 2916:102",
                            "traps 1312:102,", "traps 1312:102,1312:103", "traps x:102",
                            "traps 1312:102:103"}) {
        check(!zap::parse_offline_seed(seed_text("discs -", std::string("unfound -\n") + bad),
                                       traps)
                   .empty(),
              bad);
    }
}

void test_the_folder() {
    const std::wstring folder = temp_folder(L"zap_offline_test");
    std::filesystem::create_directories(folder + L"\\seeds");
    {
        std::ofstream(folder + L"\\config-B1.txt")
            << cached_config("config inventory=0x10 flags=0x20");
        std::ofstream(folder + L"\\seeds\\good.txt") << seed_text();
        std::ofstream(folder + L"\\seeds\\bad.txt") << "zexion-ap offline 1\ntable 00\n";
        std::ofstream(folder + L"\\seeds\\ignored.dat") << seed_text();
    }
    zap::OfflineFiles f = zap::read_offline_files(folder, "B1");
    check(f.config.size() == 10 && f.config["flags"] == 0x20 && f.checks.size() == 8, "the build's config");
    check(f.seeds.size() == 1 && f.seeds[0].name == "good.txt", "the good seed, by its name");
    check(f.error.find("bad.txt") != std::string::npos, "the bad one skipped, and said why");
    zap::OfflineFiles other = zap::read_offline_files(folder, "B2");
    check(other.config.empty() && other.seeds.size() == 1, "no config for another build");
    check(zap::read_offline_files(folder + L"\\none", "B1").seeds.empty(), "no folder: nothing");
    std::filesystem::remove_all(folder);
}

void test_the_helper_writes_what_the_client_sends() {
    const std::wstring folder = temp_folder(L"zap_offline_put");
    const std::string seed_put = "offline_put seed AP_1-0-1\n" + seed_text();
    zap::OfflineSeed put;
    check(zap::put_offline_file(folder, "B1", seed_put, &put) == "stored", "a seed stored");
    check(put.name == "AP_1-0-1" && zap::offline_matches(put, seed_of(g_table, g_start)),
          "and handed back, named (for the AP-save test)");
    zap::OfflineSeed again;
    check(zap::put_offline_file(folder, "B1", seed_put, &again) == "unchanged" &&
              again.name == "AP_1-0-1",
          "the same again: unchanged, and handed back too");
    zap::OfflineSeed none;
    check(zap::put_offline_file(folder, "B1", "offline_put config\n" + cached_config("config"),
                                &none) == "stored" &&
              none.name.empty(),
          "the config stored, no seed handed back");
    // A folder that cannot be written: refused, but the seed is the running helper's.
    zap::OfflineSeed unwritten;
    const std::wstring nowhere = folder + L"\\seeds\\AP_1-0-1.txt\\under-a-file";
    check(zap::put_offline_file(nowhere, "B1", seed_put, &unwritten).rfind("error", 0) == 0 &&
              unwritten.name == "AP_1-0-1" && unwritten.count == 3,
          "a seed whose file cannot be written is still handed over");
    // The same with no folder at all (LOCALAPPDATA unusable), and for a file too large to
    // keep: said, and the seed still handed over.
    zap::OfflineSeed homeless;
    check(zap::put_offline_file(L"", "B1", seed_put, &homeless) == "error no LOCALAPPDATA" &&
              homeless.name == "AP_1-0-1" && homeless.count == 3,
          "a seed with nowhere to be written is still handed over");
    zap::OfflineSeed large;
    const std::string padded = seed_put + std::string(49 * 1024, '\n');
    check(zap::put_offline_file(folder, "B1", padded, &large) == "error too large" &&
              large.name == "AP_1-0-1" && large.count == 3,
          "a seed too large to keep is still handed over");
    check(zap::put_offline_file(L"", "B1", "offline_put config\n" + cached_config("config"),
                                &none) == "error no LOCALAPPDATA" &&
              none.name.empty(),
          "a config with nowhere to be written: refused");
    check(zap::put_offline_file(folder, "B1", "offline_put seed AP_2-0-1\nnot a seed\n", &none)
                  .rfind("error", 0) == 0 &&
              none.name.empty(),
          "a seed that does not parse: none handed back");
    struct Refused {
        const char* build;
        std::string request;
        const char* what;
    };
    const Refused refused[] = {
        {"B1", "offline_put config\nconfig flags=0x20\n", "a config with too few checks refused"},
        {"B1", "offline_put seed COM1\n" + seed_text(),
         "a name not as the client makes it refused"},
        {"B1", "offline_put seed ../x\n" + seed_text(), "a name with a path refused"},
        {"B1", "offline_put seed y\nhello\n", "a seed that does not parse refused"},
        {nullptr, "offline_put config\nconfig a=0x1\n", "no build: no config"},
    };
    for (const Refused& r : refused) {
        check(zap::put_offline_file(folder, r.build, r.request).rfind("error", 0) == 0, r.what);
    }
    zap::OfflineFiles f = zap::read_offline_files(folder, "B1");
    check(f.seeds.size() == 1 && f.seeds[0].name == "AP_1-0-1.txt" && f.config.size() == 8 &&
              f.checks.size() == 8,
          "and read back");
    std::filesystem::remove_all(folder);
}

void test_the_cache_must_fit_the_game() {
    HMODULE self = GetModuleHandleW(nullptr);
    uint64_t entry = zap::nt_headers(self)->OptionalHeader.AddressOfEntryPoint;
    zap::OfflineFiles f;
    for (int i = 0; i < 8; ++i) {
        std::string key = "fn" + std::to_string(i);
        f.config[key] = entry + i;
        f.checks[key] = hex(reinterpret_cast<const BYTE*>(self) + entry + i, 16);
    }
    check(zap::check_offline_cache(self, f).empty(), "the bytes as cached: used");
    f.checks["fn3"][0] = f.checks["fn3"][0] == '0' ? '1' : '0';
    check(!zap::check_offline_cache(self, f).empty(), "one function differs: refused");
    f.checks.erase("fn3");
    check(!zap::check_offline_cache(self, f).empty(), "too few checked: refused");
    zap::OfflineFiles none;
    check(!zap::check_offline_cache(self, none).empty(), "no cache: refused");
}

}  // namespace

// ---- Seed files in the save folder (offline.h, "Seed files in the save folder") ----

// A seed file as the client writes it (check.h's client_text), of this file's table and start.
std::string client_text(int count, bool free_assists, int labels, const std::string& traps = "-") {
    return ::client_text(std::string(reinterpret_cast<const char*>(g_table), sizeof(g_table)),
                         std::string(reinterpret_cast<const char*>(g_start), sizeof(g_start)),
                         count, free_assists, labels, traps);
}

std::string read_all(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_all(const std::wstring& path, const std::string& text) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// The names in `folder`.
std::set<std::wstring> names_in(const std::wstring& folder) {
    std::set<std::wstring> names;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        names.insert(entry.path().filename().wstring());
    }
    return names;
}

const std::wstring kCopy = L"ZexionAP-seed-AP_1-0-1.sav";

void test_the_seed_text_round_trips() {
    for (int i = 0; i < 154; ++i) g_table[i] = static_cast<uint8_t>(i % 47);
    for (const std::string& text : {client_text(3, false, 2), client_text(0, true, 0),
                                    client_text(9, true, 1, "1312:102,40:103")}) {
        zap::OfflineSeed seed;
        check(zap::parse_offline_seed(text, seed).empty(), "the client's text parses");
        check(zap::format_offline_seed(seed) == text, "and formats back as the client wrote it");
    }
    // No disks: "discs -".
    std::string none = client_text(3, false, 0);
    none.replace(none.find("discs 3 12 434f4c4c454354"), 25, "discs -");
    none.replace(none.find("unfound 40,1312"), 15, "unfound -");
    zap::OfflineSeed seed;
    check(zap::parse_offline_seed(none, seed).empty() && !seed.discs &&
              zap::format_offline_seed(seed) == none,
          "a file with no disk count formats back as it was");
    // Secret Shuffle's 155-entry table keeps its length.
    uint8_t table[155];
    for (int i = 0; i < 155; ++i) table[i] = static_cast<uint8_t>(i % 23);
    std::string shuffled = "zexion-ap offline 1\ntable " + hex(table, 155) + "\nstart " +
                           hex(g_start, 6) +
                           "\nintel 0\nassists lock\ndiscs -\nunfound -\ntraps -\n";
    for (char& c : shuffled) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    check(zap::parse_offline_seed(shuffled, seed).empty() && seed.locations == 155 &&
              zap::format_offline_seed(seed) == shuffled,
          "a 155-entry table too");
}

void test_the_synced_text_is_refused_when_cut() {
    zap::OfflineSeed seed;
    check(zap::parse_offline_seed(client_text(3, false, 4), seed).empty(), "a seed");
    const std::string text = zap::synced_file_text(seed);
    check(text.size() > 20 && text.compare(text.size() - 13, 4, "crc ") == 0 &&
              text.back() == '\n',
          "the copy ends with its crc line");
    zap::OfflineSeed back;
    check(zap::parse_synced_file(text, back).empty() &&
              zap::format_offline_seed(back) == zap::format_offline_seed(seed),
          "and reads back");
    // Every shorter prefix is refused, the cut labels block included: never read as a seed.
    bool every_cut_refused = true;
    for (size_t n = 0; n < text.size(); ++n) {
        zap::OfflineSeed cut;
        every_cut_refused =
            every_cut_refused && !zap::parse_synced_file(text.substr(0, n), cut).empty();
    }
    check(every_cut_refused, "a copy cut anywhere is refused");
    std::string flipped = text;
    flipped[flipped.find("intel 1") + 6] = '0';
    zap::OfflineSeed bad;
    check(!zap::parse_synced_file(flipped, bad).empty(), "a changed byte is refused");
    check(!zap::parse_synced_file(client_text(3, false, 0), bad).empty(),
          "a local file (no crc line) is not a synced copy");
    std::string crlf;
    for (char c : text) crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    check(!zap::parse_synced_file(crlf, bad).empty(),
          "a text with its line ends changed is refused");
    check(!zap::parse_synced_file("", bad).empty() && !zap::parse_synced_file("\n", bad).empty() &&
              !zap::parse_synced_file("crc 00000000\n", bad).empty(),
          "nothing, or only a crc line, is refused");
}

void test_two_copies_are_made_one() {
    zap::OfflineSeed older, newer;
    check(zap::parse_offline_seed(client_text(5, false, 3, "1312:102"), older).empty() &&
              zap::parse_offline_seed(client_text(2, true, 1), newer).empty(),
          "two copies of one seed");
    zap::OfflineSeed merged = zap::merge_offline_seeds(older, newer);
    check(!merged.lock_assists, "Assist Unlock: free wins");  // free is lock_assists false
    check(merged.count == 5 && merged.discs, "the larger disk count stands");
    check(merged.labels == older.labels, "the labels with more lines stand");
    check(merged.traps.size() == 1 && merged.traps[0].cell == 1312,
          "the traps of the copy that has them");
    zap::OfflineSeed flipped = zap::merge_offline_seeds(newer, older);
    check(flipped.count == 5 && flipped.labels == older.labels && !flipped.lock_assists &&
              flipped.traps.size() == 1,
          "the same result whichever is the newer");
    // A tie goes to the newer one's fields (its scanner's line), and a missing count yields.
    zap::OfflineSeed tie_old, tie_new;
    zap::parse_offline_seed(client_text(4, false, 0), tie_old);
    zap::parse_offline_seed(client_text(4, false, 0), tie_new);
    tie_new.scan_line = "NEWER";
    check(zap::merge_offline_seeds(tie_old, tie_new).scan_line == "NEWER", "a tie: the newer's");
    zap::OfflineSeed no_discs = tie_new;
    no_discs.discs = false;
    no_discs.count = 0;
    check(zap::merge_offline_seeds(tie_old, no_discs).count == 4 &&
              zap::merge_offline_seeds(tie_old, no_discs).discs,
          "a copy with no disk count yields to one that has it");
}

// The seeds of the randomizer saves among the game's slots, as the helper's snapshot holds them
// (Game::saved_seeds, published through SavedSeeds): what a seed's copy waits for. No save file of
// the game is opened by the helper, so the tests give lists, never files.
using Saved = std::vector<zap::SeedKey>;

// Another seed's table (g_table with its first entry changed).
uint8_t g_other_table[154];

Saved saves_of(const uint8_t* table) { return {seed_of(table, g_start)}; }

void test_a_put_reaches_the_save_folder() {
    const std::wstring folder = temp_folder(L"zap_sync_put_local");
    const std::wstring save = temp_folder(L"zap_sync_put_save");
    std::filesystem::create_directories(save);
    std::memcpy(g_other_table, g_table, sizeof(g_table));
    g_other_table[0] = 46;
    const Saved used = saves_of(g_table), other = saves_of(g_other_table);
    write_all(save + L"\\Save Game 1.sav", "SAVE ONE");  // the game's own: never opened
    write_all(save + L"\\GameUnlocks.sav", "UNLOCKS");
    const std::string put = "offline_put seed AP_1-0-1\n" + client_text(3, false, 2);
    zap::OfflineSeed got;
    std::string error = "unset";
    check(zap::put_offline_file(folder, "B1", put, &got, save, &error, &used) == "stored" &&
              error.empty(),
          "a seed stored, and its copy kept in the save folder (a slot uses it)");
    zap::OfflineSeed copy;
    check(zap::parse_synced_file(read_all(save + L"\\" + kCopy), copy).empty() &&
              zap::format_offline_seed(copy) == client_text(3, false, 2),
          "the copy is the seed's text and its crc line");
    check(names_in(save) ==
              std::set<std::wstring>{L"Save Game 1.sav", L"GameUnlocks.sav", kCopy},
          "and it is the only file added: no temporary file left, no other name");
    check(read_all(save + L"\\Save Game 1.sav") == "SAVE ONE" &&
              read_all(save + L"\\GameUnlocks.sav") == "UNLOCKS",
          "the game's own files untouched");
    check(read_all(folder + L"\\seeds\\AP_1-0-1.txt") == client_text(3, false, 2),
          "the local file as before");
    // A poorer put (fewer disks, no labels, locked again) never takes a richer copy's place.
    zap::OfflineSeed rich;
    zap::parse_offline_seed(client_text(8, true, 5), rich);
    write_all(save + L"\\" + kCopy, zap::synced_file_text(rich));
    zap::OfflineSeed poor;
    check(zap::put_offline_file(folder, "B1",
                                "offline_put seed AP_1-0-1\n" + client_text(1, false, 0), &poor,
                                save, &error, &used) == "stored" &&
              error.empty(),
          "a poorer put over a richer copy");
    check(poor.count == 8 && !poor.lock_assists &&
              poor.labels.find("label 24") != std::string::npos,
          "is handed over made one with it");
    check(read_all(folder + L"\\seeds\\AP_1-0-1.txt") == client_text(8, true, 5) &&
              zap::parse_synced_file(read_all(save + L"\\" + kCopy), copy).empty() &&
              copy.count == 8,
          "and so are the local file and the copy");
    // Another seed's file under the same name is replaced by the put one.
    zap::OfflineSeed another;
    g_table[0] = 46;
    zap::parse_offline_seed(client_text(8, true, 5), another);
    g_table[0] = 0;
    write_all(save + L"\\" + kCopy, zap::synced_file_text(another));
    check(zap::put_offline_file(folder, "B1", put, &got, save, &error, &used) == "stored" &&
              got.count == 3 &&
              zap::parse_synced_file(read_all(save + L"\\" + kCopy), copy).empty() &&
              zap::offline_matches(copy, got),
          "a copy of another seed under the name is replaced");
    // A copy that cannot be written is said, and the rest goes on.
    std::filesystem::remove(save + L"\\" + kCopy);
    std::filesystem::create_directories(save + L"\\" + kCopy);
    zap::OfflineSeed unwritten;
    check(zap::put_offline_file(folder, "B1", put, &unwritten, save, &error, &used) ==
                  "unchanged" &&
              !error.empty() && unwritten.count == 3,
          "a copy that cannot be written: said, the local file and the seed unaffected");
    std::filesystem::remove_all(save + L"\\" + kCopy);
    // No save folder: nothing made, nothing said.
    const std::wstring nosave = save + L"-missing";
    check(zap::put_offline_file(folder, "B1", put, &got, nosave, &error, &used) == "unchanged" &&
              error.empty() && !std::filesystem::exists(nosave),
          "no save folder: not made");
    // Too large to keep (padding): no copy either.
    check(zap::put_offline_file(folder, "B1", put + std::string(49 * 1024, '\n'), &got, save,
                                &error, &used) == "error too large" &&
              !std::filesystem::exists(save + L"\\" + kCopy),
          "a file too large to keep has no copy");
    // No slot uses the seed, or none is known yet: the put is kept locally and handed over, and
    // no copy is made (the copy thread writes it when the slots show the seed).
    std::filesystem::remove(save + L"\\" + kCopy);
    zap::OfflineSeed unused;
    check(zap::put_offline_file(folder, "B1", put, &unused, save, &error, &other) == "unchanged" &&
              error.empty() && unused.count == 3 && !std::filesystem::exists(save + L"\\" + kCopy),
          "no slot of the seed: no copy, the seed handed over, nothing said");
    check(zap::put_offline_file(folder, "B1", put, &unused, save, &error, nullptr) == "unchanged" &&
              !std::filesystem::exists(save + L"\\" + kCopy),
          "no list of the slots yet: no copy");
    check(zap::put_offline_file(folder, "B1", put, &unused, save, &error, &used) == "unchanged" &&
              error.empty() &&
              zap::parse_synced_file(read_all(save + L"\\" + kCopy), copy).empty(),
          "a slot of the seed now: the copy is written");
    std::filesystem::remove_all(folder);
    std::filesystem::remove_all(save);
}

void test_the_read_joins_the_save_folder() {
    const std::wstring folder = temp_folder(L"zap_sync_read_local");
    const std::wstring save = temp_folder(L"zap_sync_read_save");
    std::filesystem::create_directories(save);
    std::memcpy(g_other_table, g_table, sizeof(g_table));
    g_other_table[0] = 46;
    const Saved used = saves_of(g_table), second_used = saves_of(g_other_table);
    write_all(save + L"\\Save Game 3.sav", "SAVE THREE");  // the game's own: never opened
    // The first run after an upgrade: only the local copy exists, and it is mirrored.
    write_all(folder + L"\\seeds\\AP_1-0-1.txt", client_text(3, false, 2));
    write_all(folder + L"\\seeds\\notanap.txt", client_text(1, false, 0));  // not a client's name
    zap::OfflineFiles f = zap::read_offline_files(folder, "B1", save, true, &used);
    zap::OfflineSeed copy;
    check(f.seeds.size() == 2 &&
              zap::parse_synced_file(read_all(save + L"\\" + kCopy), copy).empty() &&
              copy.count == 3,
          "a local seed a slot uses gets its copy in the save folder");
    check(names_in(save) == std::set<std::wstring>{L"Save Game 3.sav", kCopy},
          "and a file not named as the client names them does not");
    const std::string once = read_all(save + L"\\" + kCopy);
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    check(read_all(save + L"\\" + kCopy) == once && f.error.empty(),
          "read again: nothing changes");
    // A seed no slot uses gets no copy; one a slot later uses gets it at the next pass.
    g_table[0] = 46;
    write_all(folder + L"\\seeds\\AP_2-0-1.txt", client_text(2, false, 0));
    g_table[0] = 0;
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    const std::wstring copy_two = L"ZexionAP-seed-AP_2-0-1.sav";
    check(f.seeds.size() == 3 && !std::filesystem::exists(save + L"\\" + copy_two),
          "a seed no slot uses: read, and no copy");
    f = zap::read_offline_files(folder, "B1", save, true, nullptr);
    check(!std::filesystem::exists(save + L"\\" + copy_two), "no list of the slots: none");
    Saved both = used;
    both.push_back(second_used[0]);
    f = zap::read_offline_files(folder, "B1", save, true, &both);
    check(std::filesystem::exists(save + L"\\" + copy_two),
          "a slot of it now: its copy at the next pass");
    std::filesystem::remove(save + L"\\" + copy_two);
    std::filesystem::remove(folder + L"\\seeds\\AP_2-0-1.txt");
    // A read that writes nothing (a client that set nothing went): the seeds only made one.
    std::filesystem::remove(save + L"\\" + kCopy);
    f = zap::read_offline_files(folder, "B1", save, false, &used);
    check(f.seeds.size() == 2 && !std::filesystem::exists(save + L"\\" + kCopy),
          "a read with write false makes no copy");
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    check(std::filesystem::exists(save + L"\\" + kCopy), "and the next one that writes does");
    // A new machine: only the copy exists, and the local file is made from it.
    std::filesystem::remove_all(folder);
    f = zap::read_offline_files(folder, "B1", save, false, &used);
    check(f.seeds.size() == 1 && !std::filesystem::exists(folder),
          "a seed only in the save folder is read; with write false no local file is made");
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    check(f.seeds.size() == 1 && f.seeds[0].name == "AP_1-0-1.txt" && f.seeds[0].count == 3 &&
              read_all(folder + L"\\seeds\\AP_1-0-1.txt") == client_text(3, false, 2),
          "and its local file made at a read that writes");
    // Two machines' copies: the richer fields of each.
    write_all(folder + L"\\seeds\\AP_1-0-1.txt", client_text(9, false, 1));
    zap::OfflineSeed theirs;
    zap::parse_offline_seed(client_text(4, true, 4), theirs);
    write_all(save + L"\\" + kCopy, zap::synced_file_text(theirs));
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    check(f.seeds.size() == 1 && f.seeds[0].count == 9 && !f.seeds[0].lock_assists &&
              f.seeds[0].labels.find("label 23") != std::string::npos,
          "two copies are read as one: the larger count, Assist Unlock, the longer labels");
    const std::string joined = client_text(9, true, 4);
    check(read_all(folder + L"\\seeds\\AP_1-0-1.txt") == joined &&
              zap::parse_synced_file(read_all(save + L"\\" + kCopy), copy).empty() &&
              zap::format_offline_seed(copy) == joined,
          "and each is written as the result");
    // A copy of a seed no slot uses is read, merged in memory, and not refreshed.
    const Saved none;
    write_all(save + L"\\" + kCopy, zap::synced_file_text(theirs));
    const std::string stale = read_all(save + L"\\" + kCopy);
    write_all(folder + L"\\seeds\\AP_1-0-1.txt", client_text(9, false, 1));
    f = zap::read_offline_files(folder, "B1", save, true, &none);
    check(f.seeds.size() == 1 && f.seeds[0].count == 9 && read_all(save + L"\\" + kCopy) == stale,
          "no slot uses the seed: its copy is read and made one, and left as it is");
    // A copy cut short by a sync is refused, said, and replaced by the good local file.
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    const std::string good = read_all(save + L"\\" + kCopy);
    const std::string cut = good.substr(0, good.size() - 40);
    write_all(save + L"\\" + kCopy, cut);
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    check(f.seeds.size() == 1 && f.seeds[0].count == 9 &&
              f.error.find("crc") != std::string::npos && read_all(save + L"\\" + kCopy) == good,
          "a cut copy is refused, said, and replaced");
    // With no local seed, a cut copy is neither used nor touched.
    std::filesystem::remove_all(folder);
    write_all(save + L"\\" + kCopy, cut);
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    check(f.seeds.empty() && read_all(save + L"\\" + kCopy) == cut &&
              !std::filesystem::exists(folder),
          "a cut copy and no local seed: nothing used, nothing written");
    // A local file that is there and was not read is not written over from a copy.
    write_all(save + L"\\" + kCopy, zap::synced_file_text(theirs));
    write_all(folder + L"\\seeds\\AP_1-0-1.txt", "not a seed\n");
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    check(read_all(folder + L"\\seeds\\AP_1-0-1.txt") == "not a seed\n" && f.seeds.size() == 1,
          "a local file that does not read is left alone (the copy is read, not copied over it)");
    // A copy that cannot be opened (held with no sharing, as Steam or a scanner may) is not
    // read, and so is not written over: it is left as it is, said, and the next pass has it.
    write_all(folder + L"\\seeds\\AP_1-0-1.txt", client_text(9, false, 1));
    write_all(save + L"\\" + kCopy, zap::synced_file_text(theirs));
    const std::string held_text = read_all(save + L"\\" + kCopy);
    HANDLE held = CreateFileW((save + L"\\" + kCopy).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(held != INVALID_HANDLE_VALUE, "the copy opened with no sharing, for the test");
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    check(f.error.find("unreadable") != std::string::npos && f.seeds.size() == 1 &&
              f.seeds[0].count == 9,
          "a copy that cannot be opened is said, the local file's seed used");
    CloseHandle(held);
    check(read_all(save + L"\\" + kCopy) == held_text, "and it was not replaced");
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    check(f.error.empty() && read_all(save + L"\\" + kCopy) != held_text &&
              zap::parse_synced_file(read_all(save + L"\\" + kCopy), copy).empty() &&
              copy.count == 9,
          "once it can be opened the pass merges it and writes the result");
    std::filesystem::remove_all(folder);
    // Other names are not read.
    std::filesystem::remove(save + L"\\" + kCopy);
    write_all(save + L"\\ZexionAP-seed-AP_1-0-1.txt", client_text(3, false, 0));
    write_all(save + L"\\ZexionAP-seed-bad.sav", "x\n");
    write_all(save + L"\\ZexionAP-seed-AP_2-0-1.sav.tmp", "x\n");
    write_all(save + L"\\other-ZexionAP-seed-AP_2-0-1.sav", zap::synced_file_text(theirs));
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    check(f.seeds.empty(), "files not named ZexionAP-seed-<NAME>.sav are not read");
    // The seed of another table under a name is kept apart, not merged.
    std::filesystem::remove(save + L"\\ZexionAP-seed-AP_1-0-1.txt");
    write_all(folder + L"\\seeds\\AP_1-0-1.txt", client_text(3, false, 0));
    g_table[0] = 46;
    zap::OfflineSeed diff;
    zap::parse_offline_seed(client_text(7, false, 0), diff);
    g_table[0] = 0;
    write_all(save + L"\\" + kCopy, zap::synced_file_text(diff));
    f = zap::read_offline_files(folder, "B1", save, true, &used);
    check(f.seeds.size() == 1 && f.seeds[0].count == 3 && !f.error.empty(),
          "a copy of another seed under the name is not merged, and said");
    // No save folder: not made.
    check(zap::read_offline_files(folder, "B1", save + L"-missing", true, &used).seeds.size() == 1 &&
              !std::filesystem::exists(save + L"-missing"),
          "no save folder: the local seeds alone, none made");
    check(zap::read_offline_files(folder, "B1", L"").seeds.size() == 1, "no save folder named");
    check(read_all(save + L"\\Save Game 3.sav") == "SAVE THREE", "the saves were never touched");
    std::filesystem::remove_all(folder);
    std::filesystem::remove_all(save);
}

void test_a_put_whose_file_cannot_be_kept_waits_for_a_slot() {
    // A client's put when no slot uses the seed and its own file cannot be written (no folder, a
    // full disk): no pass finds a file for it, so the helper keeps it and the copy thread's pass
    // writes the copy once a slot uses the seed.
    const std::wstring blocker = temp_folder(L"zap_pending_blocker");  // a file, not a folder
    write_all(blocker, "x");
    const std::wstring unwritable = blocker + L"\\under-a-file";
    const std::wstring save = temp_folder(L"zap_pending_save");
    const std::wstring empty_local = temp_folder(L"zap_pending_local");
    std::filesystem::create_directories(save);
    const std::wstring copy_name = L"ZexionAP-seed-AP_9-0-1.sav";
    const std::string put = "offline_put seed AP_9-0-1\n" + client_text(5, false, 1);
    const Saved none, used = saves_of(g_table);
    std::string error;
    zap::OfflineSeed got;
    check(zap::put_offline_file(unwritable, "B1", put, &got, save, &error, &none).rfind("error", 0) ==
                  0 &&
              got.count == 5 && !std::filesystem::exists(save + L"\\" + copy_name),
          "a put whose file cannot be kept, no slot: handed over, no copy yet");
    zap::OfflineFiles f = zap::read_offline_files(empty_local, "B1", save, true, &none);
    check(!std::filesystem::exists(save + L"\\" + copy_name), "a pass with no slot: still none");
    f = zap::read_offline_files(empty_local, "B1", save, false, &used);
    check(!std::filesystem::exists(save + L"\\" + copy_name), "a pass that writes nothing: none");
    f = zap::read_offline_files(empty_local, "B1", save, true, &used);
    zap::OfflineSeed copy;
    check(zap::parse_synced_file(read_all(save + L"\\" + copy_name), copy).empty() && copy.count == 5,
          "the pass after a slot uses the seed writes the copy of the waiting put");
    std::filesystem::remove(save + L"\\" + copy_name);
    f = zap::read_offline_files(empty_local, "B1", save, true, &used);
    check(!std::filesystem::exists(save + L"\\" + copy_name), "and the entry is gone once written");
    // An entry goes too once a local file for the seed exists.
    zap::put_offline_file(unwritable, "B1", "offline_put seed AP_8-0-1\n" + client_text(2, false, 2),
                          &got, save, &error, &none);
    write_all(empty_local + L"\\seeds\\AP_8-0-1.txt", client_text(2, false, 2));
    f = zap::read_offline_files(empty_local, "B1", save, true, &none);
    std::filesystem::remove(empty_local + L"\\seeds\\AP_8-0-1.txt");
    // A slot uses the seed now: a copy would be written if the entry were still waiting.
    f = zap::read_offline_files(empty_local, "B1", save, true, &used);
    check(!std::filesystem::exists(save + L"\\ZexionAP-seed-AP_8-0-1.sav"),
          "a local file existing cleared the waiting entry");
    std::filesystem::remove_all(blocker);
    std::filesystem::remove_all(save);
    std::filesystem::remove_all(empty_local);
}

void test_saved_seeds_are_published_from_the_games_slots() {
    // The game's slots as the stand-in holds them (the file select's match): a randomizer save in
    // use; not an empty or deleted slot, a normal game, or the slot a new game is starting in.
    reset_game();
    clear_slots();
    for (int n : {3, 4, 6, 7}) slot_block(n)[0x935E] = 0;
    for (int n : {3, 4, 5, 7}) put<uint16_t>(slot_block(n), 0x9400, 1);  // 6 a normal save
    for (int n : {3, 4, 5, 6, 7}) put_seed(rando_copy(n));
    rando_copy(4)[0x26C] ^= 1;
    rando_copy(7)[0x26C] ^= 2;
    zap::Game game;
    check(game.saved_seeds().empty(), "no seeds before the helper is configured");
    check(configure(game, false).empty(), "configured");
    const Saved seeds = game.saved_seeds();
    check(seeds.size() == 3 && zap::offline_matches(seeds[0], zap::offline_seed_of(rando_copy(3))) &&
              zap::offline_matches(seeds[1], zap::offline_seed_of(rando_copy(4))) &&
              zap::offline_matches(seeds[2], zap::offline_seed_of(rando_copy(7))),
          "the seeds of slots 3, 4 and 7, not the deleted 5 or the normal 6");
    g_save_loaded = 0xFF;
    g_transition_fn = reinterpret_cast<void*>(&newgame_fn);
    g_transition_timer = 40;
    g_current_slot = 2;  // slot 3
    check(game.saved_seeds().size() == 2, "a new game in slot 3: not listed");
    g_transition_fn = nullptr;
    g_transition_timer = 0;
    g_current_slot = 0;
    g_save_loaded = 3;
    check(game.saved_seeds().size() == 3, "listed again");

    zap::SavedSeeds published;
    check(published.generation() == 0 && published.get().empty(), "nothing published yet");
    check(published.publish({}) && published.generation() == 1,
          "the first list is published, even an empty one");
    check(!published.publish({}) && published.generation() == 1, "the same list: not again");
    check(published.publish(seeds) && published.generation() == 2 && published.get().size() == 3,
          "a changed list is");
    check(!published.publish(game.saved_seeds()) && published.generation() == 2,
          "the same seeds read again: not");
    Saved fewer = seeds;
    fewer.pop_back();
    check(published.publish(fewer) && published.generation() == 3, "one fewer: published");
}

void test_the_save_folder_is_the_games() {
    // Under a Roaming AppData folder, as SDL names it; never from a relative path.
    check(zap::save_folder_under(L"C:\\Users\\Someone\\AppData\\Roaming") ==
              L"C:\\Users\\Someone\\AppData\\Roaming\\Gallant Leaf\\Zexion",
          "the save folder: Roaming AppData's, then the game's two names");
    check(zap::save_folder_under(L"C:\\Users\\Someone\\AppData\\Roaming\\") ==
              L"C:\\Users\\Someone\\AppData\\Roaming\\Gallant Leaf\\Zexion",
          "a trailing separator does not double");
    check(zap::save_folder_under(L"relative").empty() && zap::save_folder_under(L"").empty(),
          "a relative path is no save folder");
    // The real one, as Windows names it: asked once, absolute, and not made by asking.
    const std::wstring real = zap::save_folder();
    check(real.empty() || (real.size() > 20 && real.rfind(L"\\Gallant Leaf\\Zexion") ==
                                                   real.size() - 20),
          "save_folder() ends in the game's two names");
    check(zap::save_folder() == real, "and is the same each time (found once)");
}

// A junction at `link` to `target` (no administrator needed): true if it was made.
bool make_junction(const std::wstring& link, const std::wstring& target) {
    const std::wstring command =
        L"cmd /c mklink /J \"" + link + L"\" \"" + target + L"\" > nul 2>&1";
    return _wsystem(command.c_str()) == 0;
}

void test_a_test_build_takes_only_a_folder_in_temp() {
    const std::filesystem::path temp = std::filesystem::temp_directory_path();
    const std::wstring inside = (temp / L"zap_restrict_probe" / L"ZexionAP").wstring();
    const std::wstring resolved = zap::restrict_to_test_folder(inside);
    check(!resolved.empty() && resolved.size() >= 27 &&
              resolved.compare(resolved.size() - 27, 27, L"zap_restrict_probe\\ZexionAP") == 0,
          "a folder inside the temporary folder, returned resolved");
    check(zap::restrict_to_test_folder(L"").empty(), "no folder: none");
    check(zap::restrict_to_test_folder(L"relative\\folder").empty(), "a relative one: refused");
    check(zap::restrict_to_test_folder(temp.wstring()).empty(),
          "the temporary folder itself: refused");
    check(zap::restrict_to_test_folder((temp / L"x" / L".." / L"..").wstring()).empty(),
          "a path that climbs out of it: refused");
    check(zap::restrict_to_test_folder((temp / L"a" / L".." / L"b").wstring()).empty(),
          "a path with .. in it at all: refused (never folded as text)");
    std::wstring bare = temp.wstring();
    while (!bare.empty() && (bare.back() == L'\\' || bare.back() == L'/')) bare.pop_back();
    check(zap::restrict_to_test_folder(bare + L"2\\x").empty(),
          "a sibling that begins with its name: refused");
    std::wstring upper = inside;
    for (wchar_t& c : upper) c = static_cast<wchar_t>(towupper(c));
    check(!zap::restrict_to_test_folder(upper).empty(), "case apart, as Windows paths are");
    // Never the real ones, however they are reached: the folders Windows names for this user.
    const std::vector<std::wstring> saves = zap::os_save_folders(), locals = zap::os_offline_folders();
    check(saves.size() == 1 && !saves[0].empty() && locals.size() == 1 && !locals[0].empty(),
          "Windows names this user's two folders");
    check(zap::restrict_to_test_folder(saves[0]).empty(), "the real save folder: refused");
    check(zap::restrict_to_test_folder(saves[0] + L"\\Mods").empty(), "and what is inside it");
    check(zap::restrict_to_test_folder(locals[0]).empty(), "the real per-user folder: refused");
    check(zap::restrict_to_test_folder(locals[0] + L"\\seeds").empty(), "and what is inside it");
    // What a test sets LOCALAPPDATA to is its own: allowed when it is inside the temporary folder.
    check(!zap::restrict_to_test_folder(inside + L"\\seeds").empty(),
          "a scratch folder of a test's own");
    // A junction under the temporary folder is followed: where it points is what counts.
    wchar_t profile[MAX_PATH] = {};
    GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    const std::wstring outside =
        (std::filesystem::path(profile) /
         (L"zap_guard_probe_" + std::to_wstring(GetCurrentProcessId())))
            .wstring();
    std::filesystem::remove_all(outside);
    std::filesystem::create_directories(outside);
    const std::wstring link = (temp / (L"zap_junction_" + std::to_wstring(GetCurrentProcessId()))).wstring();
    RemoveDirectoryW(link.c_str());  // a leftover junction only, never what it points at
    if (make_junction(link, outside)) {
        check(zap::restrict_to_test_folder(link).empty() &&
                  zap::restrict_to_test_folder(link + L"\\under").empty(),
              "a junction in temp to a folder outside it: refused, and what is under it");
        // The write primitives refuse it too (the test-only core): a put through it writes nothing.
        const Saved used = saves_of(g_table);
        std::string error;
        zap::OfflineSeed got;
        zap::put_offline_file(temp.wstring() + L"\\zap_guard_local", "B1",
                              "offline_put seed AP_1-0-1\n" + client_text(3, false, 1), &got, link,
                              &error, &used);
        check(!error.empty() && std::filesystem::is_empty(outside),
              "a copy through the junction is refused and writes nothing there");
        std::filesystem::remove_all(temp / L"zap_guard_local");
        RemoveDirectoryW(link.c_str());  // the junction only, never what it points at
        // Never a link, even for a moment, to a real folder: should this process die with it in
        // place, a temp cleaner that follows reparse points could reach real saves. The real
        // folders are tested above by their paths alone, and a junction to a stand-in here.
    } else {
        check(false, "a junction could not be made for the test");
    }
    // The write primitives, handed a folder outside the temporary folder, write nothing, even in
    // a program that is not a test build of the helper: the test-only core refuses.
    {
        const Saved used = saves_of(g_table);
        std::string error;
        zap::OfflineSeed got;
        zap::put_offline_file(temp.wstring() + L"\\zap_guard_local", "B1",
                              "offline_put seed AP_1-0-1\n" + client_text(3, false, 1), &got,
                              outside, &error, &used);
        check(!error.empty() && std::filesystem::is_empty(outside),
              "a put with a save folder outside temp: the copy is refused");
        zap::put_offline_file(outside, "B1", "offline_put seed AP_1-0-1\n" + client_text(3, false, 1),
                              &got);
        check(std::filesystem::is_empty(outside), "and a local folder outside temp is not written");
        std::filesystem::remove_all(temp / L"zap_guard_local");
    }
    std::filesystem::remove_all(outside);
}

int main() {
    test_the_seed_text_round_trips();
    test_the_synced_text_is_refused_when_cut();
    test_two_copies_are_made_one();
    test_a_put_reaches_the_save_folder();
    test_the_read_joins_the_save_folder();
    test_a_put_whose_file_cannot_be_kept_waits_for_a_slot();
    test_saved_seeds_are_published_from_the_games_slots();
    test_the_save_folder_is_the_games();
    test_a_test_build_takes_only_a_folder_in_temp();
    test_the_file();
    test_the_folder();
    test_the_cache_must_fit_the_game();
    test_the_helper_writes_what_the_client_sends();
    return report("offline tests");
}
