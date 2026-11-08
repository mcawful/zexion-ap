// Host tests for AP saves without the client (offline.h): the seed file, its match and disk
// count, the folder read, and a stand-in game dressing a save of the seed and undressing it.

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
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
    check(counts.size() == 1 && counts.count(3) == 1 && counts[3] == 3,
          "the file select: the seed's save with its count");
    slot_block(3)[0x877F - 2 + 1312] = 0x1B;  // a disk this save found offline, on its own map
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(game.files().counts().at(3) == 4, "counted for that save on the file select");

    check(configure(game).empty() && game.active(), "a client's config");
    check(game.files().counts().empty(), "the file select's counts left to the client");
    check(game.queued_popups() == 0, "no notice: no save was dressed (the file select)");

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
    other.set_disk_slots({{4, 7}});
    for (int i = 0; i < 16; ++i) other.watch_offline();
    auto merged = other.files().counts();
    check(merged.size() == 2 && merged.at(4) == 7 && merged.at(3) == 4,
          "the client's slot 4 and the known seed's slot 3, together");
    other.set_disk_slots({});
    check(other.files().counts().size() == 1 && other.files().counts().at(3) == 4,
          "the client's taken back: the offline one stays");
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

// The cache needs every key, as a client's config does: one missing refuses.
void test_a_cache_needs_every_key() {
    HMODULE self = GetModuleHandleW(nullptr);
    const auto full = stand_in_config();
    for (const auto& [key, rva] : full) {
        auto older = full;
        older.erase(key);
        zap::Game refused;
        if (refused.configure(self, older, false) != "missing " + key || refused.configured()) {
            check(false, ("a cache missing " + key + " refuses").c_str());
        }
    }
}

}  // namespace

int main() {
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
    return report("offline tests");
}
