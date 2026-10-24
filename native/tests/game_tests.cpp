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
    zap::Batch notice;
    check(zap::parse_batch("apply\nnotice 49 Hint: Phaser at Machinus 29,30", notice).empty() &&
              notice.popups.size() == 1 && notice.popups[0].bare &&
              notice.popups[0].suffix == "HINT: PHASER AT MACHINUS 29,30",
          "a notice keeps its colon and has no head");
    zap::Batch plain;
    zap::parse_batch("apply\npopup 49 Hint: X", plain);
    check(!plain.popups[0].bare && plain.popups[0].suffix == "HINT X",
          "a popup's suffix still drops the colon");
    zap::Batch empty_notice;
    check(!zap::parse_batch("apply\nnotice 49 ~~", empty_notice).empty(), "an empty notice is refused");
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
    check(g_text == "\xC5MISSILE BOX FROM ALICE" && g_message_args[0] == 12,
          "icon and name, then the sender centered on the next line, the box 12 wide");
    check(g_message_args[1] == 0xA0 && g_message_args[2] == 0x58 &&
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
              g_text == "\xC5MISSILE BOX  FROM BOB",
          "then shows the next");

    // A notice: its text alone, wrapped and centered, with the item's sound.
    reset_game();
    apply(game, "apply\nnotice 36 HINT: PHASER AT MACHINUS 29,30");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_text == "HINT: PHASER AT MACHINUS29,30" && g_message_args[0] == 24,
          "a notice is its text alone, left-aligned, wrapped at 24");
    int w = 0;
    check(zap::notice_text("HINT: AB CDEFGHIJKLMNOPQRSTUVW", 24, &w) ==
                  "HINT: AB" + std::string(13, ' ') + "CDEFGHIJKLMNOPQRSTUVW" &&
              w == 21,
          "whole words to a line, each line padded to the box's width");
    check(zap::wrap_hyphenated("HINT: ABCDEFGHIJKLMNOPQRSTUVWXYZ0123", 24) ==
              std::vector<std::string>{"HINT: ABCDEFGHIJKLMNOPQ-", "RSTUVWXYZ0123"},
          "a word longer than a line is split with a hyphen at the line's end");
    {
        // A full line, then a word longer than a line: it starts a new line, no line runs over.
        auto lines = zap::wrap_hyphenated("HINT: MAGNUS ARMOR IS AT " + std::string(25, 'A'), 24);
        bool within = !lines.empty();
        for (const std::string& line : lines) within = within && line.size() <= 24;
        check(within && lines[0] == "HINT: MAGNUS ARMOR IS AT" &&
                  lines[1] == std::string(23, 'A') + "-" && lines[2] == "AA",
              "a long word after a full line is split from a line of its own");
        int width = 0;
        zap::notice_text("HINT: MAGNUS ARMOR IS AT " + std::string(25, 'A'), 24, &width);
        check(width == 24, "and the popup stays 24 wide");
    }
    check(g_sound == 61, "with the item's own sound");
    check(*reinterpret_cast<int32_t*>(g_message + 0x1FC) == 4 * 1 + 7 &&
              *reinterpret_cast<double*>(g_message + 0x48) == 0x58 - 1,
          "a two-line notice keeps the game's box");

    // Three lines and the blank one: the box made for the three, centred on them.
    reset_game();
    ::apply(game, "apply\nnotice 36 HINT: GAMMA KEY IS AT AN OLD TOWER BY THE SEA IN ALICE'S WORLD");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(*reinterpret_cast<int32_t*>(g_message + 0x1FC) == 4 * 3 + 4 &&
              *reinterpret_cast<double*>(g_message + 0x48) == 0x58 - 1 - 4,
          "a notice of three lines gets a box fitted to them");
    {
        std::string text =
            zap::notice_text("HINT: GAMMA KEY IS AT AN OLD TOWER BY THE SEA IN ALICE'S WORLD", 24);
        *reinterpret_cast<double*>(g_message + 0x48) = 0x58 - 1;
        *reinterpret_cast<int32_t*>(g_message + 0x1FC) = 99;  // not what the game wrote
        zap::fit_notice_box(g_message, text, 24);
        check(*reinterpret_cast<int32_t*>(g_message + 0x1FC) == 99,
              "a message not as the game made it is left alone");
    }

    // A long notice: every line shown, up to what the game's 256-byte message slot holds. A
    // 24-letter word makes the box 24 wide, then each 12-letter word takes a line of its own,
    // padded to 24: past 255 bytes, cut to the whole lines that fit (10 of 24), then the
    // blank line that keeps the last one in the game's window.
    reset_game();
    std::string words(24, 'Y');
    for (int i = 0; i < 14; ++i) words += " ABCDEFGHIJKL";
    ::apply(game, "apply\nnotice 36 " + words);
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_text.size() == 241 && g_text.back() == ' ' && g_message_args[0] == 24 &&
              g_text.rfind(std::string(24, 'Y') + "ABCDEFGHIJKL", 0) == 0,
          "a notice past the message slot is cut to whole lines that fit");
    check(zap::notice_text("HINT: GAMMA KEY IS AT AN OLD TOWER IN ALICE'S WORLD", 24, &w) ==
                  "HINT: GAMMA KEY IS AT AN" "OLD TOWER IN ALICE'S    " "WORLD" +
                      std::string(19, ' ') + " " &&
              w == 24,
          "three lines or more end with a blank line, so the game's window shows the last");
    zap::Batch long_notice;
    zap::parse_batch("apply\nnotice 49 " + std::string(300, 'a'), long_notice);
    check(long_notice.popups[0].suffix.size() == 200, "a notice is capped at 200 characters");

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

// Read and written through volatile: the compiler would otherwise take the array's initial
// bytes as constants.
int32_t popup_width_in_code() {
    volatile unsigned char* code = g_popup_width_code;
    return code[1] | (code[2] << 8) | (code[3] << 16) | (code[4] << 24);
}

void set_code_byte(unsigned char value) {
    DWORD old = 0;
    VirtualProtect(g_popup_width_code, 1, PAGE_EXECUTE_READWRITE, &old);
    *static_cast<volatile unsigned char*>(g_popup_width_code) = value;
    VirtualProtect(g_popup_width_code, 1, old, &old);
}

void own_stand_in(BYTE* r, int16_t frame, const char* name, int16_t event) {
    std::memset(r, 0, 100);
    *reinterpret_cast<int16_t*>(r + 0x00) = 34;
    *reinterpret_cast<int16_t*>(r + 0x02) = frame;
    *reinterpret_cast<int16_t*>(r + 0x04) = 6;
    *reinterpret_cast<int16_t*>(r + 0x06) = 2;
    r[0x09] = 8;
    std::memcpy(r + 0x0A, name, std::strlen(name) + 1);
    *reinterpret_cast<int16_t*>(r + 0x3C) = 46;
    *reinterpret_cast<int16_t*>(r + 0x40) = event;
    *reinterpret_cast<int16_t*>(r + 0x58) = 1;
}

void test_pickup_popup_width() {
    zap::Game game;
    std::string configured = game.configure(GetModuleHandleW(nullptr), stand_in_config());
    check(configured.empty(), "configures with the pickup popup's width instruction");
    reset_game();
    own_stand_in(g_records + 47 * 100, 134, "SCRAP ENERGY", 3549);
    own_stand_in(g_records + 48 * 100, 142, "SCRAP CHUNK", 3550);
    std::map<int, zap::Label> labels;
    // "Map Width" and "For Finder" on two lines, at map cell (5, 2).
    check(zap::parse_labels("ap_labels\nlabel 5 2 4d61702057696474680a466f722046696e646572",
                            labels)
              .empty(),
          "a two-line label parses");
    check(labels[2 * 54 + 5].width == 10, "its width is its widest line");
    game.set_labels(labels);
    field<uint8_t>(0x107A) = 2;  // row
    field<uint8_t>(0x107B) = 5;  // column
    game.watch_ap_items();
    check(game.popup_width() == 10 && popup_width_in_code() == 10,
          "in the label's cell the pickup popup is the label's width");
    check(game.ap_items_restyled(), "and unload has something to put back");
    g_menu = 2;  // a menu open (the equipment screen, say)
    game.watch_ap_items();
    check(*reinterpret_cast<int16_t*>(g_records + 48 * 100 + 0x02) == 142 &&
              std::string(reinterpret_cast<const char*>(g_records + 48 * 100 + 0x0A)) ==
                  "SCRAP CHUNK" &&
              popup_width_in_code() == 24,
          "in a menu the stand-ins are the game's own, and the popup width the game's");
    g_menu = 0;
    game.watch_ap_items();
    check(popup_width_in_code() == 10, "back in the world, ours again");
    field<uint8_t>(0x107A) = 3;
    game.watch_ap_items();
    check(popup_width_in_code() == 24, "elsewhere the game's 24");
    field<uint8_t>(0x107A) = 2;
    game.watch_ap_items();
    game.restore_ap_items();
    check(popup_width_in_code() == 24 && !game.ap_items_restyled(), "restore puts 24 back");

    // A width an earlier helper left (one that went without putting 24 back) is taken over.
    {
        DWORD old_protect = 0;
        VirtualProtect(g_popup_width_code, 5, PAGE_EXECUTE_READWRITE, &old_protect);
        static_cast<volatile unsigned char*>(g_popup_width_code)[1] = 12;
        VirtualProtect(g_popup_width_code, 5, old_protect, &old_protect);
        zap::Game later;
        later.configure(GetModuleHandleW(nullptr), stand_in_config());
        later.set_labels(labels);
        field<uint8_t>(0x107A) = 3;  // not the label's cell
        later.watch_ap_items();
        check(popup_width_in_code() == 24, "a later helper puts 24 back over a width left behind");
        later.restore_ap_items();
    }

    // Code that is not the traced instruction is left alone.
    set_code_byte(0xB9);  // mov ecx, not mov edx
    game.set_labels(labels);
    game.watch_ap_items();
    check(popup_width_in_code() == 24 && game.popup_width() == 24, "other code: not written");
    game.restore_ap_items();
    set_code_byte(0xBA);
}

void test_zexion_item_labels() {
    zap::Game game;
    game.configure(GetModuleHandleW(nullptr), stand_in_config());
    reset_game();
    own_stand_in(g_records + 47 * 100, 134, "SCRAP ENERGY", 3549);
    own_stand_in(g_records + 48 * 100, 142, "SCRAP CHUNK", 3550);
    std::map<int, zap::Label> labels;
    // "@36" (the Missile Box) for ZEXION2 at (5, 2); "@47" (Scrap Energy) at (6, 2).
    check(zap::parse_labels("ap_labels\nlabel 5 2 4033360a464f52205a4558494f4e32\n"
                            "label 6 2 4034370a464f52205a4558494f4e32\n"
                            "label 7 2 4033370a464f52205a4558494f4e32",
                            labels)
              .empty(),
          "item labels parse");
    check(labels[2 * 54 + 5].item == 36 && labels[2 * 54 + 5].who == "FOR ZEXION2",
          "the item and whom it is for");
    game.set_labels(labels);
    field<uint8_t>(0x107A) = 2;
    field<uint8_t>(0x107B) = 5;
    game.watch_ap_items();
    std::string name(reinterpret_cast<const char*>(g_records + 47 * 100 + 0x0A));
    check(name == "\xC5MISSILE BOX" "FOR ZEXION2",
          "named from the item's record: its icon and the game's name, then whom it is for");
    check(popup_width_in_code() == 12, "the box as wide as the item's line");
    field<uint8_t>(0x107B) = 6;
    game.watch_ap_items();
    name = reinterpret_cast<const char*>(g_records + 47 * 100 + 0x0A);
    check(name == "\xEE" "SCRAP ENERGY FOR ZEXION2",
          "a stand-in item of theirs takes the stand-in's own name and the logo glyph");
    field<uint8_t>(0x107B) = 7;  // item 37, whose record holds no name here
    game.watch_ap_items();
    name = reinterpret_cast<const char*>(g_records + 47 * 100 + 0x0A);
    check(name == " \xEE" "AP ITEM" "  FOR ZEXION2", "a record with no name: AP ITEM");
    game.restore_ap_items();
}

BYTE* pool_slot(int k) { return g_item_pool + k * 0x3B0; }

void put_object(int k, int32_t spawn, int16_t record, int x, int y) {
    BYTE* o = pool_slot(k);
    std::memset(o, 0, 0x3B0);
    BYTE* frames = g_frames;
    std::memcpy(o + 0x1A8, &frames, sizeof(frames));
    BYTE* palette = g_palette_table[8];
    std::memcpy(o + 0x198, &palette, sizeof(palette));
    *reinterpret_cast<int16_t*>(o + 0x1A0) = 8;
    *reinterpret_cast<int32_t*>(o + 0x1BC) = 152;
    *reinterpret_cast<int32_t*>(o + 0x1D8) = spawn;
    *reinterpret_cast<int16_t*>(o + 0x1E8) = record;
    o[0x3AC] = static_cast<BYTE>(y);
    o[0x3AD] = static_cast<BYTE>(x);
}

BYTE* object_base(int k) { return *reinterpret_cast<BYTE**>(pool_slot(k) + 0x1A8); }
BYTE* object_palette(int k) { return *reinterpret_cast<BYTE**>(pool_slot(k) + 0x198); }
int16_t object_palette_number(int k) { return *reinterpret_cast<int16_t*>(pool_slot(k) + 0x1A0); }

BYTE* general_slot(int k) { return g_object_pool + k * 0x5C8; }

// An item as some rooms show it: an object in the general pool (type 2026 in the reversed-gravity
// rooms, 715 elsewhere) with the sprite fields an item object has, its cell at +0x210 and +0x211,
// and the frame its stand-in record gives.
void put_flipped(int k, int32_t spawn, int32_t frame, int x, int y) {
    BYTE* o = general_slot(k);
    std::memset(o, 0, 0x5C8);
    BYTE* frames = g_frames;
    std::memcpy(o + 0x1A8, &frames, sizeof(frames));
    BYTE* shown = g_frames + frame * 0x38;
    std::memcpy(o + 0x1B0, &shown, sizeof(shown));
    BYTE* palette = g_palette_table[8];
    std::memcpy(o + 0x198, &palette, sizeof(palette));
    *reinterpret_cast<int16_t*>(o + 0x1A0) = 8;
    *reinterpret_cast<int32_t*>(o + 0x1BC) = 152;
    *reinterpret_cast<int32_t*>(o + 0x1C4) = frame;
    *reinterpret_cast<int32_t*>(o + 0x1D8) = spawn;
    o[0x210] = static_cast<BYTE>(y);
    o[0x211] = static_cast<BYTE>(x);
}

BYTE* general_ptr(int k, ptrdiff_t at) { return *reinterpret_cast<BYTE**>(general_slot(k) + at); }

void test_flipped_items_show_as_themselves() {
    zap::Game game;
    game.configure(GetModuleHandleW(nullptr), stand_in_config());
    reset_game();
    own_stand_in(g_records + 47 * 100, 134, "SCRAP ENERGY", 3549);
    own_stand_in(g_records + 48 * 100, 142, "SCRAP CHUNK", 3550);
    BYTE* box = g_records + 36 * 100;  // the Missile Box: frames 39 to 42, 4 ticks, palette 1
    *reinterpret_cast<int16_t*>(box + 0x02) = 39;
    *reinterpret_cast<int16_t*>(box + 0x04) = 4;
    *reinterpret_cast<int16_t*>(box + 0x06) = 4;
    box[0x09] = 1;
    g_frame_clock = 1000;  // 250 % 4 = 2: frame 41
    put_flipped(12, 501, 95, 5, 2);  // a progression stand-in in the Missile Box's cell
    put_flipped(13, 502, 95, 6, 2);  // one in a cell labeled AP ITEM
    put_flipped(14, 503, 95, 5, 2);  // one in that cell drawing another sprite set: not an item
    BYTE* other_list = g_frames + 0x38;
    std::memcpy(general_slot(14) + 0x1A8, &other_list, sizeof(other_list));
    put_flipped(15, 504, 40, 5, 2);  // one at a frame that is not a logo's: not a stand-in
    std::map<int, zap::Label> labels;
    zap::parse_labels("ap_labels\nlabel 5 2 4033360a464f52205a4558494f4e32\n"
                      "label 6 2 4150204974656d0a464f52205a4558494f4e32",
                      labels);
    game.set_labels(labels);
    field<uint8_t>(0x107A) = 2;
    field<uint8_t>(0x107B) = 5;
    game.watch_ap_items();
    check(general_ptr(12, 0x1A8) == g_frames && general_ptr(12, 0x1B0) == g_frames + 41 * 0x38,
          "a reversed-gravity stand-in shown as the Missile Box's frame 41, its list untouched");
    check(general_ptr(12, 0x198) == g_palette_table[1] &&
              *reinterpret_cast<int16_t*>(general_slot(12) + 0x1A0) == 1,
          "in its palette");
    check(general_ptr(13, 0x1A8) == g_frames && general_ptr(13, 0x198) == g_palette_table[8],
          "another game's item keeps the logo");
    check(general_ptr(14, 0x1B0) == g_frames + 95 * 0x38 && general_ptr(15, 0x1B0) == g_frames + 40 * 0x38,
          "objects not showing a logo are left alone");
    check(game.ap_items_restyled(), "and unload has something to put back");

    game.restore_ap_items();
    check(general_ptr(12, 0x1A8) == g_frames && general_ptr(12, 0x1B0) == g_frames + 95 * 0x38 &&
              general_ptr(12, 0x198) == g_palette_table[8],
          "restore puts it back");
    check(!game.ap_items_restyled(), "and nothing is left to put back");
}

void test_zexion_items_show_as_themselves() {
    zap::Game game;
    game.configure(GetModuleHandleW(nullptr), stand_in_config());
    reset_game();
    own_stand_in(g_records + 47 * 100, 134, "SCRAP ENERGY", 3549);
    own_stand_in(g_records + 48 * 100, 142, "SCRAP CHUNK", 3550);
    BYTE* box = g_records + 36 * 100;  // the Missile Box: frames 39 to 42, 4 ticks, palette 1
    *reinterpret_cast<int16_t*>(box + 0x02) = 39;
    *reinterpret_cast<int16_t*>(box + 0x04) = 4;
    *reinterpret_cast<int16_t*>(box + 0x06) = 4;
    box[0x09] = 1;
    put_object(3, 77, 48, 5, 2);  // a progression stand-in in the Missile Box's cell
    put_object(4, 78, 48, 6, 2);  // a stand-in in a cell labeled AP ITEM
    put_object(5, 79, 1, 5, 2);   // a Missile Container of this world, in the same cell
    std::map<int, zap::Label> labels;
    // (5, 2): "@36" for ZEXION2; (6, 2): "AP ITEM" for ZEXION2.
    zap::parse_labels("ap_labels\nlabel 5 2 4033360a464f52205a4558494f4e32\n"
                      "label 6 2 4150204974656d0a464f52205a4558494f4e32",
                      labels);
    game.set_labels(labels);
    field<uint8_t>(0x107A) = 2;
    field<uint8_t>(0x107B) = 5;
    game.watch_ap_items();
    // clock 1000 / 4 ticks = 250, % 4 frames = 2: frame 41, and the game computes 95.
    check(object_base(3) == g_frames + (41 - 95) * 0x38,
          "the stand-in's frame list moved so frame 95 lands on the Missile Box's frame 41");
    check(object_palette(3) == g_palette_table[1] && object_palette_number(3) == 1,
          "and the Missile Box's palette");
    check(object_base(4) == g_frames && object_palette(4) == g_palette_table[8],
          "a stand-in for another game's item keeps the logo");
    check(object_base(5) == g_frames && object_palette(5) == g_palette_table[8],
          "this world's own items are left alone");
    check(game.ap_items_restyled(), "and unload has something to put back");

    g_frame_clock = 1004;  // 251 % 4 = 3: frame 42
    game.watch_ap_items();
    check(object_base(3) == g_frames + (42 - 95) * 0x38, "the item animates with the clock");

    // The object leaves (its room left) and its slot is taken by a new spawn: that one is
    // dressed afresh, and the old one is not "put back".
    put_object(3, 80, 48, 5, 2);
    game.watch_ap_items();
    check(object_base(3) == g_frames + (42 - 95) * 0x38, "a new spawn is dressed too");

    // A base this helper did not write (another build, another tool): left alone.
    put_object(6, 81, 48, 5, 2);
    BYTE* odd = g_frames + 0x38;
    std::memcpy(pool_slot(6) + 0x1A8, &odd, sizeof(odd));
    game.watch_ap_items();
    check(object_base(6) == odd && object_palette(6) == g_palette_table[8],
          "an object not as expected is not written");

    // The label goes: the stand-in shows the logo again.
    std::map<int, zap::Label> fewer;
    zap::parse_labels("ap_labels\nlabel 6 2 4150204974656d0a464f52205a4558494f4e32", fewer);
    game.set_labels(fewer);
    game.watch_ap_items();
    check(object_base(3) == g_frames && object_palette(3) == g_palette_table[8] &&
              object_palette_number(3) == 8,
          "without its label the stand-in is put back");

    game.set_labels(labels);
    game.watch_ap_items();
    check(object_palette(3) == g_palette_table[1], "dressed again");
    game.restore_ap_items();
    check(object_base(3) == g_frames && object_palette(3) == g_palette_table[8],
          "restore puts the objects back");
    check(!game.ap_items_restyled(), "and nothing is left to put back");

    // The game sets a dressed object's base back itself: it is dressed again.
    game.set_labels(labels);
    game.watch_ap_items();
    BYTE* template_base = g_frames;
    std::memcpy(pool_slot(3) + 0x1A8, &template_base, sizeof(template_base));
    game.watch_ap_items();
    check(object_base(3) == g_frames + (42 - 95) * 0x38, "a base the game set back is redressed");

    // Scrap Chunk's record is no longer ours (left as another helper left it, say): its objects
    // are put back and not dressed, since the frame the game computes is no longer the logo's.
    put_object(7, 91, 48, 5, 2);
    *reinterpret_cast<int16_t*>(g_records + 48 * 100 + 0x40) = 3551;  // neither own nor ours
    *reinterpret_cast<int16_t*>(g_records + 48 * 100 + 0x02) = 142;
    *reinterpret_cast<int16_t*>(g_records + 48 * 100 + 0x06) = 2;
    game.watch_ap_items();
    check(object_base(3) == g_frames && object_palette(3) == g_palette_table[8],
          "a stand-in whose record is not restyled is put back");
    check(object_base(7) == g_frames, "and not dressed");
    game.restore_ap_items();
    own_stand_in(g_records + 48 * 100, 142, "SCRAP CHUNK", 3550);

    // A dressed object whose slot was reused by another spawn is not "put back" on restore.
    game.set_labels(labels);
    game.watch_ap_items();
    put_object(3, 90, 1, 9, 9);  // now a Missile Container somewhere else
    BYTE* moved = g_frames + 5 * 0x38;
    std::memcpy(pool_slot(3) + 0x1A8, &moved, sizeof(moved));
    game.restore_ap_items();
    check(object_base(3) == moved, "a reused slot is left alone on restore");
}

void shield_records(int16_t object) {
    reset_events();
    BYTE* ev = g_events + 3553 * 0x28;
    ev[0] = 11;
    const int16_t words[] = {1, object};
    std::memcpy(ev + 2, words, sizeof(words));
    const int16_t self = 3553;
    std::memcpy(ev + 0x24, &self, sizeof(self));
    BYTE* shield = g_records + 50 * 100;
    std::memcpy(shield + 0x0A, "ENERGY SHIELD", sizeof("ENERGY SHIELD"));
    *reinterpret_cast<int16_t*>(shield + 0x40) = 3553;
    *reinterpret_cast<int16_t*>(shield + 0x5A) = -1;  // no sound
    field<uint8_t>(0x107A) = 4;
    field<uint8_t>(0x107B) = 7;
}

void place(BYTE* o, double x, double y) {
    std::memcpy(o + 0x40, &x, sizeof(x));
    std::memcpy(o + 0x48, &y, sizeof(y));
}

std::string record_text(int item) {
    return std::string(reinterpret_cast<const char*>(g_records + item * 100 + 0x0A));
}

void test_stand_ins_named_by_their_own_cell() {
    zap::Game game;
    game.configure(GetModuleHandleW(nullptr), stand_in_config());
    reset_game();
    own_stand_in(g_records + 47 * 100, 134, "SCRAP ENERGY", 3549);
    own_stand_in(g_records + 48 * 100, 142, "SCRAP CHUNK", 3550);
    std::map<int, zap::Label> labels;
    // (5, 2): "MAP WIDTH" for FINDER; (6, 2): "AP ITEM" for ZEXION2.
    zap::parse_labels("ap_labels\nlabel 5 2 4d61702057696474680a466f722046696e646572\n"
                      "label 6 2 4150204974656d0a464f52205a4558494f4e32",
                      labels);
    game.set_labels(labels);
    // The player in a cell with no label (a boss arena's other cell), at (1000, 600).
    field<uint8_t>(0x107A) = 3;
    field<uint8_t>(0x107B) = 9;
    field<double>(0x1100) = 1000;
    field<double>(0x1108) = 600;
    put_object(3, 77, 48, 5, 2);  // a drop for (5, 2), far away
    place(pool_slot(3), 3000, 600);
    put_object(4, 78, 48, 6, 2);  // a drop for (6, 2), close by
    place(pool_slot(4), 1010, 610);
    game.watch_ap_items();
    check(record_text(48).find("ZEXION2") != std::string::npos,
          "a stand-in is named for the location of the nearest one, not the player's cell");
    check(record_text(47).find("FINDER") == std::string::npos &&
              record_text(47).find("ZEXION2") == std::string::npos,
          "the other stand-in, with none of its own loaded, for the player's cell: AP ITEM");
    field<double>(0x1100) = 2990;
    game.watch_ap_items();
    check(record_text(48).find("FINDER") != std::string::npos, "and follows the player");
    check(game.popup_width() == 10, "the popup as wide as the label of the stand-in within reach");

    // An object off the map, or in a cell with no label, is not taken.
    place(pool_slot(3), -5, 600);
    game.watch_ap_items();
    check(record_text(48).find("ZEXION2") != std::string::npos, "a position off the map is not used");
    put_object(3, 79, 48, 7, 2);
    place(pool_slot(3), 2990, 600);
    game.watch_ap_items();
    check(record_text(48).find("ZEXION2") != std::string::npos, "nor a cell with no label");

    // A stand-in shown by a general object (found by the scan every 30 frames) counts too.
    put_flipped(12, 501, 63, 5, 2);
    place(general_slot(12), 2995, 600);
    for (int i = 0; i < 32; ++i) game.watch_ap_items();  // through the next scan
    check(record_text(47).find("FINDER") != std::string::npos,
          "a general object's stand-in is named for its own cell");

    // Out of reach of every stand-in, a labeled cell of the player's own comes first.
    field<uint8_t>(0x107A) = 2;
    field<uint8_t>(0x107B) = 5;  // (5, 2): FINDER
    field<double>(0x1100) = 1600;
    game.watch_ap_items();
    check(record_text(48).find("FINDER") != std::string::npos,
          "a stand-in out of reach does not beat the player's labeled cell");
    check(game.popup_width() == 10, "and the popup is that cell's width");

    // A general object no longer showing a logo is not taken, even before the next scan.
    put_flipped(12, 501, 40, 5, 2);
    field<uint8_t>(0x107A) = 3;
    field<uint8_t>(0x107B) = 9;
    field<double>(0x1100) = 2995;
    game.watch_ap_items();
    check(record_text(47).find("FINDER") == std::string::npos,
          "a general object is checked again before its cell is used");
    game.restore_ap_items();
}

void test_received_shield() {
    zap::Game game;
    game.configure(GetModuleHandleW(nullptr), stand_in_config());
    reset_game();
    shield_records(2018);
    apply(game, "apply\npopup 50 FROM ALICE");
    game.pump_popups();
    check(g_messages == 1 && g_spawn_calls == 0, "the popup alone spawns nothing");

    check(game.spawn_shield(5000) == "refused not settled", "not before the player has settled");
    game.note_play(5000);
    check(game.spawn_shield(5500) == "refused not settled", "half a second is not enough");
    game.note_play(5600);
    check(game.spawn_shield(6000) == "spawned", "a second in one cell: spawned");
    check(g_spawn_calls == 1 && g_spawn_row == 4 && g_spawn_column == 7 &&
              g_spawn_event == g_events + 3553 * 0x28,
          "through the type-11 handler, from the player's cell, with event 3553");

    field<uint8_t>(0x107A) = 5;  // another cell: settling starts again
    game.note_play(6100);
    check(game.spawn_shield(6500) == "refused not settled", "a new cell settles again");
    field<int16_t>(0xCEA) = 0;  // dead
    game.note_play(8000);
    check(game.spawn_shield(9000) == "refused not settled", "not while dead");
    field<int16_t>(0xCEA) = 48;
    field<uint8_t>(0x107A) = 0xFF;  // the raw cell can be -1
    game.note_play(10000);
    check(game.spawn_shield(11500) == "refused off the map", "not from off the map");
    field<uint8_t>(0x107A) = 4;
    game.note_play(12000);
    shield_records(2017);  // not the event traced
    check(game.spawn_shield(13500) == "refused event not as traced", "an event not as traced");
    check(g_spawn_calls == 1, "nothing else spawned");
    reset_events();
}

void test_received_death() {
    zap::Game game;
    check(game.kill(1000) == "refused not configured", "nothing before configure");
    game.configure(GetModuleHandleW(nullptr), stand_in_config());
    reset_game();
    check(game.kill(1000) == "refused not settled", "not before the player was seen in play");
    game.note_play(1000);
    check(game.kill(1500) == "refused not settled", "half a second is not enough");
    field<uint8_t>(0x107A) = 9;  // another cell (a room change): settles again
    game.note_play(1600);
    check(game.kill(2000) == "refused not settled", "a new cell settles again");
    g_last_message.last = g_message;  // a message still showing
    g_last_message.sequence = 7;
    *reinterpret_cast<int32_t*>(g_message + 0x1D8) = 7;
    *reinterpret_cast<int32_t*>(g_message + 0x1EC) = 30;
    check(game.kill(2700) == "refused message showing" && g_deaths == 0,
          "not while a message shows: the death routine posts one unchecked");
    *reinterpret_cast<int32_t*>(g_message + 0x1EC) = 0;
    g_halt_flag = 1;
    check(game.kill(2700) == "refused halted" && g_deaths == 0 && field<int16_t>(0xCEA) == 48,
          "not while the game is halted, and energy left alone");
    g_halt_flag = 0;
    field<int32_t>(0x103C) = 80;  // a shield up (its strength) does not stop it
    check(game.kill(2700) == "died", "a second in play in one cell: died");
    check(g_deaths == 1 && g_death_player == g_player && g_death_kind == 0 &&
              g_death_energy == 0,
          "the death routine, with the player, kind 0, and energy already 0");
    game.note_play(2800);  // energy 0: not in play
    field<int16_t>(0xCEA) = 48;
    game.note_play(2900);  // alive again (a respawn): settles again
    check(game.kill(3200) == "refused not settled", "a respawn settles again");
    g_last_message.last = nullptr;
    g_menu = 3;
    game.note_play(4000);
    check(game.kill(4000) == "refused not settled", "not in a menu");
    check(g_deaths == 1, "no other death");
}

void test_assist_watch() {
    zap::Game game;
    check(game.configure(GetModuleHandleW(nullptr), stand_in_config()).empty(), "configures");
    stand_in::OptionsMenu options(reinterpret_cast<void*>(&widget_handler_fn),
                                  reinterpret_cast<void*>(&assist_menu_fn));
    g_current_menu = options.menu;
    game.watch_assists();
    check(options.callback() == &assist_menu_fn, "not armed: left alone");
    game.set_assists_locked(true);
    game.watch_assists();
    check(options.callback() == nullptr &&
              options.code(7) == zap::kLockGlyph && options.style() == 5,
          "armed: ASSIST locked, with the padlock, in style 5");
    game.set_assists_locked(false);
    game.watch_assists();
    check(options.callback() == &assist_menu_fn &&
              options.code(7) == 0 && options.style() == 0x15,
          "disarmed: put back");
    game.set_assists_locked(true);
    game.watch_assists();
    game.restore_assists();
    check(options.callback() == &assist_menu_fn,
          "restored before an unload");
    g_current_menu = nullptr;
}

void test_popup_text_breaks_before_the_sender() {
    int width = 0;
    check(zap::popup_text("\xC5MISSILE BOX", "FROM ALICE", 24, &width) ==
                  "\xC5MISSILE BOX FROM ALICE" &&
              width == 12,
          "the sender starts the second line, centered in the widest line, which sets the width");
    check(zap::popup_text("\xC5MISSILE BOX", "", 24) == "\xC5MISSILE BOX", "no sender: one line");
    std::string head(24, 'A');
    check(zap::popup_text(head, "FROM BOB", 24, &width) == head + std::string(8, ' ') + "FROM BOB" &&
              width == 24,
          "a head that fills its line needs no padding");
    std::string wrapped = zap::popup_text("X", "FROM A VERY LONG PLAYER NAME HERE", 24, &width);
    check(wrapped == std::string(11, ' ') + "X" + std::string(11, ' ') +
                         "FROM A VERY LONG PLAYER" + std::string(7, ' ') + "NAME HERE" &&
              width == 23,
          "a long sender wraps at a word, not inside one");
}

int main() {
    test_parsers();
    test_configure();
    test_apply();
    test_popups();
    test_popup_text_breaks_before_the_sender();
    test_pickup_popup_width();
    test_zexion_item_labels();
    test_zexion_items_show_as_themselves();
    test_flipped_items_show_as_themselves();
    test_stand_ins_named_by_their_own_cell();
    test_received_shield();
    test_received_death();
    test_assist_watch();
    test_newgame_parser();
    test_newgame_watch();
    test_archipelago_choice();
    test_escape_skip();
    test_start_table();
    test_mailbox();
    if (g_failures == 0) std::printf("all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
