// Host tests for AP saves without the client (offline.h): the seed file, its match and disk
// count, the folder read, and a stand-in game dressing a save of the seed and undressing it.

#include <windows.h>

#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include "check.h"
#include "game_calls.h"
#include "offline.h"
#include "stand_in.h"
#include "stand_in_menu.h"
#include "fake_gl.h"

using namespace stand_in;

namespace {

uint8_t g_table[154];
void no_callback(void*, int) {}  // ARCHIPELAGO's callback: none here
const uint8_t g_start[6] = {6, 0, 191, 0, 0x49, 1};

std::string seed_text(const std::string& discs = "discs 3 12 434F4C4C454354",
                      const std::string& unfound = "unfound 1312,40") {
    return "zexion-ap offline 1\n"
           "table " + hex(g_table, sizeof(g_table)) + "\n"
           "start " + hex(g_start, sizeof(g_start)) + "\n"
           "intel 1\n"
           "assists lock\n" +
           discs + "\n" + unfound + "\n"
           "ap_labels\n"
           "label 23 14 4150204954454D0A464F5220414C494345\n";
}

// A seed of a table of `n` entries and a start, as offline_seed_of reads a section.
zap::OfflineSeed seed_of(const uint8_t* table, const uint8_t* start, int n = 154) {
    zap::OfflineSeed s;
    s.locations = n;
    std::memcpy(s.table, table, static_cast<size_t>(n));
    std::memcpy(s.start, start, sizeof(s.start));
    return s;
}

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

void put_seed(BYTE* section) {
    std::memcpy(section + 0x26C, g_table, sizeof(g_table));
    std::memcpy(section + 0x310, g_start, sizeof(g_start));
}

// The seed's save loaded, in play.
void load_seed() {
    std::memset(g_section, 0, sizeof(g_section));
    put_seed(g_section);
    g_rando_marker = 1;
    g_save_loaded = 3;
    g_menu = 0;
}

// `game` configured from the cache with `s` known, and s's save in play, dressed.
void dress_offline(zap::Game& game, const zap::OfflineSeed& s) {
    configure(game, false);
    game.set_offline({s});
    load_seed();
    for (int i = 0; i < 16; ++i) game.watch_offline();
}

void test_a_save_of_the_seed_dressed_offline() {
    reset_game();
    zap::Game game;
    check(configure(game, false).empty() && !game.active(), "configured from the cache: inactive");
    zap::OfflineSeed s;
    zap::parse_offline_seed(seed_text(), s);
    game.set_offline({s});

    std::memset(g_section, 0, sizeof(g_section));
    g_rando_marker = 1;
    g_save_loaded = 0xFF;  // the title: nothing
    game.watch_offline();
    check(!game.intel_on() && !game.assists_locked() && game.scan_line().empty(), "no save: off");

    put_seed(g_section);
    g_save_loaded = 3;
    g_flags[39] = 0;
    std::memset(g_map_grid, 0, sizeof(g_map_grid));
    g_menu = 2;  // still loading (a menu up): not yet
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(!game.intel_on() && game.queued_popups() == 0, "not dressed before play");
    g_menu = 0;
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(game.intel_on() && game.assists_locked(), "the seed's save: INTEL CHIP, ASSIST locked");
    check(game.queued_popups() == 1, "the warning queued once");
    for (int i = 0; i < 32; ++i) game.watch_offline();
    check(game.queued_popups() == 1, "and only once a load");
    check(game.disc_count() == 3 && game.scan_line() == "COLLECT" && !game.discs_told(),
          "the disks, not told yet");
    check(game.offline_state() == "1:", "hello: one seed, its file loaded");

    g_map_grid[1312] = 0x1B;
    g_flags[39] = 1;
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(game.disc_count() == 4 && game.discs_told(), "a disk found offline, and told");

    g_rando_marker = 0;  // a normal save keeps the old section: the marker tells
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(!game.intel_on() && !game.assists_locked() && game.scan_line().empty(),
          "another save: all of it put back");
    check(game.queued_popups() == 0, "the warning not shown yet withdrawn");
    g_rando_marker = 1;

    // Slot 3 holds the seed, slot 4 another, slot 5 is empty (its section still the seed's).
    for (int n : {3, 4, 5}) {
        put<uint16_t>(slot_block(n), 0x9400, 1);
        slot_block(n)[0x935E] = 0;
    }
    slot_block(5)[0x935E] = 0xFF;
    put_seed(rando_copy(3));
    put_seed(rando_copy(5));
    g_save_loaded = 0xFF;
    for (int i = 0; i < 16; ++i) game.watch_offline();
    auto counts = game.files().counts();
    check(counts.size() == 1 && counts.count(3) == 1 && counts[3] == zap::DiskCount(3, s.unfound),
          "the file select: the seed's save with its count and own disks' cells");

    check(configure(game).empty() && game.active(), "a client's config");
    check(game.files().counts().empty(), "the file select's counts dropped with the look, counted again at the next");
    check(game.queued_popups() == 0, "no notice: no save was dressed (the file select)");
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(game.files().counts().size() == 1 && game.files().counts().at(3) == zap::DiskCount(3, s.unfound),
          "with a client too: each save's own, from the seed's file (the entry reads its map)");

    zap::Game again;
    dress_offline(again, s);
    check(again.intel_on() && again.queued_popups() == 1, "dressed offline, warned");
    // A client's short first connection (its version check) undresses it; the real one follows.
    again.leave_offline();
    configure(again);
    check(again.queued_popups() == 0 && !again.intel_on(), "a client: the warning withdrawn");
    check(again.client_back() && again.queued_popups() == 1,
          "its session sees the save: CLIENT CONNECTED, once");
    check(!again.client_back() && again.queued_popups() == 1, "not twice");

    zap::Game other;  // a client of another seed
    configure(other);  // a client, active
    other.set_offline({s});
    for (int i = 0; i < 16; ++i) other.watch_offline();
    check(!other.intel_on(), "a client's own: no offline look by itself");
    check(other.other_save() == "told" && other.queued_popups() == 1,
          "told once: the other-seed notice");
    check(other.other_save() == "already told" && other.queued_popups() == 1,
          "and not again this load");
    for (int i = 0; i < 16; ++i) other.watch_offline();
    check(other.intel_on() && other.assists_locked() && other.queued_popups() == 1,
          "the other seed's save dressed, with no not-connected warning");
    // The client takes its own look back (none for this save): the dressed save keeps its own.
    other.set_labels({});
    other.set_intel(false);
    other.set_assists_locked(false);
    other.set_discs(0, 0, "");
    for (int i = 0; i < 16; ++i) other.watch_offline();
    check(other.intel_on() && other.assists_locked() && other.scan_line() == "COLLECT",
          "a client's look taken back: the dressed save's own again");
    // A refused unload: the offline look left by the put back, dressed again by its watch.
    other.put_back_game(nullptr);
    check(!other.intel_on() && !other.assists_locked(), "put back: the offline look left");
    for (int i = 0; i < 16; ++i) other.watch_offline();
    check(other.intel_on() && other.assists_locked(), "the watch dresses it again");
    g_save_loaded = 0xFF;
    for (int i = 0; i < 16; ++i) other.watch_offline();
    check(!other.intel_on() && !other.assists_locked(), "the save gone: undressed");
    g_save_loaded = 3;
    for (int i = 0; i < 16; ++i) other.watch_offline();
    check(!other.intel_on(), "a new load: the client's again until it says otherwise");
    check(other.other_save() == "told" && other.queued_popups() == 1,
          "and told again for this load (the last load's notice withdrawn)");
    configure(other);  // a reconnect
    check(other.queued_popups() == 0, "the last client's notice withdrawn");
    check(other.other_save() == "told" && other.queued_popups() == 1, "and told again");
    {
        zap::Game plain;  // a randomizer save of no known seed
        configure(plain);
        check(plain.other_save() == "unknown" && plain.queued_popups() == 0,
              "a plain randomizer game: no notice");
    }

    g_save_loaded = 0xFF;  // the file select
    for (int i = 0; i < 16; ++i) other.watch_offline();
    check(other.files().counts().size() == 1 &&
              other.files().counts().at(3) == zap::DiskCount(3, s.unfound),
          "the known seed's slot 3 keeps its own count with a client of another seed");
}

// A dressed save stays dressed, ASSIST locked, through a menu; a client going away with the
// options menu up locks it again in the same frame call, so the cursor never finds it open.
void test_assist_locked_again_in_the_same_frame() {
    reset_game();
    zap::Game game;
    zap::OfflineSeed s;
    zap::parse_offline_seed(seed_text(), s);
    dress_offline(game, s);
    g_menu = 2;  // the pause menu, then the options
    OptionsMenu options(reinterpret_cast<void*>(&widget_handler_fn),
                        reinterpret_cast<void*>(&assist_menu_fn));
    g_current_menu = options.menu;
    game.watch_all(&no_callback);
    check(options.callback() == nullptr, "dressed offline: ASSIST locked");
    configure(game);  // a client of this seed, its own look (no lock sent yet)
    game.watch_all(&no_callback);
    check(options.callback() == &assist_menu_fn, "the client's look: ASSIST free");
    game.client_gone(&no_callback);
    game.watch_all(&no_callback);
    check(options.callback() == nullptr, "the client gone: locked again in the same frame call");
    game.put_back_game(&no_callback);
    g_current_menu = nullptr;
}

void test_the_look_held_in_a_menu() {
    reset_game();
    zap::Game game;
    zap::OfflineSeed s;
    zap::parse_offline_seed(seed_text(), s);
    dress_offline(game, s);
    check(game.assists_locked() && game.offline_state() == "1:", "dressed in play");
    g_menu = 2;  // the pause menu up
    game.set_offline({s});  // `helper.py status` has come and gone
    check(game.assists_locked() && game.intel_on() && game.offline_state() == "1:",
          "the files read again: still dressed");
    for (int i = 0; i < 32; ++i) game.watch_offline();
    check(game.assists_locked(), "and kept");
    check(game.queued_popups() == 1, "no second not-connected warning");
    g_rando_marker = 0;  // a read of no known seed in the menu
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(game.assists_locked(), "held with the menu up");
    g_rando_marker = 1;
    configure(game);  // a client of this seed: its own look
    check(!game.assists_locked(), "the client's look");
    game.client_gone(&no_callback);
    game.watch_offline();
    check(game.assists_locked() && game.intel_on(), "the client gone in the menu: dressed at once");
    zap::OfflineSeed other = s;
    other.table[0] ^= 1;
    game.set_offline({other});
    check(!game.assists_locked() && game.offline_state() == "1",
          "files without the seed: undressed");
    game.set_offline({s});
    g_rando_marker = 0;  // in play, no known seed: another save
    g_menu = 0;
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(!game.assists_locked(), "in play as no known seed: undressed");
    g_rando_marker = 1;
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(game.assists_locked(), "dressed again");
    g_save_loaded = 0xFF;
    game.watch_offline();
    check(!game.assists_locked(), "no save: undressed at once");
    g_save_loaded = 3;
    g_menu = 2;  // the next load, not in play yet
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(!game.assists_locked(), "a new load waits for play");
    g_menu = 0;
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(game.assists_locked(), "then dressed");

    // The files read again newest first: CLIENT CONNECTED. still for the save played offline.
    zap::Game order;
    configure(order, false);
    order.set_offline({other, s});
    load_seed();
    for (int i = 0; i < 16; ++i) order.watch_offline();
    order.set_offline({s, other});
    check(order.assists_locked() && order.offline_state() == "2:", "dressed, its place moved");
    configure(order);
    check(order.client_back(), "CLIENT CONNECTED. for the save played offline");
}

// The helper's notices are each for the load they were queued in: withdrawn if the player
// leaves it before they show.
void test_a_notice_not_shown_goes_with_its_save() {
    reset_game();
    zap::OfflineSeed s;
    zap::parse_offline_seed(seed_text(), s);
    auto frames = [](zap::Game& game, int n) {
        for (int i = 0; i < n; ++i) {
            game.watch_all(nullptr);
            game.pump_popups();
        }
    };
    {
        zap::Game game;
        check(configure(game).empty(), "a client's config");
        game.set_offline({s});
        load_seed();
        check(game.other_save() == "told" && game.queued_popups() == 1,
              "another seed's save: the notice queued");
        g_menu = 1;  // paused before it shows
        frames(game, 40);
        check(g_messages == 0 && game.queued_popups() == 1, "not shown while paused");
        g_menu = 0;
        g_save_loaded = 0xFF;  // quit to the title
        frames(game, 1);
        check(game.queued_popups() == 0, "withdrawn as the save goes");
        std::memset(g_section + 0x26C, 7, sizeof(g_table));  // the client's own seed's save
        g_save_loaded = 3;
        frames(game, 40);
        check(g_messages == 0, "not shown in the client's own seed's save", g_text);
        game.put_back_game(nullptr);
    }
    {
        // A client going away before its notice shows.
        zap::Game game;
        configure(game);
        game.set_offline({s});
        put_seed(g_section);
        g_messages = 0;
        check(game.other_save() == "told" && game.queued_popups() == 1, "the notice queued");
        g_menu = 1;  // paused
        frames(game, 40);
        game.client_gone(nullptr);
        check(game.queued_popups() == 0, "withdrawn as its client goes");
        g_menu = 0;
        frames(game, 200);
        // (The box's lines may join words: its start is enough to tell the two apart.)
        check(g_messages == 1 && g_text.rfind("ARCHIPELAGO CLIENT", 0) == 0,
              "only the not-connected warning shown", g_text);
        configure(game);
        check(game.other_save() == "told", "the next client is told again");
        game.put_back_game(nullptr);
    }
    {
        // Warned, paused before it showed, a client of another seed come and gone meanwhile.
        g_messages = 0;
        zap::Game game;
        dress_offline(game, s);
        check(game.queued_popups() == 1, "warned with no client");
        g_menu = 1;
        configure(game);
        check(game.other_save() == "told" && game.queued_popups() == 1,
              "a client of another seed: its notice in the warning's place");
        game.client_gone(nullptr);
        check(game.queued_popups() == 0, "withdrawn as it goes");
        g_menu = 0;
        frames(game, 200);
        check(g_messages == 1 && g_text.rfind("ARCHIPELAGO CLIENT", 0) == 0,
              "the not-connected warning given again", g_text);
        game.put_back_game(nullptr);
    }
    {
        // Warned and shown; a client comes back, then goes again in the same load.
        g_messages = 0;
        zap::Game game;
        dress_offline(game, s);
        frames(game, 200);
        check(g_messages == 1 && g_text.find("NOT") != std::string::npos,
              "the not-connected warning shown", g_text);
        configure(game);
        check(game.client_back(), "CLIENT CONNECTED queued");
        frames(game, 200);
        check(g_messages == 2 && g_text.find("NOT") == std::string::npos,
              "CLIENT CONNECTED shown", g_text);
        game.client_gone(nullptr);
        frames(game, 200);
        check(g_messages == 3 && g_text.find("NOT") != std::string::npos,
              "warned again when it goes", g_text);
        frames(game, 200);
        check(g_messages == 3, "once for that loss");
        game.put_back_game(nullptr);
    }
    {
        zap::Game game;  // CLIENT CONNECTED. not shown yet when its client goes
        dress_offline(game, s);
        game.leave_offline();
        configure(game);
        check(game.client_back() && game.queued_popups() == 1, "CLIENT CONNECTED queued");
        game.client_gone(nullptr);
        check(game.queued_popups() == 0, "withdrawn as its client goes");
        game.put_back_game(nullptr);
    }
    {
        zap::Game game;  // or when a client configures again
        dress_offline(game, s);
        game.leave_offline();
        configure(game);
        check(game.client_back() && game.queued_popups() == 1, "CLIENT CONNECTED queued");
        configure(game);
        check(game.queued_popups() == 0, "withdrawn as a client configures again");
        game.put_back_game(nullptr);
    }
    {
        // CLIENT CONNECTED, for a save played offline, likewise.
        g_messages = 0;
        zap::Game game;
        dress_offline(game, s);
        game.leave_offline();
        configure(game);
        check(game.client_back() && game.queued_popups() == 1, "CLIENT CONNECTED queued");
        g_save_loaded = 0xFF;
        frames(game, 1);
        check(game.queued_popups() == 0, "withdrawn as the save goes");
        g_save_loaded = 3;
        frames(game, 40);
        check(g_messages == 0, "not shown in the next save", g_text);
        game.put_back_game(nullptr);
    }
    g_save_loaded = 0xFF;
    g_rando_marker = 0;
}

// A seed file put while a client is connected reaches the look at once; a dressed save keeps
// its own record.
void test_a_seed_put_with_a_client_reaches_the_look() {
    reset_game();
    zap::Game game;
    check(configure(game).empty(), "a client's config");
    load_seed();
    check(game.other_save() == "unknown", "no file for the save's seed: unknown");
    zap::OfflineSeed b;
    zap::parse_offline_seed(seed_text(), b);
    b.name = "b";
    game.merge_offline({b});
    check(game.known_seeds() == 1 && game.ap_save_loaded(), "known to the AP-save test");
    check(game.other_save() == "told", "and to the look: told");
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(game.intel_on() && game.assists_locked() && game.offline_state() == "1:b",
          "the save dressed");
    zap::OfflineSeed newer = b;  // the same seed, its look changed
    newer.name = "b2";
    newer.intel = false;
    zap::OfflineSeed c = b;
    c.name = "c";
    c.table[0] ^= 1;
    game.merge_offline({newer, c});
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(game.intel_on() && game.offline_state() == "2:b", "the dressed record kept; one added");
    check(game.known_seeds() == 2, "both known");
    g_save_loaded = 0xFF;
    for (int i = 0; i < 16; ++i) game.watch_offline();
    game.merge_offline({newer});
    check(game.offline_state() == "2", "in place, not added");
    g_save_loaded = 3;
    check(game.other_save() == "told", "told on the next load");
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(!game.intel_on() && game.assists_locked() && game.offline_state() == "2:b2",
          "dressed with the newer record");
    game.put_back_game(nullptr);
    g_save_loaded = 0xFF;
    g_rando_marker = 0;
}

void test_the_ap_saves_slots() {
    // Which slots get the file select's AP logo: a randomizer save in use whose own copy of the
    // section is a known seed's, from a seed file (no client) or a client's put.
    reset_game();
    clear_slots();
    for (int n : {3, 4, 6, 7}) slot_block(n)[0x935E] = 0;
    for (int n : {3, 4, 5, 7}) put<uint16_t>(slot_block(n), 0x9400, 1);  // 6 a normal save
    // 5 deleted, 6 given a normal game: their copies kept.
    for (int n : {3, 5, 6}) put_seed(rando_copy(n));
    put_seed(rando_copy(4));
    rando_copy(4)[0x26C] ^= 1;  // 4: a plain randomizer game
    put_seed(rando_copy(7));
    rando_copy(7)[0x26C] ^= 2;  // 7: a seed only a client has put

    zap::Game game;
    check(configure(game, false).empty(), "configured from the cache");
    check(game.ap_file_slots().empty(), "no seed known: no AP save");
    zap::OfflineSeed s;
    zap::parse_offline_seed(seed_text(), s);
    game.set_offline({s});
    check(game.ap_file_slots() == std::set<int>{3},
          "the seed file's save; not a plain randomizer game, a deleted slot or a normal save");
    game.add_known_seeds({zap::offline_seed_of(rando_copy(7))});
    check(game.ap_file_slots() == std::set<int>{3, 7}, "and a seed a client put");
    check(configure(game).empty() && game.active() &&
              game.ap_file_slots() == std::set<int>{3, 7},
          "with a client too");

    // A new game queued in slot 3 (a deleted AP save there, its empty mark 0 for the fade, its
    // marker word and section kept): the slot is not that save's for the 40 frames, neither for
    // the logo nor for the count; another slot's new game leaves it.
    g_save_loaded = 0xFF;
    g_transition_fn = reinterpret_cast<void*>(&newgame_fn);
    g_transition_timer = 40;
    g_current_slot = 3;  // slot 4
    check(game.ap_file_slots() == std::set<int>{3, 7}, "a new game in another slot: kept");
    g_current_slot = 2;  // slot 3
    check(game.ap_file_slots() == std::set<int>{7}, "a new game in slot 3: not listed");
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(game.files().counts().empty(), "nor counted");
    g_transition_fn = nullptr;
    g_transition_timer = 0;
    g_current_slot = 0;
    check(game.ap_file_slots() == std::set<int>{3, 7}, "listed again once none is queued");
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(game.files().counts().size() == 1 && game.files().counts().count(3) == 1,
          "and counted again");
    g_save_loaded = 3;  // as it was

    check(game.files().ap_slots().empty(), "not before a frame");
    game.watch_all(&no_callback);
    check(game.files().ap_slots() == std::set<int>{3, 7}, "the first frame sets them");
    game.put_back_game(&no_callback);
    check(game.files().ap_slots().empty() && !game.files().holds_hook(), "put back: none");

    // A seed known only as the labels' (the loaded save's when a client set them).
    zap::Game labeled;
    check(configure(labeled).empty(), "a client's config");
    put_seed(g_section);
    g_section[0x26C] ^= 2;  // slot 7's seed
    g_rando_marker = 1;
    g_save_loaded = 7;
    std::map<int, zap::Label> labels;
    check(zap::parse_labels("ap_labels\nlabel 5 2 41", labels).empty(), "a label parses");
    labeled.set_labels(labels);
    check(labeled.ap_file_slots() == std::set<int>{7}, "the labels' seed's save");
}

// The file select's AP logo as the frame call paints it: whiter through a new game's fade to
// white, cleared when the file select's look is put back.
void test_the_logo_through_the_frame_call() {
    reset_game();
    gl_fake::reset_gl();
    clear_slots();
    slot_block(3)[0x935E] = 0;
    put<uint16_t>(slot_block(3), 0x9400, 1);
    put_seed(rando_copy(3));
    zap::Game game;
    check(configure(game).empty(), "a client's config");
    game.set_gl(gl_fake::fake_gl());
    zap::OfflineSeed s;
    zap::parse_offline_seed(seed_text(), s);
    game.add_known_seeds({s});
    g_save_loaded = 0xFF;  // the file select
    g_transition_fn = nullptr;
    g_transition_timer = 0;
    // The logo's (6, 0) is red ('r'): its green rises as it is whitened.
    auto logo_green = [](const gl_fake::Upload& u) { return u.pixels[(0 * 16 + 6) * 4 + 1]; };
    auto logo_uploads = [] {
        std::vector<gl_fake::Upload> out;
        for (const gl_fake::Upload& u : gl_fake::g_uploads) {
            if (u.x == zap::kApLogoX && u.y == zap::kApLogoY && u.w == 16 && u.h == 16) {
                out.push_back(u);
            }
        }
        return out;
    };
    game.watch_all(&no_callback);
    std::vector<gl_fake::Upload> logos = logo_uploads();
    check(logos.size() == 1 && logo_green(logos[0]) < 0x80,
          "an AP save listed: the logo painted at (96, 1040) in its colors");
    const uint8_t own = logos.empty() ? 0 : logo_green(logos[0]);
    g_transition_fn = reinterpret_cast<void*>(&newgame_fn);  // a new game queued, 20 frames left
    g_transition_timer = 20;
    gl_fake::g_uploads.clear();
    game.watch_all(&no_callback);
    logos = logo_uploads();
    check(logos.size() == 1 && logo_green(logos[0]) > own &&
              logo_green(logos[0]) < 0xFF,
          "a new game's fade: painted again, whiter");
    g_transition_timer = 1;
    gl_fake::g_uploads.clear();
    game.watch_all(&no_callback);
    logos = logo_uploads();
    check(logos.size() == 1 && logo_green(logos[0]) == 0xFF, "all white on its last frame");
    g_transition_fn = nullptr;
    g_transition_timer = 0;
    gl_fake::g_uploads.clear();
    game.put_back_files();
    logos = logo_uploads();
    check(logos.size() == 1 && gl_fake::all_blank(logos),
          "put_back_files clears the logo from the texture");
    game.put_back_game(&no_callback);
    clear_slots();
    g_save_loaded = 3;
}

// A received disk's glyph is painted while its popup waits and while it shows, with the goal off
// (a load before the client turns it on again), and cleared once the popup has gone.
void test_the_disk_glyph_kept_for_its_popup() {
    reset_game();
    gl_fake::reset_gl();
    g_slots_live = g_message_is_last = true;  // the popup the game's last message, as in play
    zap::Game game;
    check(configure(game).empty(), "a client's config");
    game.set_gl(gl_fake::fake_gl());
    auto glyphs = [] {
        std::vector<gl_fake::Upload> out;
        for (const gl_fake::Upload& u : gl_fake::g_uploads) {
            if (u.x == zap::font_x(zap::kDiskGlyph) && u.y == zap::font_y(zap::kDiskGlyph)) {
                out.push_back(u);
            }
        }
        return out;
    };
    check(apply(game, "apply\npopup 44 FROM BOB") == "applied", "a received disk");
    game.watch_all(&no_callback);
    check(glyphs().size() == 1 && !gl_fake::all_blank(glyphs()), "painted while it waits");
    game.pump_popups();
    check(!g_text.empty() && static_cast<uint8_t>(g_text[0]) == zap::kDiskGlyph, "then shown");
    gl_fake::g_uploads.clear();
    game.watch_all(&no_callback);
    check(glyphs().empty(), "kept while it shows");
    put<int32_t>(g_slots, 0x1EC, 0);  // its frames run out
    game.watch_all(&no_callback);
    check(glyphs().size() == 1 && gl_fake::all_blank(glyphs()), "cleared once it has gone");
    game.put_back_game(&no_callback);
    g_slots_live = g_message_is_last = false;
}

// The cache needs every key but the optional text_cut_code (the next test) and the three
// AILMENTS keys (the traps run without them: game_tests.cpp), as a client's config does: one
// missing refuses.
void test_a_cache_needs_every_key() {
    HMODULE self = GetModuleHandleW(nullptr);
    const auto full = stand_in_config();
    for (const auto& [key, rva] : full) {
        if (key == "text_cut_code" || key.rfind("ailments", 0) == 0) continue;  // optional
        auto older = full;
        older.erase(key);
        zap::Game refused;
        if (refused.configure(self, older, false) != "missing " + key || refused.configured()) {
            check(false, ("a cache missing " + key + " refuses").c_str());
        }
    }
}

// The text cut's patch step by step on the stand-in's code, as TextCut makes it in watch_all:
// read, built, written and put back.
void test_the_text_cut_patches_the_stand_in() {
    reset_game();
    BYTE now[zap::kTextCutSize];
    check(zap::safe_read(g_text_cut_code, now, sizeof now), "the stand-in's cut reads");
    check(std::memcmp(now, kTextCutGame, sizeof now) == 0, "and holds the game's bytes");
    BYTE patch[zap::kTextCutSize] = {};
    check(zap::text_cut_patch(now, patch), "the cut's patch builds from them");
    check(zap::patch_code(g_text_cut_code, patch, sizeof patch), "patch_code writes it");
    check(code_is(g_text_cut_code, patch, sizeof patch), "the stand-in holds the patch");
    check(zap::patch_code(g_text_cut_code, kTextCutGame, sizeof kTextCutGame), "and puts it back");
    zap::Game game;
    check(configure(game).empty(), "a client's config");
    game.watch_all(&no_callback);
    const char* blocker = game.unload_blocker();
    check(blocker != nullptr && std::string(blocker).find("cut") != std::string::npos,
          "and the Game patches it", blocker == nullptr ? "(no blocker)" : blocker);
    game.put_back_game(&no_callback);
    // As the helper with no client: configured from its cache, not active.
    auto bytes = [] {
        std::string out;
        char hex[4];
        for (int i = 0; i < zap::kTextCutSize; ++i) {
            std::snprintf(hex, sizeof hex, "%02X ", code_byte(g_text_cut_code, i));
            out += hex;
        }
        return out;
    };
    zap::Game idle;
    check(idle.configure(GetModuleHandleW(nullptr), stand_in_config(), false).empty(),
          "a cache configures");
    idle.watch_all(&no_callback);
    const char* idle_blocker = idle.unload_blocker();
    check(idle_blocker != nullptr && std::string(idle_blocker).find("cut") != std::string::npos,
          "and the inactive Game patches it",
          std::string(idle_blocker == nullptr ? "(no blocker)" : idle_blocker) + "; bytes " +
              bytes());
    idle.watch_all(&no_callback);
    check(!code_is(g_text_cut_code, kTextCutGame),
          "and after a second frame", bytes());
    idle.put_back_game(&no_callback);
}

// 1.0.0's cache, written before text_cut_code: it configures everything else, the text window's
// cut left the game's, until a client's config, which holds the key, patches it.
void test_a_cache_without_the_text_cut() {
    HMODULE self = GetModuleHandleW(nullptr);
    auto older = stand_in_config();
    older.erase("text_cut_code");
    auto is_games = [] {
        return code_is(g_text_cut_code, kTextCutGame);
    };
    reset_game();
    zap::Game game;
    check(game.configure(self, older, false).empty() && game.configured() && !game.active(),
          "a cache without text_cut_code configures");
    game.watch_all(&no_callback);
    check(is_games(), "the cut left the game's");
    check(configure(game).empty(), "a client's config, text_cut_code in it");
    game.watch_all(&no_callback);
    check(!is_games(), "then the cut is patched");
    game.put_back_game(&no_callback);
    check(is_games(), "and put back with the game");

    zap::Game full;
    check(full.configure(self, stand_in_config(), false).empty(), "a cache with it configures");
    full.watch_all(&no_callback);
    check(!is_games(), "and the cut is patched with no client");
    full.put_back_game(&no_callback);
    check(is_games(), "put back with the game");
}

}  // namespace

// ---- Seed files in the save folder (offline.h, "Seed files in the save folder") ----

// A seed file as the client writes it (client/offline.py, seed_text): lower-case hex, the labels
// last. `count` disks, Assist Unlock free or locked, `labels` label lines.
std::string client_text(int count, bool free_assists, int labels, const std::string& traps = "-") {
    std::string text = "zexion-ap offline 1\ntable " + hex(g_table, sizeof(g_table)) + "\nstart " +
                       hex(g_start, sizeof(g_start)) + "\nintel 1\nassists " +
                       (free_assists ? "free" : "lock") + "\ndiscs " + std::to_string(count) +
                       " 12 434f4c4c454354\nunfound 40,1312\ntraps " + traps + "\n";
    if (labels > 0) text += "ap_labels\n";
    for (int i = 0; i < labels; ++i) {
        text += "label " + std::to_string(20 + i) + " 14 4150204954454d\n";
    }
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
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

// A save file of slot `n` in `save`, as far as the helper reads one (offline.cpp, saved_seeds): the
// randomizer marker, the empty mark or not, and the seed's table and start. `marker` 1 is a
// randomizer save, `empty` marks the slot a deleted save left.
void write_save(const std::wstring& save, int n, const uint8_t* table = g_table,
                const uint8_t* start = g_start, int marker = 1, bool empty = false) {
    std::string data(0x13100, '\0');
    data[0x9402] = static_cast<char>(marker);
    data[0x9360] = empty ? '\xFF' : '\0';
    std::memcpy(&data[0x12A80], table, 154);
    std::memcpy(&data[0x12B24], start, 6);
    write_all(save + L"\\Save Game " + std::to_wstring(n) + L".sav", data);
}

// Another seed's table (g_table with its first entry changed).
uint8_t g_other_table[154];

void test_a_put_reaches_the_save_folder() {
    const std::wstring folder = temp_folder(L"zap_sync_put_local");
    const std::wstring save = temp_folder(L"zap_sync_put_save");
    std::filesystem::create_directories(save);
    std::memcpy(g_other_table, g_table, sizeof(g_table));
    g_other_table[0] = 46;
    write_save(save, 1);  // a save of the seed
    write_all(save + L"\\GameUnlocks.sav", "UNLOCKS");
    const std::string slot_one = read_all(save + L"\\Save Game 1.sav");
    const std::string put = "offline_put seed AP_1-0-1\n" + client_text(3, false, 2);
    zap::OfflineSeed got;
    std::string error = "unset";
    check(zap::put_offline_file(folder, "B1", put, &got, save, &error) == "stored" &&
              error.empty(),
          "a seed stored, and its copy kept in the save folder (a save of it is there)");
    zap::OfflineSeed copy;
    check(zap::parse_synced_file(read_all(save + L"\\" + kCopy), copy).empty() &&
              zap::format_offline_seed(copy) == client_text(3, false, 2),
          "the copy is the seed's text and its crc line");
    check(names_in(save) ==
              std::set<std::wstring>{L"Save Game 1.sav", L"GameUnlocks.sav", kCopy},
          "and it is the only file added: no temporary file left, no other name");
    check(read_all(save + L"\\Save Game 1.sav") == slot_one &&
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
                                save, &error) == "stored" &&
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
    zap::OfflineSeed other;
    g_table[0] = 46;
    zap::parse_offline_seed(client_text(8, true, 5), other);
    g_table[0] = 0;
    write_all(save + L"\\" + kCopy, zap::synced_file_text(other));
    check(zap::put_offline_file(folder, "B1", put, &got, save, &error) == "stored" &&
              got.count == 3 &&
              zap::parse_synced_file(read_all(save + L"\\" + kCopy), copy).empty() &&
              zap::offline_matches(copy, got),
          "a copy of another seed under the name is replaced");
    // A copy that cannot be written is said, and the rest goes on.
    std::filesystem::remove(save + L"\\" + kCopy);
    std::filesystem::create_directories(save + L"\\" + kCopy);
    zap::OfflineSeed unwritten;
    check(zap::put_offline_file(folder, "B1", put, &unwritten, save, &error) == "unchanged" &&
              !error.empty() && unwritten.count == 3,
          "a copy that cannot be written: said, the local file and the seed unaffected");
    std::filesystem::remove_all(save + L"\\" + kCopy);
    // No save folder: nothing made, nothing said.
    const std::wstring nosave = save + L"-missing";
    check(zap::put_offline_file(folder, "B1", put, &got, nosave, &error) == "unchanged" &&
              error.empty() && !std::filesystem::exists(nosave),
          "no save folder: not made");
    // Too large to keep (padding): no copy either.
    check(zap::put_offline_file(folder, "B1", put + std::string(49 * 1024, '\n'), &got, save,
                                &error) == "error too large" &&
              !std::filesystem::exists(save + L"\\" + kCopy),
          "a file too large to keep has no copy");
    // No save uses the seed: the put is kept locally and handed over, and no copy is made.
    // A slot of another seed, a normal game, an empty slot and a short file do not count.
    std::filesystem::remove(save + L"\\Save Game 1.sav");
    write_save(save, 2, g_other_table);
    write_save(save, 3, g_table, g_start, 0);         // a normal game
    write_save(save, 4, g_table, g_start, 1, true);   // a slot a deleted save left
    write_all(save + L"\\Save Game 5.sav", "SHORT");  // not a save
    std::filesystem::remove(save + L"\\" + kCopy);
    zap::OfflineSeed unused;
    check(zap::put_offline_file(folder, "B1", put, &unused, save, &error) == "unchanged" &&
              error.empty() && unused.count == 3 && !std::filesystem::exists(save + L"\\" + kCopy),
          "no save of the seed: no copy, the seed handed over, nothing said");
    // The next put after the game has saved it writes the copy (the same text: local unchanged).
    write_save(save, 6);
    check(zap::put_offline_file(folder, "B1", put, &unused, save, &error) == "unchanged" &&
              error.empty() &&
              zap::parse_synced_file(read_all(save + L"\\" + kCopy), copy).empty(),
          "a save of the seed now: the copy is written");
    std::filesystem::remove_all(folder);
    std::filesystem::remove_all(save);
}

void test_the_read_joins_the_save_folder() {
    const std::wstring folder = temp_folder(L"zap_sync_read_local");
    const std::wstring save = temp_folder(L"zap_sync_read_save");
    std::filesystem::create_directories(save);
    std::memcpy(g_other_table, g_table, sizeof(g_table));
    g_other_table[0] = 46;
    write_save(save, 3);  // a save of the seed
    const std::string slot_three = read_all(save + L"\\Save Game 3.sav");
    // The first run after an upgrade: only the local copy exists, and it is mirrored.
    write_all(folder + L"\\seeds\\AP_1-0-1.txt", client_text(3, false, 2));
    write_all(folder + L"\\seeds\\notanap.txt", client_text(1, false, 0));  // not a client's name
    zap::OfflineFiles f = zap::read_offline_files(folder, "B1", save);
    zap::OfflineSeed copy;
    check(f.seeds.size() == 2 &&
              zap::parse_synced_file(read_all(save + L"\\" + kCopy), copy).empty() &&
              copy.count == 3,
          "a local seed a save uses gets its copy in the save folder");
    check(names_in(save) == std::set<std::wstring>{L"Save Game 3.sav", kCopy},
          "and a file not named as the client names them does not");
    const std::string once = read_all(save + L"\\" + kCopy);
    f = zap::read_offline_files(folder, "B1", save);
    check(read_all(save + L"\\" + kCopy) == once && f.error.empty(),
          "read again: nothing changes");
    // A seed no save uses gets no copy; one a save later uses gets it at the next read.
    g_table[0] = 46;
    zap::OfflineSeed second;
    zap::parse_offline_seed(client_text(2, false, 0), second);
    write_all(folder + L"\\seeds\\AP_2-0-1.txt", client_text(2, false, 0));
    g_table[0] = 0;
    f = zap::read_offline_files(folder, "B1", save);
    const std::wstring copy_two = L"ZexionAP-seed-AP_2-0-1.sav";
    check(f.seeds.size() == 3 && !std::filesystem::exists(save + L"\\" + copy_two),
          "a seed no save uses: read, and no copy");
    write_save(save, 7, g_other_table);
    f = zap::read_offline_files(folder, "B1", save);
    check(std::filesystem::exists(save + L"\\" + copy_two),
          "a save of it now: its copy at the next read");
    std::filesystem::remove(save + L"\\Save Game 7.sav");
    std::filesystem::remove(save + L"\\" + copy_two);
    std::filesystem::remove(folder + L"\\seeds\\AP_2-0-1.txt");
    // A read that writes nothing (a client that set nothing went): the seeds only made one.
    std::filesystem::remove(save + L"\\" + kCopy);
    f = zap::read_offline_files(folder, "B1", save, false);
    check(f.seeds.size() == 2 && !std::filesystem::exists(save + L"\\" + kCopy),
          "a read with write false makes no copy");
    f = zap::read_offline_files(folder, "B1", save);
    check(std::filesystem::exists(save + L"\\" + kCopy), "and the next one that writes does");
    // A new machine: only the copy exists, and the local file is made from it.
    std::filesystem::remove_all(folder);
    f = zap::read_offline_files(folder, "B1", save, false);
    check(f.seeds.size() == 1 && !std::filesystem::exists(folder),
          "a seed only in the save folder is read; with write false no local file is made");
    f = zap::read_offline_files(folder, "B1", save);
    check(f.seeds.size() == 1 && f.seeds[0].name == "AP_1-0-1.txt" && f.seeds[0].count == 3 &&
              read_all(folder + L"\\seeds\\AP_1-0-1.txt") == client_text(3, false, 2),
          "and its local file made at a read that writes");
    // Two machines' copies: the richer fields of each.
    write_all(folder + L"\\seeds\\AP_1-0-1.txt", client_text(9, false, 1));
    zap::OfflineSeed theirs;
    zap::parse_offline_seed(client_text(4, true, 4), theirs);
    write_all(save + L"\\" + kCopy, zap::synced_file_text(theirs));
    f = zap::read_offline_files(folder, "B1", save);
    check(f.seeds.size() == 1 && f.seeds[0].count == 9 && !f.seeds[0].lock_assists &&
              f.seeds[0].labels.find("label 23") != std::string::npos,
          "two copies are read as one: the larger count, Assist Unlock, the longer labels");
    const std::string joined = client_text(9, true, 4);
    check(read_all(folder + L"\\seeds\\AP_1-0-1.txt") == joined &&
              zap::parse_synced_file(read_all(save + L"\\" + kCopy), copy).empty() &&
              zap::format_offline_seed(copy) == joined,
          "and each is written as the result");
    // A copy of a seed no save uses is read, merged in memory, and not refreshed.
    std::filesystem::remove(save + L"\\Save Game 3.sav");
    write_all(save + L"\\" + kCopy, zap::synced_file_text(theirs));
    const std::string stale = read_all(save + L"\\" + kCopy);
    write_all(folder + L"\\seeds\\AP_1-0-1.txt", client_text(9, false, 1));
    f = zap::read_offline_files(folder, "B1", save);
    check(f.seeds.size() == 1 && f.seeds[0].count == 9 && read_all(save + L"\\" + kCopy) == stale,
          "no save uses the seed: its copy is read and made one, and left as it is");
    write_save(save, 3);
    // A copy cut short by a sync is refused, said, and replaced by the good local file.
    f = zap::read_offline_files(folder, "B1", save);
    const std::string good = read_all(save + L"\\" + kCopy);
    const std::string cut = good.substr(0, good.size() - 40);
    write_all(save + L"\\" + kCopy, cut);
    f = zap::read_offline_files(folder, "B1", save);
    check(f.seeds.size() == 1 && f.seeds[0].count == 9 &&
              f.error.find("crc") != std::string::npos && read_all(save + L"\\" + kCopy) == good,
          "a cut copy is refused, said, and replaced");
    // With no local seed, a cut copy is neither used nor touched.
    std::filesystem::remove_all(folder);
    write_all(save + L"\\" + kCopy, cut);
    f = zap::read_offline_files(folder, "B1", save);
    check(f.seeds.empty() && read_all(save + L"\\" + kCopy) == cut &&
              !std::filesystem::exists(folder),
          "a cut copy and no local seed: nothing used, nothing written");
    // A local file that is there and was not read is not written over from a copy.
    write_all(save + L"\\" + kCopy, zap::synced_file_text(theirs));
    write_all(folder + L"\\seeds\\AP_1-0-1.txt", "not a seed\n");
    f = zap::read_offline_files(folder, "B1", save);
    check(read_all(folder + L"\\seeds\\AP_1-0-1.txt") == "not a seed\n" && f.seeds.size() == 1,
          "a local file that does not read is left alone (the copy is read, not copied over it)");
    std::filesystem::remove_all(folder);
    // Other names are not read.
    std::filesystem::remove(save + L"\\" + kCopy);
    write_all(save + L"\\ZexionAP-seed-AP_1-0-1.txt", client_text(3, false, 0));
    write_all(save + L"\\ZexionAP-seed-bad.sav", "x\n");
    write_all(save + L"\\ZexionAP-seed-AP_2-0-1.sav.tmp", "x\n");
    write_all(save + L"\\other-ZexionAP-seed-AP_2-0-1.sav", zap::synced_file_text(theirs));
    f = zap::read_offline_files(folder, "B1", save);
    check(f.seeds.empty(), "files not named ZexionAP-seed-<NAME>.sav are not read");
    // The seed of another table under a name is kept apart, not merged.
    std::filesystem::remove(save + L"\\ZexionAP-seed-AP_1-0-1.txt");
    write_all(folder + L"\\seeds\\AP_1-0-1.txt", client_text(3, false, 0));
    g_table[0] = 46;
    zap::OfflineSeed another;
    zap::parse_offline_seed(client_text(7, false, 0), another);
    g_table[0] = 0;
    write_all(save + L"\\" + kCopy, zap::synced_file_text(another));
    f = zap::read_offline_files(folder, "B1", save);
    check(f.seeds.size() == 1 && f.seeds[0].count == 3 && !f.error.empty(),
          "a copy of another seed under the name is not merged, and said");
    // No save folder: not made.
    check(zap::read_offline_files(folder, "B1", save + L"-missing").seeds.size() == 1 &&
              !std::filesystem::exists(save + L"-missing"),
          "no save folder: the local seeds alone, none made");
    check(zap::read_offline_files(folder, "B1", L"").seeds.size() == 1, "no save folder named");
    check(read_all(save + L"\\Save Game 3.sav") == slot_three, "the saves were never touched");
    std::filesystem::remove_all(folder);
    std::filesystem::remove_all(save);
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

void test_a_test_build_takes_only_a_folder_in_temp() {
    const std::filesystem::path temp = std::filesystem::temp_directory_path();
    const std::wstring inside = (temp / L"zap_restrict_probe" / L"ZexionAP").wstring();
    check(zap::restrict_to_test_folder(inside) == inside, "a folder inside the temporary folder");
    check(zap::restrict_to_test_folder(L"").empty(), "no folder: none");
    check(zap::restrict_to_test_folder(L"relative\\folder").empty(), "a relative one: refused");
    check(zap::restrict_to_test_folder(temp.wstring()).empty(),
          "the temporary folder itself: refused");
    check(zap::restrict_to_test_folder((temp / L"x" / L".." / L"..").wstring()).empty(),
          "a path that climbs out of it: refused");
    check(!zap::restrict_to_test_folder((temp / L"a" / L".." / L"b").wstring()).empty(),
          "one that climbs and stays inside: allowed");
    std::wstring bare = temp.wstring();
    while (!bare.empty() && (bare.back() == L'\\' || bare.back() == L'/')) bare.pop_back();
    check(zap::restrict_to_test_folder(bare + L"2\\x").empty(),
          "a sibling that begins with its name: refused");
    std::wstring upper = inside;
    for (wchar_t& c : upper) c = static_cast<wchar_t>(towupper(c));
    check(!zap::restrict_to_test_folder(upper).empty(), "case apart, as Windows paths are");
    // Never the real ones, however they are reached: the folders Windows names for this user.
    const std::wstring real_save = zap::os_save_folder(), real_local = zap::os_offline_folder();
    check(!real_save.empty() && !real_local.empty(), "Windows names this user's two folders");
    check(zap::restrict_to_test_folder(real_save).empty(), "the real save folder: refused");
    check(zap::restrict_to_test_folder(real_save + L"\\Mods").empty(), "and what is inside it");
    check(zap::restrict_to_test_folder(real_local).empty(), "the real per-user folder: refused");
    check(zap::restrict_to_test_folder(real_local + L"\\seeds").empty(), "and what is inside it");
    // What a test sets LOCALAPPDATA to is its own: allowed when it is inside the temporary folder.
    check(zap::restrict_to_test_folder(inside + L"\\seeds") == inside + L"\\seeds",
          "a scratch folder of a test's own");
}

int main() {
    test_the_seed_text_round_trips();
    test_the_synced_text_is_refused_when_cut();
    test_two_copies_are_made_one();
    test_a_put_reaches_the_save_folder();
    test_the_read_joins_the_save_folder();
    test_the_save_folder_is_the_games();
    test_a_test_build_takes_only_a_folder_in_temp();
    test_the_file();
    test_the_folder();
    test_the_cache_must_fit_the_game();
    test_the_helper_writes_what_the_client_sends();
    test_a_save_of_the_seed_dressed_offline();
    test_a_notice_not_shown_goes_with_its_save();
    test_the_look_held_in_a_menu();
    test_assist_locked_again_in_the_same_frame();
    test_a_seed_put_with_a_client_reaches_the_look();
    test_the_ap_saves_slots();
    test_the_logo_through_the_frame_call();
    test_the_disk_glyph_kept_for_its_popup();
    test_a_cache_needs_every_key();
    test_the_text_cut_patches_the_stand_in();
    test_a_cache_without_the_text_cut();
    return report("offline tests");
}
