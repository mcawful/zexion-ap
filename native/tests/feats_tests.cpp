// Host tests for Feats and achievements kept out of AP saves (feats.h): the AP-save test, the
// guard going on and off, the ending's patch run on a stand-in ending, Steam's import slot
// hooked, the stub's test while an AP save loads, the refusals, and putting both back. This
// program imports SteamInternal_ContextInit by name from a stand-in steam_api64.dll
// (steam_stub.cpp), as the game does, so the helper finds and hooks the real import slot of a
// real import.

#include <windows.h>

#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "game_calls.h"
#include "iat.h"
#include "offline.h"
#include "stand_in.h"

using namespace stand_in;

extern "C" __declspec(dllimport) void* SteamInternal_ContextInit(void* context);
extern "C" __declspec(dllimport) int stub_calls(int slot);

namespace {

constexpr int kSetAchievement = 0x30 / 8, kStoreStats = 0x48 / 8;

void** import_slot() {
    return zap::find_import_slot(GetModuleHandleW(nullptr), "steam_api64.dll",
                                 "SteamInternal_ContextInit");
}

void* steam_function() {
    return reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"steam_api64.dll"), "SteamInternal_ContextInit"));
}

void* slot_value() { return *static_cast<void* volatile*>(import_slot()); }

// Writes the import slot as another hook would.
void write_slot(void* value) {
    DWORD old = 0;
    VirtualProtect(import_slot(), sizeof(void*), PAGE_READWRITE, &old);
    *static_cast<void* volatile*>(import_slot()) = value;
    VirtualProtect(import_slot(), sizeof(void*), old, &old);
}

// An achievement call as the game makes one: the accessor through the import, then the
// interface's vtable slot (SetAchievement with a name, StoreStats without).
using Method = bool (*)(void* self, const char* name);
__declspec(noinline) bool call_stats(void* context, int slot, const char* name) {
    void* holder = SteamInternal_ContextInit(context);
    void* object = *static_cast<void**>(holder);
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<Method>(vtable[slot])(object, name);
}

// A randomizer save loaded whose section has an item table and start of its own (`salt`).
void load_save(uint8_t salt) {
    for (int i = 0; i < 154; ++i) g_section[0x26C + i] = static_cast<uint8_t>(i * 7 + salt);
    for (int i = 0; i < 6; ++i) g_section[0x310 + i] = static_cast<uint8_t>(salt + i);
    g_save_loaded = 3;
    g_rando_marker = 1;
}

zap::OfflineSeed seed_of_save() { return zap::offline_seed_of(g_section); }

// The 12 bytes at the stand-in ending's call, read through volatile (stand_in.h, write_code).
std::vector<unsigned char> call_bytes() {
    std::vector<unsigned char> out;
    for (size_t i = 0; i < zap::kEndingPatchSize; ++i) {
        out.push_back(code_byte(g_ending, static_cast<int>(kEndingCall + i)));
    }
    return out;
}

void reset() {
    reset_game();
    g_rando_marker = 0;
    g_flags[250] = 0;
    g_flags[251] = 0;
    build_ending(import_slot());
    if (slot_value() != steam_function()) write_slot(steam_function());
}

// The stand-in game, with the import slot this program calls Steam through.
std::map<std::string, uint64_t> feats_config() {
    std::map<std::string, uint64_t> config = stand_in_config();
    config["context_init_slot"] = rva(import_slot());
    return config;
}

std::string configure_feats(zap::Game& game, bool active = true) {
    return game.configure(GetModuleHandleW(nullptr), feats_config(), active);
}

// An AP save of a seed the game knows, loaded and watched over two frame calls: the guard on.
void guard_on(zap::Game& game, uint8_t salt = 1) {
    load_save(salt);
    game.add_known_seeds({seed_of_save()});
    game.watch_feats();
    game.watch_feats();
}

// A normal save loaded and watched: the guard off, the import slot back to steam_api64's.
void guard_off(zap::Game& game) {
    g_rando_marker = 0;
    game.watch_feats();
}

// Frame calls enough for the Feats of Natu screen's look (every 8th).
constexpr int kNoteLook = 8;
void note_looks(zap::Game& game) {
    for (int i = 0; i < kNoteLook; ++i) game.watch_feats();
}

void test_the_ap_save_test() {
    reset();
    zap::Game game;
    load_save(1);
    check(!game.ap_save_loaded(), "nothing configured: no AP save");
    check(configure_feats(game).empty(), "the stand-in configures");
    check(!game.ap_save_loaded(), "a randomizer save of no known seed (a plain one) is not one");
    game.add_known_seeds({seed_of_save()});
    check(game.ap_save_loaded(), "a seed a client put is");
    g_rando_marker = 0;
    check(!game.ap_save_loaded(), "a normal save (the section still the last game's) is not");
    g_rando_marker = 1;
    g_save_loaded = 0xFF;
    check(!game.ap_save_loaded(), "with no save loaded, nothing is");
    load_save(2);
    check(!game.ap_save_loaded(), "another seed's section is not");
    game.add_known_seeds({seed_of_save(), seed_of_save()});
    check(game.known_seeds() == 2, "each seed known once");

    // Offline: a seed file read from the folder, with no client.
    zap::Game offline;
    check(configure_feats(offline, false).empty(), "configured from the cache");
    load_save(3);
    offline.set_offline({seed_of_save()});
    check(offline.ap_save_loaded(), "a seed file's save, with no client");
    // A client of another seed: the save's own seed known from its file still counts.
    load_save(4);
    offline.add_known_seeds({seed_of_save()});  // the client's own
    check(configure_feats(offline).empty(), "a client configures");
    load_save(3);
    check(offline.ap_save_loaded(), "a seed file's save, with a client of another seed");

    // The seed the labels were set for (a client sends them while its save is loaded).
    zap::Game labeled;
    check(configure_feats(labeled).empty(), "configured");
    load_save(5);
    std::map<int, zap::Label> labels;
    check(zap::parse_labels("ap_labels\nlabel 5 2 41", labels).empty(), "a label parses");
    labeled.set_labels(labels);
    check(labeled.ap_save_loaded(), "the labels' seed's save is one");
    load_save(6);
    check(!labeled.ap_save_loaded(), "and only it");
}

void test_on_and_off() {
    reset();
    zap::Game game;
    check(configure_feats(game).empty(), "configured");
    load_save(1);
    game.add_known_seeds({seed_of_save()});
    game.watch_feats();
    check(!game.feats().on() && !game.feats().ending_patched() && !game.feats().steam_hooked(),
          "one frame call is not enough: during a load the section may be the last save's");
    game.watch_feats();
    check(game.feats().on() && game.feats_state() == "on", "on at the second", game.feats_state());
    check(game.feats().ending_patched() && game.feats().steam_hooked(), "both changes made");
    check(game.unload_blocker() != nullptr &&
              std::string(game.unload_blocker()) == "the ending's patch is still the helper's",
          "an unload waits for a frame to put the ending back");
    g_rando_marker = 0;  // a normal save
    game.watch_feats();
    check(!game.feats().on() && game.feats_state() == "off", "off at once", game.feats_state());
    check(!game.feats().ending_patched() && !game.feats().steam_hooked(), "both put back");
    check(slot_value() == steam_function(), "the import slot is steam_api64's again");
    check(game.unload_blocker() == nullptr, "nothing to wait for");
}

void test_the_ending() {
    reset();
    const std::vector<unsigned char> original = call_bytes();
    // Off: the ending as the game runs it, its Steam call through the import.
    int set_before = stub_calls(kSetAchievement);
    run_ending();
    check(g_flags[250] == 1 && g_flags[251] == 1 && stub_calls(kSetAchievement) == set_before + 1,
          "off: the ending sets 14Victory, then flags 251 and 250");

    zap::Game game;
    check(configure_feats(game).empty(), "configured");
    guard_on(game);
    check(game.feats().ending_patched(), "on: patched", game.feats_state());
    const unsigned char* site = g_ending + kEndingCall;
    const std::vector<unsigned char> patch = call_bytes();
    check(patch[0] == 0xC6 && patch[1] == 0x05 && patch[6] == 0 && patch[7] == 0xE9,
          "as `mov byte [flag 251], 0; jmp join`");
    check(site + 7 + get<int32_t>(patch.data(), 2) == g_flags + 251, "the mov clears flag 251");
    check(site + 12 + get<int32_t>(patch.data(), 8) == g_ending + kEndingJoin,
          "the jmp lands on the join");
    // A save that already holds flag 251 (an ending before the guard).
    g_flags[250] = 0;
    g_flags[251] = 1;
    set_before = stub_calls(kSetAchievement);
    run_ending();
    check(g_flags[250] == 1, "on: flag 250 (the client's goal) is still set");
    check(g_flags[251] == 0, "on: flag 251 is cleared");
    check(stub_calls(kSetAchievement) == set_before, "on: no achievement");
    // Run twice: the routine runs once per ending, but nothing in it depends on that.
    run_ending();
    check(g_flags[251] == 0 && stub_calls(kSetAchievement) == set_before, "again the same");

    g_save_loaded = 0xFF;  // MISSION COMPLETE and the title: no save loaded
    game.watch_feats();
    check(!game.feats().ending_patched() && call_bytes() == original,
          "off: the game's bytes back");
    g_save_loaded = 3;
    g_rando_marker = 0;  // a normal save
    g_flags[250] = 0;
    g_flags[251] = 0;
    run_ending();
    check(g_flags[250] == 1 && g_flags[251] == 1 && stub_calls(kSetAchievement) == set_before + 1,
          "a normal save's ending as before");
}

void test_steam() {
    reset();
    zap::Game game;
    check(configure_feats(game).empty(), "configured");
    guard_on(game);
    check(game.feats().steam_hooked() && slot_value() == game.feats().stub(),
          "on: the import slot holds the stub");
    int set_before = stub_calls(kSetAchievement), store_before = stub_calls(kStoreStats);
    check(!call_stats(g_stats_context, kSetAchievement, "02MachineGun"),
          "on: SetAchievement on the stats context reaches the stand-in (0)");
    check(!call_stats(g_stats_context, kStoreStats, nullptr), "and StoreStats");
    check(stub_calls(kSetAchievement) == set_before && stub_calls(kStoreStats) == store_before,
          "Steam sees neither");
    check(SteamInternal_ContextInit(g_stats_context) == game.feats().stand_in(),
          "the accessor returns the stand-in's holder");
    check(call_stats(g_utils_context, kSetAchievement, "x") &&
              stub_calls(kSetAchievement) == set_before + 1,
          "on: another context passes through to steam_api64");
    g_rando_marker = 0;  // a normal save
    game.watch_feats();
    check(slot_value() == steam_function(), "off: the slot is steam_api64's");
    check(call_stats(g_stats_context, kSetAchievement, "02MachineGun") &&
              stub_calls(kSetAchievement) == set_before + 2,
          "off: the stats context reaches Steam");
}

// Save slot `n` (1 to 30) in the slot array, and its copy of the randomizer section.
BYTE* slot_block(int n) { return g_slot_array + (n - 1) * 0x9408; }
BYTE* rando_copy(int n) { return g_rando_slots + (n - 1) * 0x908; }
// Every slot empty, then slot `n` a randomizer save whose copy is section `salt`'s.
void clear_slots() {
    std::memset(g_slot_array, 0, sizeof(g_slot_array));
    std::memset(g_rando_slots, 0, sizeof(g_rando_slots));
    for (int n = 1; n <= 30; ++n) slot_block(n)[0x935E] = 0xFF;
}
void put_slot(int n, uint8_t salt) {
    load_save(salt);
    slot_block(n)[0x935E] = 0;
    put<uint16_t>(slot_block(n), 0x9400, 1);
    std::memcpy(rando_copy(n), g_section, 0x908);
}

// The load routine sets current_slot's save loaded and runs the 15Explorer check in one game
// call, before any frame call (feats.h): the stub blocks then if that slot holds an AP save.
void test_a_load_from_the_file_select() {
    reset();
    clear_slots();
    put_slot(3, 1);  // an AP save
    zap::Game game;
    check(configure_feats(game).empty(), "configured");
    game.add_known_seeds({seed_of_save()});
    put_slot(5, 2);  // a plain randomizer save
    g_rando_marker = 0;
    g_save_loaded = 0xFF;  // the file select
    game.watch_feats();
    check(!game.feats().on() && game.feats().steam_hooked() && game.feats().load_test() &&
              slot_value() == game.feats().stub() && game.feats_state() == "off",
          "off, but hooked for the load test while a slot holds an AP save");

    int set_before = stub_calls(kSetAchievement), store_before = stub_calls(kStoreStats);
    // The load of slot 3, up to its tail call: current_slot, the marker, save_loaded.
    g_current_slot = 2;
    g_rando_marker = 1;
    g_save_loaded = 0;
    check(!call_stats(g_stats_context, kSetAchievement, "15Explorer") &&
              !call_stats(g_stats_context, kStoreStats, nullptr) &&
              stub_calls(kSetAchievement) == set_before && stub_calls(kStoreStats) == store_before,
          "loading the AP save: 15Explorer and StoreStats reach the stand-in");
    check(call_stats(g_utils_context, kSetAchievement, "x") &&
              stub_calls(kSetAchievement) == set_before + 1,
          "another context passes through");
    g_current_slot = 4;  // slot 5, the plain one
    check(call_stats(g_stats_context, kSetAchievement, "15Explorer") &&
              stub_calls(kSetAchievement) == set_before + 2,
          "loading another slot: Steam sees it");
    g_current_slot = 2;
    g_rando_marker = 0;  // a normal save in it (marker 0)
    check(call_stats(g_stats_context, kSetAchievement, "15Explorer") &&
              stub_calls(kSetAchievement) == set_before + 3,
          "the marker not set: Steam sees it");
    g_rando_marker = 1;
    for (int32_t slot : {-1, 30, 1000}) {
        g_current_slot = slot;
        check(call_stats(g_stats_context, kSetAchievement, "x"), "a slot out of range passes");
    }
    check(stub_calls(kSetAchievement) == set_before + 6, "all three to Steam");

    // The AP save loaded: the guard on after two frame calls, then the title again.
    g_current_slot = 2;
    std::memcpy(g_section, rando_copy(3), 0x908);
    game.watch_feats();
    game.watch_feats();
    check(game.feats().on() && game.feats().load_test(), "in play: on");
    g_save_loaded = 0xFF;
    game.watch_feats();
    check(!game.feats().on() && game.feats().steam_hooked() && game.feats().load_test(),
          "back at the title: off, the load test kept");

    // The plain save loaded: the slots kept for one frame call, dropped at the second.
    g_current_slot = 4;
    std::memcpy(g_section, rando_copy(5), 0x908);
    g_save_loaded = 0;
    game.watch_feats();
    check(game.feats().load_test(), "one frame call: kept, the section may be the last save's");
    game.watch_feats();
    check(!game.feats().steam_hooked() && !game.feats().load_test() &&
              slot_value() == steam_function(),
          "a save that is not an AP save: the slot back to steam_api64's");
    g_current_slot = 2;
    std::memcpy(g_section, rando_copy(3), 0x908);
    g_rando_marker = 1;
    set_before = stub_calls(kSetAchievement);
    g_save_loaded = 0xFF;
    game.watch_feats();
    check(game.feats().load_test(), "at the title again: armed at once");

    // From the pipe thread once no frame can watch (an unload): the test off with the slot.
    void* stub = game.feats().stub();
    game.release_steam();
    auto stub_fn = reinterpret_cast<void* (*)(void*)>(stub);
    check(slot_value() == steam_function() && !game.feats().load_test() &&
              stub_fn(g_stats_context) == &g_stats_context[2],
          "release_steam: the slot back, and the stub passes a load's call through");
    game.watch_feats();
    check(game.feats().load_test(), "a refused unload: armed again");
    game.restore_feats();
    check(!game.feats().steam_hooked() && !game.feats().load_test() &&
              slot_value() == steam_function(),
          "restore_feats: both off");
    game.watch_feats();
    check(game.feats().load_test(), "and set again by the next frame call");
    game.restore_feats();

    // An offline cache without current_slot (a client-only key): no load test, nothing hooked.
    zap::Game offline;
    auto config = feats_config();
    config.erase("current_slot");
    check(offline.configure(GetModuleHandleW(nullptr), config, false).empty(),
          "configured from a cache without current_slot");
    offline.set_offline({zap::offline_seed_of(rando_copy(3))});
    offline.watch_feats();
    check(!offline.feats().steam_hooked() && !offline.feats().load_test(),
          "without current_slot: not hooked at the file select");
    offline.restore_feats();
    clear_slots();
    g_current_slot = 0;
    g_rando_marker = 0;
    g_save_loaded = 3;
}

void test_refusals() {
    // The stand-in's own config: a slot that is not the import's. Neither change is made.
    reset();
    zap::Game plain;
    check(configure(plain).empty(), "configured with a slot that is no import");
    guard_on(plain);
    check(plain.feats().on() && !plain.feats().steam_hooked() && !plain.feats().ending_patched(),
          "on, but nothing changed");
    const std::string why = plain.feats_state();
    check(why.find("the ending's call reads otherwise") != std::string::npos &&
              why.find("the slot is not steam_api64's SteamInternal_ContextInit") !=
                  std::string::npos,
          "and hello says why", why);

    // A context that asks for another interface.
    reset();
    zap::Game other;
    auto config = feats_config();
    config["stats_context"] = rva(g_utils_context);
    check(other.configure(GetModuleHandleW(nullptr), config).empty(), "configured");
    guard_on(other);
    check(!other.feats().steam_hooked() &&
              other.feats_state().find("not the user stats'") != std::string::npos,
          "another context is not hooked", other.feats_state());
    check(other.feats().ending_patched(), "the ending is, on its own checks");
    guard_off(other);

    // An ending that reads otherwise at its join: nothing written.
    reset();
    const std::vector<unsigned char> before = call_bytes();
    set_code_byte(g_ending, kEndingJoin + 1, 0x8B);  // not a movsxd
    zap::Game join;
    check(configure_feats(join).empty(), "configured");
    guard_on(join);
    check(!join.feats().ending_patched() &&
              call_bytes() == before &&
              join.feats_state().find("join reads otherwise") != std::string::npos,
          "a join that reads otherwise: the ending left alone", join.feats_state());
    check(join.feats().steam_hooked(), "Steam still hooked");
    guard_off(join);

    // An import slot something else has hooked: left to it.
    reset();
    void* other_hook = reinterpret_cast<void*>(&stub_calls);
    write_slot(other_hook);
    zap::Game hooked;
    check(configure_feats(hooked).empty(), "configured");
    guard_on(hooked);
    check(!hooked.feats().steam_hooked() && slot_value() == other_hook &&
              hooked.feats_state().find("does not hold steam_api64's") != std::string::npos,
          "another hook in the slot: not replaced", hooked.feats_state());
    write_slot(steam_function());
    hooked.watch_feats();
    check(hooked.feats().steam_hooked(), "hooked once it lets go");
    guard_off(hooked);
}

// The Feats of Natu screen as read live on B1 (feats.h): 4 panels, two rows of 7 Feats, and the
// title label, three lines of 38 glyphs with the title on the middle one.
struct FeatsScreen {
    static constexpr int kColumns = 38, kLength = 3 * kColumns;
    BYTE menu[0x80] = {};
    BYTE* panels[4] = {};
    BYTE panel[4][0x300] = {};
    BYTE* children[1] = {};
    BYTE title[0x2D0] = {};
    BYTE label[0x230] = {};
    BYTE* glyph_list[kLength] = {};
    BYTE glyphs[kLength][0x1D8] = {};
    std::vector<BYTE> table = std::vector<BYTE>(256 * 0x38);
    // MISSION COMPLETE's statistics, the menu under it: 1 panel of 10 lines.
    BYTE stats[0x80] = {};
    BYTE* stats_panels[1] = {};
    BYTE stats_panel[0x300] = {};

    explicit FeatsScreen(const std::string& middle = "FEATS OF NATU!") {
        at<BYTE*>(menu, 0) = reinterpret_cast<BYTE*>(panels);
        at<int32_t>(menu, 8) = 4;
        at<BYTE*>(menu, 0x78) = stats;
        at<BYTE*>(stats, 0) = reinterpret_cast<BYTE*>(stats_panels);
        at<int32_t>(stats, 8) = 1;
        stats_panels[0] = stats_panel;
        at<int32_t>(stats_panel, 0x1E8) = 10;
        for (int i = 0; i < 4; ++i) panels[i] = panel[i];
        at<BYTE**>(panel[0], 0x1E0) = children;
        at<int32_t>(panel[0], 0x1E8) = 1;
        at<int32_t>(panel[1], 0x1E8) = 1;  // EXIT
        at<int32_t>(panel[2], 0x1E8) = 7;
        at<int32_t>(panel[3], 0x1E8) = 7;
        children[0] = title;
        at<void*>(title, 0x2C8) = reinterpret_cast<void*>(&widget_handler_fn);
        at<BYTE*>(title, 0x1F0) = label;
        at<int32_t>(label, 0x1D8) = kLength;
        at<int32_t>(label, 0x1DC) = kColumns;
        at<BYTE**>(label, 0x228) = glyph_list;
        const size_t left = (kColumns - middle.size()) / 2;
        for (int i = 0; i < kLength; ++i) {
            glyph_list[i] = glyphs[i];
            at<BYTE*>(glyphs[i], 0x1A8) = table.data();
            at<int32_t>(glyphs[i], 0x1BC) = 256;
            int32_t f = i < 2 * kColumns ? ' ' : 0;  // the first two lines spaces, the last blank
            const size_t col = static_cast<size_t>(i % kColumns);
            if (i / kColumns == 1 && col >= left && col < left + middle.size()) f = middle[col - left];
            at<int32_t>(glyphs[i], 0x1C4) = f;
        }
    }

    // Line n's frames as text, '.' for blank (frame 0).
    std::string line(int n) const {
        std::string out;
        for (int i = n * kColumns; i < (n + 1) * kColumns; ++i) {
            int32_t f = get<int32_t>(glyphs[i], 0x1C4);
            out += f == 0 ? '.' : static_cast<char>(f);
        }
        return out;
    }
};

std::string noted_line() {
    const std::string note = zap::kFeatsNote;
    const size_t left = (FeatsScreen::kColumns - note.size() + 1) / 2;
    std::string out(FeatsScreen::kColumns, '.');
    out.replace(left, note.size(), note);
    return out;
}

void test_the_screen_note() {
    const std::string blank(FeatsScreen::kColumns, '.');
    FeatsScreen screen;
    BYTE* current = screen.menu;
    zap::GameAddresses a;
    a.current_menu = reinterpret_cast<BYTE*>(&current);
    a.widget_handler_fn = reinterpret_cast<BYTE*>(&widget_handler_fn);
    check(zap::feats_screen_note(a, true) && screen.line(2) == noted_line(),
          "the note under FEATS OF NATU!, centered", screen.line(2));
    check(screen.line(1).find("FEATS OF NATU!") != std::string::npos, "the title left as it is");
    check(zap::feats_screen_note(a, true) && screen.line(2) == noted_line(), "written once");
    check(zap::feats_screen_note(a, false) && screen.line(2) == blank, "and blanked again");

    // The game's own text on the last line: never written over.
    at<int32_t>(screen.glyphs[2 * FeatsScreen::kColumns + 3], 0x1C4) = 'X';
    const std::string theirs = screen.line(2);
    check(zap::feats_screen_note(a, true) && screen.line(2) == theirs, "other text left alone");

    // Other screens: not this one.
    FeatsScreen other("OPTIONS");
    current = other.menu;
    check(!zap::feats_screen_note(a, true) && other.line(2) == blank, "a title without NATU");
    FeatsScreen six;
    at<int32_t>(six.panel[3], 0x1E8) = 6;
    current = six.menu;
    check(!zap::feats_screen_note(a, true) && six.line(2) == blank, "a row of 6");
    FeatsScreen alone;
    at<BYTE*>(alone.menu, 0x78) = nullptr;
    current = alone.menu;
    check(!zap::feats_screen_note(a, true) && alone.line(2) == blank,
          "not after an ending (no MISSION COMPLETE under it)");
    FeatsScreen few;
    at<int32_t>(few.glyphs[2 * FeatsScreen::kColumns + 5], 0x1BC) = 64;
    current = few.menu;
    check(zap::feats_screen_note(a, true) && few.line(2) == blank,
          "a glyph that cannot take the note: the line left whole");
    FeatsScreen released;
    at<int32_t>(released.title, 0x1C8) = 1;
    current = released.menu;
    check(!zap::feats_screen_note(a, true) && released.line(2) == blank, "a released title");
    current = nullptr;
    check(!zap::feats_screen_note(a, true), "no menu");
}

// The note after an AP save's ending, through Game: the guard on, then MISSION COMPLETE (no
// save loaded, flag 250 set), then the screen.
void test_the_note_after_an_ending() {
    reset();
    const std::string blank(FeatsScreen::kColumns, '.');
    zap::Game game;
    check(configure_feats(game).empty(), "configured");
    guard_on(game);
    g_flags[250] = 1;  // the ending ran
    g_save_loaded = 0xFF;
    FeatsScreen screen;
    g_current_menu = screen.menu;
    note_looks(game);
    check(game.feats_ended() && screen.line(2) == noted_line(), "an AP ending: the note",
          screen.line(2));
    game.restore_feats();
    check(screen.line(2) == blank, "blanked before an unload");
    note_looks(game);
    check(screen.line(2) == noted_line(), "and back after a refused one");

    // A save loaded ends it.
    g_save_loaded = 3;
    guard_off(game);
    check(!game.feats_ended(), "a save loaded ends it");
    g_current_menu = nullptr;

    // A normal save's ending: the guard never on, no note.
    zap::Game normal;
    check(configure_feats(normal).empty(), "configured");
    g_save_loaded = 3;
    g_rando_marker = 0;
    for (int i = 0; i < 3; ++i) normal.watch_feats();
    g_save_loaded = 0xFF;
    g_flags[250] = 1;
    FeatsScreen plain;
    g_current_menu = plain.menu;
    note_looks(normal);
    check(!normal.feats_ended() && plain.line(2) == blank, "a normal ending: no note");
    // An AP save left with no ending (flag 250 clear): no note either.
    zap::Game left;
    check(configure_feats(left).empty(), "configured");
    g_flags[250] = 0;
    g_current_menu = nullptr;
    guard_on(left);
    g_save_loaded = 0xFF;
    g_current_menu = plain.menu;
    note_looks(left);
    check(!left.feats_ended() && plain.line(2) == blank, "an AP save left before its ending");
    g_current_menu = nullptr;

    // The guard on but the patch refused (the ending's bytes not as traced): the ending ran as the
    // game's, so no note.
    reset();
    set_code_byte(g_ending, kEndingJoin + 1, 0x8B);
    zap::Game refused;
    check(configure_feats(refused).empty(), "configured");
    guard_on(refused);
    check(refused.feats().on() && !refused.feats().ending_patched(), "on, the patch refused");
    g_flags[250] = 1;
    g_flags[251] = 1;
    g_save_loaded = 0xFF;
    FeatsScreen feats_earned;
    g_current_menu = feats_earned.menu;
    note_looks(refused);
    check(!refused.feats_ended() && feats_earned.line(2) == blank,
          "a refused patch: no note over the Feats earned");
    g_current_menu = nullptr;

    // The screen seen, then gone: the note's turn is over.
    reset();
    zap::Game seen;
    check(configure_feats(seen).empty(), "configured");
    guard_on(seen);
    g_flags[250] = 1;
    g_save_loaded = 0xFF;
    FeatsScreen once;
    g_current_menu = once.menu;
    note_looks(seen);
    check(seen.feats_ended() && once.line(2) == noted_line(), "the note");
    g_current_menu = nullptr;
    note_looks(seen);
    check(!seen.feats_ended(), "the screen gone: over");
    FeatsScreen later;
    g_current_menu = later.menu;
    note_looks(seen);
    check(later.line(2) == blank, "a later Feats screen gets none");
    g_current_menu = nullptr;
    g_save_loaded = 3;
}

void test_put_back() {
    reset();
    const std::vector<unsigned char> original = call_bytes();
    zap::Game game;
    check(configure_feats(game).empty(), "configured");
    guard_on(game);

    // Configured again with the same addresses (a client reconnecting): left as it is.
    check(configure_feats(game).empty() && game.feats().ending_patched() &&
              game.feats().steam_hooked(),
          "a config with the same addresses keeps both");

    // Before an unload: both back, in the frame call.
    game.restore_feats();
    check(!game.feats().ending_patched() && !game.feats().steam_hooked() &&
              call_bytes() == original &&
              slot_value() == steam_function() && game.unload_blocker() == nullptr,
          "restore_feats puts both back");
    // A refused unload: the next frame calls set them again.
    game.watch_feats();
    game.watch_feats();
    check(game.feats().ending_patched() && game.feats().steam_hooked(), "and they come back");

    // An unload no frame came for: Steam's slot back from the pipe thread, the stub kept.
    void* stub = game.feats().stub();
    game.release_steam();
    check(slot_value() == steam_function() && !game.feats().steam_hooked(),
          "release_steam puts the slot back");
    auto stub_fn = reinterpret_cast<void* (*)(void*)>(stub);
    check(stub_fn(g_stats_context) == &g_stats_context[2],
          "a call that read the stub before then passes through");
    // The stand-in a call may still hold: its methods still answer 0.
    void* object = *static_cast<void**>(game.feats().stand_in());
    void** vtable = *static_cast<void***>(object);
    check(!reinterpret_cast<Method>(vtable[kSetAchievement])(object, "x") &&
              !reinterpret_cast<Method>(vtable[kStoreStats])(object, nullptr),
          "the stand-in stays, answering 0");

    // Something hooked over the stub: the slot is left to it, and the stub passes through.
    game.watch_feats();
    check(game.feats().steam_hooked(), "hooked again");
    void* over = reinterpret_cast<void*>(&stub_calls);
    write_slot(over);
    game.restore_feats();
    check(slot_value() == over, "a hook over the stub is not undone");
    check(stub_fn(g_stats_context) == &g_stats_context[2], "the stub passes through");
    write_slot(steam_function());
    guard_off(game);
}

}  // namespace

int main() {
    if (import_slot() == nullptr || steam_function() == nullptr) {
        std::printf("FAIL: the stand-in steam_api64.dll is not imported\n");
        return EXIT_FAILURE;
    }
    test_the_ap_save_test();
    test_on_and_off();
    test_the_ending();
    test_steam();
    test_a_load_from_the_file_select();
    test_refusals();
    test_the_screen_note();
    test_the_note_after_an_ending();
    test_put_back();
    return report("feats tests");
}
