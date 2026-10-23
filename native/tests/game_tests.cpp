// Host tests for helper phase 2: the mailbox between the pipe thread and the frame call, the
// request parsers, and Game against a stand-in "game" made of this program's own data and
// functions (so configure's checks and every call are real).

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>

#include "game_calls.h"
#include "mailbox.h"
#include "stand_in.h"
#include "stand_in_menu.h"

namespace {

using namespace stand_in;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

std::string apply(zap::Game& game, const std::string& request) {
    zap::Batch batch;
    std::string error = zap::parse_batch(request, batch);
    return error.empty() ? game.apply(batch) : "parse: " + error;
}

// --- tests ------------------------------------------------------------------------------------

void test_parsers() {
    std::map<std::string, uint64_t> rvas;
    check(zap::parse_config("config a=0x10 b=32", rvas).empty() && rvas["a"] == 16 &&
              rvas["b"] == 32,
          "config takes hex and decimal");
    check(!zap::parse_config("config a=zz", rvas).empty(), "config refuses a bad number");
    check(!zap::parse_config("hello", rvas).empty(), "config refuses another request");

    zap::Batch batch;
    check(zap::parse_batch("apply\ncas 36 0 1\nrefresh\npopup 36 from Alice ü!", batch).empty(),
          "parses a batch");
    check(batch.cas.size() == 1 && batch.cas[0].item == 36 && batch.cas[0].value == 1, "cas");
    check(batch.refresh, "refresh");
    check(batch.popups.size() == 1 && batch.popups[0].suffix == "FROM ALICE !",
          "the suffix is upper-cased and filtered to the font");
    zap::Batch bad;
    check(!zap::parse_batch("apply\ncas 50 0 1", bad).empty(), "item 50 is not writable");
    check(!zap::parse_batch("apply\ncas 3 0 256", bad).empty(), "values are bytes");
    check(!zap::parse_batch("apply\nwrite 3 0 1", bad).empty(), "unknown lines are refused");
    check(!zap::parse_batch("apply\npopup 51 X", bad).empty(), "popup items are 0..50");
    zap::Batch longer;
    zap::parse_batch("apply\npopup 1 " + std::string(100, 'a'), longer);
    check(longer.popups[0].suffix.size() == 40, "the suffix is capped");
}

// The RVA of the start of the section holding `p` in this program.
uint64_t section_start_of(const void* p) {
    auto* base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);
    uint64_t target = rva(p);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        uint64_t size = (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
        if (target >= section->VirtualAddress && target < section->VirtualAddress + size) {
            return section->VirtualAddress;
        }
    }
    return 0;
}

extern const BYTE kReadOnly;
const BYTE kReadOnly = 1;  // in read-only data

void test_configure() {
    zap::Game game;
    HMODULE self = GetModuleHandleW(nullptr);
    auto config = stand_in_config();
    auto missing = config;
    missing.erase("sound_fn");
    check(game.configure(self, missing) == "missing sound_fn", "every key is required");
    auto swapped = config;
    swapped["message_fn"] = config["inventory"];
    check(game.configure(self, swapped).find("not in code") != std::string::npos,
          "a function must be in code");
    swapped = config;
    swapped["inventory"] = config["message_fn"];
    check(game.configure(self, swapped).find("in code") != std::string::npos,
          "data must not be in code");
    swapped = config;
    swapped["menu_state"] = 0x7FFFFFFF;
    check(game.configure(self, swapped).find("outside") != std::string::npos,
          "an address outside the image is refused");
    swapped = config;
    swapped["menu_state"] = rva(&kReadOnly);
    check(game.configure(self, swapped).find("read-only") != std::string::npos,
          "data must be writable");
    swapped = config;
    swapped["inventory"] = section_start_of(g_player) + 0x10;  // no player state fits before it
    check(!game.configure(self, swapped).empty(), "the player state must fit in its section");
    check(!game.configured(), "a refused configuration leaves it unconfigured");
    check(game.configure(self, config).empty() && game.configured(), "configures");
}

void test_apply() {
    zap::Game game;
    check(apply(game, "apply\ncas 36 0 1") == "refused not configured", "needs configuring");
    game.configure(GetModuleHandleW(nullptr), stand_in_config());

    reset_game();
    g_save_loaded = 0xFF;
    check(apply(game, "apply\ncas 36 0 1") == "refused not in play", "no save loaded");
    reset_game();
    g_menu = 2;
    check(apply(game, "apply\ncas 36 0 1") == "refused not in play", "a menu is open");
    reset_game();
    field<int16_t>(0xCEA) = 0;
    check(apply(game, "apply\ncas 36 0 1") == "refused not in play", "dead");
    check(g_inventory[36] == 0, "nothing written when refused");

    reset_game();
    check(apply(game, "apply\ncas 17 0 1\ncas 36 1 2") == "refused item 36 holds 0",
          "a value that differs refuses the whole batch");
    check(g_inventory[17] == 0, "all or nothing: the first write was not made");

    reset_game();
    check(apply(game, "apply\ncas 36 0 1\npopup 36 FROM ALICE") == "applied", "applies");
    check(g_inventory[36] == 1, "the byte is written");
    check(field<int16_t>(0xCC0) == 13 && field<int16_t>(0xCBC) == 12,
          "a Missile Box raises max and current missiles by 10");
    check(g_hud_values[0] == 12.0, "the missile number is refreshed with the current value");
    check(g_calls == "number number layout energy ", "the HUD refresh, without the strip");
    check(g_energy_force == 1, "the energy HUD is forced, as after a load");
    check(game.queued_popups() == 1, "the popup is queued, not shown inside apply");

    reset_game();
    g_strip_gate = 1;
    field<int16_t>(0xA4C) = 0x78;
    apply(game, "apply\ncas 2 0 1\ncas 0 3 4");
    check(field<int16_t>(0xCC2) == 1 && field<int16_t>(0xCBE) == 1, "a Mega Bomb: max and one bomb");
    check(field<int16_t>(0xCEC) == 64 && field<int16_t>(0xCEA) == 64, "a tank: max and a full heal");
    check(g_calls.find("strip") != std::string::npos, "the strip is redrawn while it shows");

    reset_game();
    apply(game, "apply\ncas 25 0 1");
    check(g_calls.find("suit") != std::string::npos && g_palette == 1,
          "Magnus Armor gained: the suit color is set");
    reset_game();
    g_inventory[25] = 1;
    apply(game, "apply\ncas 4 0 1");
    check(g_calls.find("suit") == std::string::npos, "armor already owned: no suit call");

    reset_game();
    check(apply(game, "apply\nrefresh") == "applied" && g_hud_numbers == 2,
          "refresh alone refreshes the HUD");
    check(g_inventory[1] == 3 && field<int16_t>(0xCC0) == 3, "and writes no item");
    reset_game();
    g_inventory[36] = 1;  // a Missile Box the game has not counted into max missiles yet
    apply(game, "apply\nrefresh");
    check(field<int16_t>(0xCC0) == 13 && field<int16_t>(0xCBC) == 12,
          "refresh brings max and current missiles in line with the counts");
}

void test_popups() {
    zap::Game game;
    game.configure(GetModuleHandleW(nullptr), stand_in_config());
    reset_game();
    apply(game, "apply\npopup 36 FROM ALICE\npopup 36 FROM BOB");
    game.pump_popups();
    check(g_messages == 1, "the first popup shows at once");
    check(g_text == std::string(6, ' ') + "\xC5MISSILE BOX" + std::string(13, ' ') + "FROM ALICE",
          "icon, the game's name, then the sender, each line centered");
    check(g_message_args[0] == 0x18 && g_message_args[1] == 0xA0 && g_message_args[2] == 0x58 &&
              g_message_args[3] == 0xB4,
          "the pickup popup's arguments");
    check(g_sound == 61 && g_sound_volume == 1.0f && g_sound_pitch == -1.0f &&
              g_sound_category == 8 && g_sound_x == 120.5f && g_sound_y == 64.0f &&
              g_sound_system_seen == g_sound_system,
          "the item's own sound at the player");
    // The game reports the popup still showing: the next waits.
    g_last_message.last = g_message;
    g_last_message.sequence = 7;
    *reinterpret_cast<int32_t*>(g_message + 0x1D8) = 7;
    *reinterpret_cast<int32_t*>(g_message + 0x1EC) = 100;
    for (int i = 0; i < 50; ++i) game.pump_popups();
    check(g_messages == 1 && game.queued_popups() == 1, "waits while the last popup shows");
    *reinterpret_cast<int32_t*>(g_message + 0x1EC) = 1;
    game.pump_popups();
    check(g_messages == 2 &&
              g_text == std::string(6, ' ') + "\xC5MISSILE BOX" + std::string(14, ' ') + "FROM BOB",
          "then shows the next");

    reset_game();
    apply(game, "apply\npopup 36 X");
    g_message_fails = true;
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(game.queued_popups() == 1 && g_sound == -1, "a popup the game refused stays queued");
    g_message_fails = false;
    g_menu = 2;
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(game.queued_popups() == 1, "no popups outside play");
    g_menu = 0;
    game.pump_popups();
    check(game.queued_popups() == 0, "shown once back in play");
}

void queue_new_game(uint16_t marker, int32_t timer) {
    g_transition_fn = reinterpret_cast<void*>(&newgame_fn);
    g_transition_timer = timer;
    g_rando_marker = marker;
    g_current_slot = 6;
}

void test_newgame_parser() {
    zap::NewGamePlan plan;
    check(zap::parse_newgame("ap_newgame\nexpect 0x6C0 0300\nwrite 620 2E2E\nwrite 0x310 010203040506",
                             plan)
                  .empty(),
          "parses a new-game plan");
    check(plan.expect.size() == 1 && plan.expect[0].offset == 0x6C0 && plan.expect[0].bytes.size() == 2,
          "expect");
    check(plan.write.size() == 2 && plan.write[0].offset == 620 && plan.write[1].bytes[5] == 6,
          "writes, decimal and hex offsets");
    zap::NewGamePlan bad;
    check(!zap::parse_newgame("ap_newgame\nwrite 0x906 112233", bad).empty(),
          "a write past the section is refused");
    check(!zap::parse_newgame("ap_newgame\nwrite 0x10 123", bad).empty(), "odd hex is refused");
    check(!zap::parse_newgame("ap_newgame\nexpect 0x10 00", bad).empty(), "nothing to write");
    check(!zap::parse_newgame("ap_newgame\npoke 0x10 00", bad).empty(), "unknown lines");
}

void test_newgame_watch() {
    zap::Game game;
    game.configure(GetModuleHandleW(nullptr), stand_in_config());
    reset_game();
    std::memset(g_section, 0, sizeof(g_section));
    g_section[0x6C0] = 3;  // the mode's starting Energy
    zap::NewGamePlan plan;
    zap::parse_newgame("ap_newgame\nexpect 0x6C0 0300\nwrite 0x26C 2E1B\nwrite 0x310 110022", plan);

    game.watch_newgame();
    check(game.newgame_status() == "idle", "nothing armed");
    game.arm_newgame(plan);
    check(game.newgame_status() == "armed", "armed");
    g_transition_fn = nullptr;
    game.watch_newgame();
    check(g_section[0x26C] == 0, "no new game queued: nothing written");
    queue_new_game(0, 40);
    game.watch_newgame();
    check(g_section[0x26C] == 0 && game.newgame_status() == "armed",
          "a normal new game (marker 0) is not ours");
    queue_new_game(1, 1);
    game.watch_newgame();
    check(g_section[0x26C] == 0, "too close to being built: left alone");
    queue_new_game(1, 39);
    game.watch_newgame();
    check(g_section[0x26C] == 0x2E && g_section[0x26D] == 0x1B && g_section[0x312] == 0x22,
          "a randomizer new game gets the plan");
    check(game.newgame_status() == "applied slot 7", "and reports the slot");
    std::memset(g_section, 0, 0x300);
    game.watch_newgame();
    check(g_section[0x26C] == 0, "applied once only");

    std::memset(g_section, 0, sizeof(g_section));
    g_section[0x6C0] = 5;  // another mode's starting Energy
    game.arm_newgame(plan);
    queue_new_game(1, 39);
    game.watch_newgame();
    check(g_section[0x26C] == 0, "another mode: nothing written");
    check(game.newgame_status().rfind("refused", 0) == 0, "and reported refused");
    game.watch_newgame();
    check(game.newgame_status().rfind("refused", 0) == 0, "and the plan is dropped");

    game.arm_newgame(plan);
    game.disarm_newgame();
    check(game.newgame_status() == "idle", "disarming");
}

zap::Game* g_choice_game = nullptr;
void chosen(void* widget, int event) {
    (void)widget;
    (void)event;
    g_choice_game->start_archipelago(&chosen);
}

void test_archipelago_choice() {
    zap::Game game;
    g_choice_game = &game;
    check(game.configure(GetModuleHandleW(nullptr), stand_in_config()).empty(), "configures");
    reset_game();
    std::memset(g_section, 0, sizeof(g_section));
    std::memset(g_randomizer_menu, 0, sizeof(g_randomizer_menu));
    g_randomizer_menu[0x88] = 5;  // the player's own menu: Easy's starting Energy
    g_steps.clear();
    g_transition_fn = nullptr;
    g_rando_marker = 0;
    g_slot_taken = false;
    g_current_slot = 2;
    stand_in::ModePanel panel(reinterpret_cast<void*>(&panel_handler_fn),
                              reinterpret_cast<void*>(&widget_handler_fn),
                              reinterpret_cast<void*>(&rando_menu_fn));
    g_current_menu = panel.menu;
    auto press = [&] { reinterpret_cast<zap::WidgetCallback>(panel.callback())(panel.child, 6); };

    zap::NewGamePlan without_menu;
    zap::parse_newgame("ap_newgame\nwrite 0x310 110022", without_menu);
    game.arm_newgame(without_menu);
    game.watch_menu(&chosen);
    check(!game.menu_installed() && panel.text() == "RANDOMIZER ",
          "a plan without menu lines offers no ARCHIPELAGO");

    // The seed's mode starts with 3 Energy: the menu line sets it, the placement copies it. The
    // AP seed starts with 5, written over it afterwards.
    const char* request =
        "ap_newgame\nexpect 0x6C0 0300\nwrite 0x26C 2E1B\nwrite 0x310 110022\nmenu 0x88 0300\n"
        "write 0x6C0 0500";
    zap::NewGamePlan plan;
    check(zap::parse_newgame(request, plan).empty(), "parses menu lines");
    game.arm_newgame(plan);
    game.watch_menu(&chosen);
    check(game.menu_installed() && panel.text() == "ARCHIPELAGO", "offers ARCHIPELAGO");
    press();
    check(g_steps == "placement queue ", "the placement, then the new game queued");
    check(g_start_seen == 0x11, "the placement saw the AP seed's start");
    check(g_section[0x26C] == 0x2E && g_section[0x26D] == 0x1B && g_section[0x6C0] == 5,
          "the AP seed is written over the placement before the new game is queued");
    check(g_randomizer_menu[0x88] == 5, "the player's menu is put back");
    check(game.newgame_status() == "armed", "the plan waits for the queued new game");
    check(panel.text() == "ARCHIPELAGO" && panel.callback() == &rando_menu_fn,
          "the label reads ARCHIPELAGO through the fade; RANDOMIZER's callback is back");
    chosen(panel.child, 7);
    check(g_steps == "placement queue ", "a second press does nothing");
    game.watch_menu(&chosen);
    check(!game.menu_installed(), "not offered again while the new game is queued");
    game.watch_newgame();
    check(g_section[0x26C] == 0x2E && g_section[0x312] == 0x22 && g_section[0x6C0] == 5,
          "the next frame keeps the AP seed (its starting amounts differ from the mode's)");
    check(game.newgame_status() == "applied slot 3", "reported with the slot");
    std::memset(g_section, 0, 0x300);
    game.watch_newgame();
    check(g_section[0x26C] == 0, "the queued new game is not written again");
    game.watch_menu(&chosen);
    check(!game.menu_installed(), "the plan is used up");

    // The file screen closes as the new game starts; a later visit builds the panel afresh.
    panel.reset_label();

    // Another mode: the placement's amounts are not the seed's. Nothing is queued.
    g_steps.clear();
    g_transition_fn = nullptr;
    zap::NewGamePlan other;
    zap::parse_newgame(
        "ap_newgame\nexpect 0x6C0 0300\nwrite 0x26C 2E1B\nwrite 0x310 110022\nmenu 0x88 0500",
        other);
    game.arm_newgame(other);
    game.watch_menu(&chosen);
    press();
    check(g_steps == "placement ", "another mode: no new game queued");
    check(game.newgame_status().rfind("refused the placement", 0) == 0, "and reported");
    check(g_randomizer_menu[0x88] == 5, "the player's menu is put back");
    game.watch_menu(&chosen);
    check(!game.menu_installed() && panel.text() == "RANDOMIZER ", "and the plan dropped");

    // A slot the game will not start a new game in.
    g_steps.clear();
    g_slot_taken = true;
    game.arm_newgame(plan);
    game.watch_menu(&chosen);
    press();
    check(game.newgame_status().rfind("refused the game did not queue", 0) == 0,
          "a new game the game would not queue is reported");
    game.watch_menu(&chosen);
    check(!game.menu_installed() && panel.text() == "RANDOMIZER ",
          "and the plan dropped, for the client to arm again");
    g_slot_taken = false;
    g_transition_fn = nullptr;
    game.arm_newgame(plan);
    game.watch_menu(&chosen);
    check(panel.text() == "ARCHIPELAGO", "armed again, ARCHIPELAGO is back");
    game.disarm_newgame();
    game.watch_menu(&chosen);
    check(!game.menu_installed() && panel.text() == "RANDOMIZER " &&
              panel.callback() == &rando_menu_fn,
          "disarming puts RANDOMIZER back");
    g_current_menu = nullptr;
    g_slot_taken = false;
}

void test_start_table() {
    zap::NewGamePlan bad;
    check(!zap::parse_newgame("ap_newgame\nwrite 0x10 00\nstart_table 0100190001ff", bad).empty(),
          "a start table not in 5-byte rooms is refused");
    check(!zap::parse_newgame("ap_newgame\nwrite 0x10 00\nstart_table 3600000000", bad).empty(),
          "a room off the 54 x 54 map is refused");
    check(!zap::parse_newgame("ap_newgame\nwrite 0x10 00\nstart_table 0100190002", bad).empty(),
          "a flag other than 0 or 1 is refused");

    zap::Game game;
    g_choice_game = &game;
    check(game.configure(GetModuleHandleW(nullptr), stand_in_config()).empty(), "configures");
    reset_game();
    std::memset(g_section, 0, sizeof(g_section));
    std::memset(g_randomizer_menu, 0, sizeof(g_randomizer_menu));
    g_steps.clear();
    g_transition_fn = nullptr;
    g_slot_taken = false;
    g_start_count = 0;
    std::memset(g_start_cells, 0, sizeof(g_start_cells));
    std::memset(g_start_flags, 0, sizeof(g_start_flags));
    stand_in::ModePanel panel(reinterpret_cast<void*>(&panel_handler_fn),
                              reinterpret_cast<void*>(&widget_handler_fn),
                              reinterpret_cast<void*>(&rando_menu_fn));
    g_current_menu = panel.menu;
    auto press = [&] { reinterpret_cast<zap::WidgetCallback>(panel.callback())(panel.child, 6); };
    const char* request =
        "ap_newgame\nexpect 0x6C0 0000\nwrite 0x26C 2E1B\nmenu 0x88 0000\n"
        "start_table 01001900010600290000";
    zap::NewGamePlan plan;
    check(zap::parse_newgame(request, plan).empty() && plan.start_table.size() == 2,
          "parses the start table");

    // The menu never opened: the table is built as it would build it, before the placement.
    game.arm_newgame(plan);
    game.watch_menu(&chosen);
    press();
    check(g_start_count == 2 && g_start_cells[0] == 1 && g_start_cells[1] == 25 &&
              g_start_cells[2] == 6 && g_start_cells[3] == 41 && g_start_flags[0] == 1 &&
              g_start_flags[1] == 0,
          "an empty start table is filled in");
    check(g_steps == "placement queue ", "and the new game started");
    game.watch_newgame();

    // The menu built it already, the same: accepted as it is.
    g_steps.clear();
    g_transition_fn = nullptr;
    panel.reset_label();
    game.arm_newgame(plan);
    game.watch_menu(&chosen);
    press();
    check(g_steps == "placement queue ", "a matching table: the new game started");
    game.watch_newgame();

    // A table that differs (another build's rooms, say): refused before anything is placed.
    g_steps.clear();
    g_transition_fn = nullptr;
    g_start_flags[1] = 1;
    panel.reset_label();
    game.arm_newgame(plan);
    game.watch_menu(&chosen);
    press();
    check(g_steps.empty(), "a differing table: nothing placed or queued");
    check(game.newgame_status().rfind("refused the game's start table differs", 0) == 0,
          "and reported");
    check(panel.text() == "RANDOMIZER ", "RANDOMIZER is back");

    // A second start outside the table: refused before anything is written.
    g_steps.clear();
    g_start_count = 0;
    zap::NewGamePlan outside;
    zap::parse_newgame(std::string(request) + "\nwrite 0x6CC 0500", outside);
    panel.reset_label();
    game.arm_newgame(outside);
    game.watch_menu(&chosen);
    press();
    check(g_steps.empty() && g_start_count == 0, "a second start outside: nothing written");
    check(game.newgame_status().rfind("refused the second start is outside", 0) == 0,
          "and reported");
    g_current_menu = nullptr;
    g_start_count = 0;
}

void test_escape_skip() {
    zap::Game game;
    check(game.configure(GetModuleHandleW(nullptr), stand_in_config()).empty(), "configures");
    reset_events();
    game.watch_skip();
    check(event_else() == 3668 && game.skip_offered(), "event 3665 offers the skip either way");
    game.watch_skip();  // within its 30 frames: nothing to do
    game.restore_skip();
    check(event_else() == -1 && !game.skip_offered(), "restored");

    reset_events();
    event_3665()[2] = 251;  // not the record traced on B1
    for (int i = 0; i < 31; ++i) game.watch_skip();
    check(event_3665()[6] == 0xFF && !game.skip_offered(), "another record is left alone");

    reset_events();
    g_event_count = 3000;  // the table not loaded in full
    zap::Game fresh;
    fresh.configure(GetModuleHandleW(nullptr), stand_in_config());
    fresh.watch_skip();
    check(event_else() == -1, "a short table is left alone");
    g_event_count = kStandInEvents;
    for (int i = 0; i < 31; ++i) fresh.watch_skip();
    check(event_else() == 3668, "patched once it is loaded");

    // The game loads its events again into a new table: the new one is patched.
    static BYTE other[kStandInEvents * 0x28];
    std::memcpy(other, g_events, sizeof(other));
    const int16_t none = -1;
    std::memcpy(other + 3665 * 0x28 + 6, &none, 2);
    g_event_table = other;
    for (int i = 0; i < 31; ++i) fresh.watch_skip();
    int16_t v;
    std::memcpy(&v, other + 3665 * 0x28 + 6, 2);
    check(v == 3668, "a reloaded table is patched");
    fresh.restore_skip();
    std::memcpy(&v, other + 3665 * 0x28 + 6, 2);
    check(v == -1, "and restored");

    // Restore only into the table patched, and only if the record still reads as patched.
    reset_events();
    for (int i = 0; i < 31; ++i) fresh.watch_skip();
    g_event_table = other;  // replaced since
    fresh.restore_skip();
    check(event_else() == 3668, "a table no longer loaded is not written");
    g_event_table = g_events;
}

void test_mailbox() {
    zap::Mailbox box;
    std::string result;
    std::atomic<bool> ran{false};
    check(!box.call([&] { ran = true; return std::string("x"); }, 20, result),
          "no frame: the call times out");
    box.serve();
    check(!ran, "withdrawn work never runs later");

    std::atomic<bool> stop{false};
    std::atomic<int> runs{0};
    std::thread frames([&] {
        while (!stop) {
            box.serve();
        }
    });
    int answered = 0;
    for (int i = 0; i < 20000; ++i) {
        if (box.call([&] { ++runs; return std::string("ok"); }, i % 3, result)) {
            if (result == "ok") ++answered;
        }
    }
    stop = true;
    frames.join();
    box.serve();
    check(runs == answered, "every answered call ran exactly once, and no other did");

    std::thread one([&] { Sleep(10); box.serve(); });
    check(box.call([] { throw 1; return std::string(); }, 1000, result) && result == "error exception",
          "an exception in the work becomes an error, not a crash");
    one.join();
}

}  // namespace

void test_popup_text_breaks_before_the_sender() {
    check(zap::popup_text("\xC5MISSILE BOX", "FROM ALICE", 24) ==
              std::string(6, ' ') + "\xC5MISSILE BOX" + std::string(6 + 7, ' ') + "FROM ALICE",
          "the sender starts the second line, both lines centered");
    check(zap::popup_text("\xC5MISSILE BOX", "", 24) == "\xC5MISSILE BOX", "no sender: one line");
    std::string head(24, 'A');
    check(zap::popup_text(head, "FROM BOB", 24) == head + std::string(8, ' ') + "FROM BOB",
          "a head that fills its line needs no padding");
    std::string wrapped = zap::popup_text("X", "FROM A VERY LONG PLAYER NAME HERE", 24);
    check(wrapped == std::string(11, ' ') + "X" + std::string(12, ' ') +
                         "FROM A VERY LONG PLAYER" + std::string(1 + 7, ' ') + "NAME HERE",
          "a long sender wraps at a word, not inside one");
}

int main() {
    test_parsers();
    test_configure();
    test_apply();
    test_popups();
    test_popup_text_breaks_before_the_sender();
    test_newgame_parser();
    test_newgame_watch();
    test_archipelago_choice();
    test_escape_skip();
    test_start_table();
    test_mailbox();
    if (g_failures == 0) std::printf("all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
