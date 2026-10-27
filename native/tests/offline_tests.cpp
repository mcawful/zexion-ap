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
    check(zap::offline_matches(s, g_table, g_start), "matches its own table and start");
    uint8_t other[154];
    std::memcpy(other, g_table, sizeof(other));
    other[5] ^= 1;
    check(!zap::offline_matches(s, other, g_start), "another table does not");
    uint8_t moved[6] = {6, 0, 191, 0, 0x4A, 1};
    check(!zap::offline_matches(s, g_table, moved), "another start does not");

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
    check(put.name == "AP_1-0-1" && zap::offline_matches(put, g_table, g_start),
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
    // Code that is surely there.
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

    // Another save (a normal one keeps the old section: the marker says so).
    g_rando_marker = 0;
    for (int i = 0; i < 16; ++i) game.watch_offline();
    check(!game.intel_on() && !game.assists_locked() && game.scan_line().empty(),
          "another save: all of it put back");
    check(game.queued_popups() == 0, "the warning not shown yet withdrawn");
    g_rando_marker = 1;

    // The file select: slot 3 holds the seed, slot 4 another, slot 5 is empty (its section
    // still the seed's).
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

    // A client configures: the offline look goes.
    check(configure(game).empty() && game.active(), "a client's config");
    check(game.files().counts().empty(), "the file select's counts left to the client");
    check(game.queued_popups() == 0, "no notice: no save was dressed (the file select)");

    // Played offline, then a client connects: told so, and the not-connected warning withdrawn.
    zap::Game again;
    configure(again, false);
    again.set_offline({s});
    g_save_loaded = 3;
    g_menu = 0;
    put_seed(g_section);
    for (int i = 0; i < 16; ++i) again.watch_offline();
    check(again.intel_on() && again.queued_popups() == 1, "dressed offline, warned");
    // A client's short first connection (its version check) undresses it, as any client going
    // away does; the real one comes next.
    again.leave_offline();
    configure(again);
    check(again.queued_popups() == 0 && !again.intel_on(), "a client: the warning withdrawn");
    check(again.client_back() && again.queued_popups() == 1,
          "its session sees the save: CLIENT CONNECTED, once");
    check(!again.client_back() && again.queued_popups() == 1, "not twice");

    // A client of another seed: its session says the loaded save is not its seed's.
    zap::Game other;
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
    // A client attaching meanwhile takes its own look back (it has none for this save): the
    // dressed save keeps its own.
    other.set_labels({});
    other.set_intel(false);
    other.set_assists_locked(false);
    other.set_discs(0, 0, "");
    for (int i = 0; i < 16; ++i) other.watch_offline();
    check(other.intel_on() && other.assists_locked() && other.scan_line() == "COLLECT",
          "a client's look taken back: the dressed save's own again");
    {
        // A refused unload meanwhile: the look is the offline one's, not set again with what
        // the client set; the watch dresses the save again by itself.
        zap::ClientState saved = other.snapshot();
        check(!saved.intel && !saved.assists_locked && saved.labels.empty() &&
                  saved.scan_line.empty(),
              "a dressed save's look is left out of the snapshot");
        other.put_back_all(nullptr);
        other.rearm(saved);
        check(!other.intel_on() && !other.assists_locked(), "so rearm does not set it");
        for (int i = 0; i < 16; ++i) other.watch_offline();
        check(other.intel_on() && other.assists_locked(), "the watch dresses it again");
    }
    g_save_loaded = 0xFF;
    for (int i = 0; i < 16; ++i) other.watch_offline();
    check(!other.intel_on() && !other.assists_locked(), "the save gone: undressed");
    g_save_loaded = 3;
    for (int i = 0; i < 16; ++i) other.watch_offline();
    check(!other.intel_on(), "a new load: the client's again until it says otherwise");
    check(other.other_save() == "told" && other.queued_popups() == 2,
          "and told again for this load");
    configure(other);  // a reconnect
    check(other.other_save() == "told" && other.queued_popups() == 3, "and again on a reconnect");
    {
        // A randomizer save of no known seed (a plain randomizer game): nothing said or done.
        zap::Game plain;
        configure(plain);
        check(plain.other_save() == "unknown" && plain.queued_popups() == 0,
              "a plain randomizer game: no notice");
    }

    // The file select with a client: its counts for its saves, the offline ones for others'.
    g_save_loaded = 0xFF;
    other.set_disk_slots({{4, 7}});
    for (int i = 0; i < 16; ++i) other.watch_offline();
    auto merged = other.files().counts();
    check(merged.size() == 2 && merged.at(4) == 7 && merged.at(3) == 4,
          "the client's slot 4 and the known seed's slot 3, together");
    other.set_disk_slots({});
    check(other.files().counts().size() == 1 && other.files().counts().at(3) == 4,
          "the client's taken back: the offline one stays");
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

    // The frame call gives them to the file select's entries; putting back takes them away.
    check(game.files().ap_slots().empty(), "not before a frame");
    game.watch_all(&no_callback);
    check(game.files().ap_slots() == std::set<int>{3, 7}, "the first frame sets them");
    game.put_back_all(&no_callback);
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

// The file select's AP logo as the frame call paints it (Game::watch_files): in its colors,
// whiter through a new game's fade to white (its routine queued, the timer counting down), and
// cleared from the texture when the file select's look is put back (restore_files).
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
    game.restore_files();
    logos = logo_uploads();
    check(logos.size() == 1 && gl_fake::all_blank(logos),
          "restore_files clears the logo from the texture");
    game.put_back_all(&no_callback);
    clear_slots();
    g_save_loaded = 3;
}

// The keys only the client's parts read (game_calls.cpp, client_only), in the table's order.
const char* const kClientOnly[] = {
    "weapon_strip_gate", "hud_number_fn",   "hud_layout_fn", "energy_hud_fn", "weapon_strip_fn",
    "suit_color_fn",     "current_slot",    "randomizer_menu", "panel_handler_fn",
    "rando_menu_fn",     "placement_fn",    "rando_newgame_fn", "start_count", "start_cells",
    "start_flags",       "event_handlers",  "death_fn",      "halt_flag",     "routine_table",
    "fade",              "fade_level",
};

// The cache of a client from before a release that added client-only keys (here: all of them).
// The helper configures from it, inactive, with those members null: every part the offline look
// uses runs (a null one read would fault here), and the client's parts refuse until a client's
// config sets every key. A cache missing any other key still refuses.
void test_a_cache_without_the_client_only_keys() {
    HMODULE self = GetModuleHandleW(nullptr);
    auto partial = stand_in_config();
    std::string named;
    for (const char* key : kClientOnly) {
        check(partial.erase(key) == 1, "a client-only key of the stand-in's config");
        named += (named.empty() ? "" : ",") + std::string(key);
    }
    for (const auto& [key, rva] : partial) {
        auto older = partial;
        older.erase(key);
        zap::Game refused;
        if (refused.configure(self, older, false) != "missing " + key || refused.configured()) {
            check(false, ("a cache missing " + key + " (needed offline) refuses").c_str());
        }
    }
    {
        zap::Game client;
        check(client.configure(self, partial) == "missing weapon_strip_gate" &&
                  !client.configured(),
              "a client's config needs every key");
        // One that does not fit (here: data in code) is left out as a missing one is.
        auto misplaced = stand_in_config();
        misplaced["fade"] = misplaced["message_fn"];
        zap::Game cached;
        check(cached.configure(self, misplaced, false).empty() && cached.left_out() == "fade",
              "a cache's client-only key that does not fit: left out");
        check(!client.configure(self, misplaced).empty(), "but not from a client");
    }

    reset_game();
    zap::Game game;
    check(game.configure(self, partial, false).empty() && game.configured() && !game.active(),
          "the cache without them configures, inactive");
    check(!game.client_ready() && game.left_out() == named, "and says which it lacks");
    zap::OfflineSeed s;
    zap::parse_offline_seed(seed_text(), s);
    game.set_offline({s});

    // The title and the file select: slot 3 holds the seed's save.
    clear_slots();
    slot_block(3)[0x935E] = 0;
    put<uint16_t>(slot_block(3), 0x9400, 1);
    put_seed(rando_copy(3));
    g_save_loaded = 0xFF;
    g_rando_marker = 1;
    std::memset(g_section, 0, sizeof(g_section));
    for (int i = 0; i < 32; ++i) game.watch_all(&no_callback);
    check(game.files().ap_slots() == std::set<int>{3} && game.files().counts().at(3) == 3,
          "the file select: the AP save's mark and its count");

    // ARCHIPELAGO's new game needs a client.
    zap::NewGamePlan plan;
    zap::parse_newgame("ap_newgame\nexpect 0x6C0 0300\nwrite 0x26C 2E1B", plan);
    check(!game.arm_newgame(plan) && game.newgame_status() != "armed", "no new game armed");
    for (int i = 0; i < 4; ++i) game.watch_all(&no_callback);
    check(!game.menu_installed(), "no ARCHIPELAGO choice");

    // The seed's save in play: dressed, the warning shown.
    put_seed(g_section);
    g_save_loaded = 3;
    g_menu = 0;
    g_flags[39] = 0;
    std::memset(g_map_grid, 0, sizeof(g_map_grid));
    for (int i = 0; i < 64; ++i) {
        game.watch_all(&no_callback);
        game.pump_popups();
    }
    check(game.intel_on() && game.assists_locked() && game.scan_line() == "COLLECT" &&
              game.disc_count() == 3 && !game.label_names().empty(),
          "the save dressed: INTEL CHIP, ASSIST locked, the disks, the labels");
    check(g_messages == 1 && game.queued_popups() == 0, "the not-connected warning shown");
    g_menu = 1;  // paused: the map's marks, the pause row
    for (int i = 0; i < 32; ++i) game.watch_all(&no_callback);
    g_menu = 0;

    // The client's parts refuse.
    game.note_play(1000);
    zap::Batch batch;
    zap::parse_batch("apply\ncas 1 3 4", batch);
    check(game.apply(batch) == "refused not configured" && g_inventory[1] == 3, "no grant");
    check(game.kill(5000) == "refused not configured", "no DeathLink death");
    check(game.spawn_shield(5000) == "refused not configured", "no Energy Shield");
    check(!game.request_mission(), "no RETURN HOME asked");
    for (int i = 0; i < 4; ++i) game.watch_all(&no_callback);
    check(game.mission_status() == "not asked" && g_fade_calls == 0, "none shown");
    {
        // A refused unload's state (as a client's would be) taken back: still no new game or
        // prompt.
        zap::ClientState state = game.snapshot();
        state.newgame_armed = true;
        state.newgame = plan;
        state.mission_asked = true;
        game.rearm(state);
        check(game.newgame_status() != "armed" && game.mission_status() == "not asked",
              "rearm sets neither");
    }

    // A client's short first connection going away, or an unload: everything put back (the
    // prompt's, the choice's and the map's too) with those members null, then dressed again.
    game.client_gone(&no_callback);
    check(game.configured() && !game.client_ready() && !game.intel_on() &&
              !game.menu_installed(),
          "put back, still the cache's config");
    for (int i = 0; i < 32; ++i) game.watch_all(&no_callback);
    check(game.intel_on() && game.assists_locked(), "and the save dressed again");

    // A client's config sets every key: its parts run.
    check(configure(game).empty() && game.client_ready() && game.left_out().empty(),
          "a client's config: every key");
    check(game.apply(batch) == "applied" && g_inventory[1] == 4, "a grant");
    game.put_back_all(&no_callback);
    g_save_loaded = 0xFF;
    g_rando_marker = 0;
}

}  // namespace

int main() {
    test_the_file();
    test_the_folder();
    test_the_cache_must_fit_the_game();
    test_the_helper_writes_what_the_client_sends();
    test_a_save_of_the_seed_dressed_offline();
    test_the_ap_saves_slots();
    test_the_logo_through_the_frame_call();
    test_a_cache_without_the_client_only_keys();
    return report("offline tests");
}
