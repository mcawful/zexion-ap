// Tests for Game against a stand-in game made of this program's own data and functions, so
// configure's checks and every call are real.

#include <windows.h>

#include <cstring>
#include <map>
#include <string>

#include "check.h"
#include "game_calls.h"
#include "scan.h"
#include "stand_in.h"
#include "stand_in_menu.h"

namespace {

using namespace stand_in;

// Configured as a client configures it, and the stand-in game as a save in play leaves it.
void ready(zap::Game& game) {
    check(configure(game).empty(), "configures");
    reset_game();
}

void player_cell(int column, int row) {
    field<uint8_t>(0x107A) = static_cast<uint8_t>(row);
    field<uint8_t>(0x107B) = static_cast<uint8_t>(column);
}

// Home for RETURN HOME, as the client sends it: the Landing Pod's save point in tiles, cell
// row 16, column 13 (329 / 20, 191 / 14).
constexpr int kPodColumn = 191, kPodRow = 329;

// The game's last message still showing (its sequence the last one's), `frames` left.
void message_showing(int32_t frames) {
    g_last_message = {g_message, 7};
    put<int32_t>(g_message, 0x1D8, 7);
    put<int32_t>(g_message, 0x1EC, frames);
}

// The RVA of the start of the section holding `p` in this program.
uint64_t section_start_of(const void* p) {
    const IMAGE_NT_HEADERS64* nt = zap::nt_headers(GetModuleHandleW(nullptr));
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
    ready(game);
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
    check(g_hud_value == 12.0, "the missile number is refreshed with the current value");
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
    ready(game);
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
    message_showing(100);
    for (int i = 0; i < 50; ++i) game.pump_popups();
    check(g_messages == 1 && game.queued_popups() == 1, "waits while the last popup shows");
    message_showing(1);
    game.pump_popups();
    check(g_messages == 2 &&
              g_text == "\xC5MISSILE BOX  FROM BOB",
          "then shows the next");

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
    check(get<int32_t>(g_message, 0x1FC) == 4 * 1 + 7 && get<double>(g_message, 0x48) == 0x58 - 1,
          "a two-line notice keeps the game's box");

    reset_game();
    ::apply(game, "apply\nnotice 36 HINT: GAMMA KEY IS AT AN OLD TOWER BY THE SEA IN ALICE'S WORLD");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(get<int32_t>(g_message, 0x1FC) == 4 * 3 + 4 &&
              get<double>(g_message, 0x48) == 0x58 - 1 - 4,
          "a notice of three lines gets a box fitted to them");
    {
        std::string text =
            zap::notice_text("HINT: GAMMA KEY IS AT AN OLD TOWER BY THE SEA IN ALICE'S WORLD", 24);
        put<double>(g_message, 0x48, 0x58 - 1);
        put<int32_t>(g_message, 0x1FC, 99);  // not what the game wrote
        zap::fit_notice_box(g_message, text, 24);
        check(get<int32_t>(g_message, 0x1FC) == 99,
              "a message not as the game made it is left alone");
    }

    // The 256-byte message slot holds 10 lines of 24, then the blank line's space: 241.
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
    check(long_notice.popups[0].text.size() == 200, "a notice is capped at 200 characters");

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

void test_newgame_watch() {
    zap::Game game;
    ready(game);
    std::memset(g_section, 0, sizeof(g_section));
    std::memcpy(g_section + 0x658, "AB", 2);  // the seed's text
    zap::NewGamePlan plan;
    zap::parse_newgame("ap_newgame\nidentify 0x658 4142\nexpect 0x6C0 0300\nwrite 0x26C 2E1B\n"
                       "write 0x310 110022\nmenu 0x88 0300",
                       plan);

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
    std::memcpy(g_section + 0x658, "XY", 2);  // another seed's text
    game.arm_newgame(plan);
    queue_new_game(1, 39);
    game.watch_newgame();
    check(g_section[0x26C] == 0, "another seed: nothing written");
    check(game.newgame_status().rfind("refused", 0) == 0, "and reported refused");
    game.watch_newgame();
    check(game.newgame_status().rfind("refused", 0) == 0, "and the plan is dropped");

    game.arm_newgame(plan);
    game.disarm_newgame();
    check(game.newgame_status() == "idle", "disarming");
}

// A RANDOMIZER new game with the seed's text is placed again as the seed (the menu set to its
// settings), as ARCHIPELAGO places it, and the plan written over it.
void test_randomizer_by_seed() {
    zap::Game game;
    ready(game);
    const std::string request =
        "ap_newgame\nidentify 0x658 4142\nexpect 0x6C0 0300\nwrite 0x26C 2E1B\n"
        "write 0x310 110022\nmenu 0x88 0300";
    zap::NewGamePlan plan;
    check(zap::parse_newgame(request, plan).empty() && !plan.choice, "parses a RANDOMIZER plan");
    auto started = [](const char* seed_text, uint8_t energy) {
        // The player's placement: the menu's text and amounts, its own item table.
        std::memset(g_section, 0, sizeof(g_section));
        std::memcpy(g_section + 0x658, seed_text, 2);
        g_section[0x6C0] = energy;
        g_section[0x26C] = 0x55;
        std::memset(g_randomizer_menu, 0, sizeof(g_randomizer_menu));
        g_randomizer_menu[0x88] = energy;
        g_steps.clear();
        g_start_seen = 0;
        queue_new_game(1, 39);
    };

    // The seed's text, the player's menu another mode's (5 Energy): placed again as the seed.
    started("AB", 5);
    game.arm_newgame(plan);
    game.watch_newgame();
    check(g_steps == "placement ", "the seed's placement runs once, nothing else queued");
    check(g_start_seen == 0x11, "the placement saw the AP seed's start");
    check(g_section[0x6C0] == 3 && g_section[0x26C] == 0x2E && g_section[0x312] == 0x22,
          "the section is the seed's placement with the plan written over it");
    check(g_randomizer_menu[0x88] == 5, "the player's menu is put back");
    check(game.newgame_status() == "applied slot 7", "reported with the slot");
    game.watch_newgame();
    check(g_steps == "placement ", "applied once only");

    started("AC", 3);
    game.arm_newgame(plan);
    game.watch_newgame();
    check(g_steps.empty() && g_section[0x26C] == 0x55, "another seed text: nothing placed");
    check(game.newgame_status().rfind("refused the new game is not this seed", 0) == 0,
          "and reported refused");

    // The seed's placement not the seed's (its amounts here): refused, the menu put back.
    zap::NewGamePlan other;
    zap::parse_newgame(request + "\nmenu 0x88 0500", other);
    started("AB", 3);
    game.arm_newgame(other);
    game.watch_newgame();
    check(g_steps == "placement ", "placed");
    check(g_section[0x26C] == 0x55 && g_section[0x6C0] == 3 && g_section[0x310] == 0 &&
              g_randomizer_menu[0x88] == 3,
          "the player's own placement and menu put back, nothing of the plan left");
    check(game.newgame_status().rfind("refused the placement is not the seed's (section +", 0) ==
              0,
          "and reported refused");

    // Double Teleport: the game's own start table is used, not the plan's.
    g_start_count = 2;
    std::memset(g_start_flags, 0, sizeof(g_start_flags));
    zap::NewGamePlan teleport;
    zap::parse_newgame(request + "\nstart_table 0100190001\nwrite 0x6CC 0100", teleport);
    started("AB", 3);
    game.arm_newgame(teleport);
    game.watch_newgame();
    check(g_steps == "placement " && game.newgame_status() == "applied slot 7",
          "a plan's table that differs from the game's is not compared");
    check(g_start_count == 2 && g_start_flags[0] == 0, "and the game's own is left as it is");
    zap::NewGamePlan outside;
    zap::parse_newgame(request + "\nwrite 0x6CC 0200", outside);
    started("AB", 3);
    game.arm_newgame(outside);
    game.watch_newgame();
    check(g_steps.empty() && g_section[0x26C] == 0x55, "a second start outside it: nothing placed");
    check(game.newgame_status().rfind("refused the second start is not a room of the game's own", 0) ==
              0,
          "and reported refused");
    g_start_count = 0;
    g_transition_fn = nullptr;
    g_rando_marker = 0;
}

zap::Game* g_choice_game = nullptr;
void chosen(void* widget, int event) {
    (void)widget;
    (void)event;
    g_choice_game->start_archipelago(&chosen);
}

// The file screen's game-mode panel open (the third slot picked) with nothing queued, the
// section and the randomizer menu blank, for `game` (configured here; `chosen` starts it).
struct ModeScreen {
    zap::Game& game;
    stand_in::ModePanel panel{reinterpret_cast<void*>(&panel_handler_fn),
                              reinterpret_cast<void*>(&widget_handler_fn),
                              reinterpret_cast<void*>(&rando_menu_fn)};

    explicit ModeScreen(zap::Game& g) : game(g) {
        g_choice_game = &game;
        ready(game);
        std::memset(g_section, 0, sizeof(g_section));
        std::memset(g_randomizer_menu, 0, sizeof(g_randomizer_menu));
        g_steps.clear();
        g_transition_fn = nullptr;
        g_rando_marker = 0;
        g_slot_taken = false;
        g_current_slot = 2;
        g_current_menu = panel.menu;
    }
    ~ModeScreen() {
        g_current_menu = nullptr;
        g_transition_fn = nullptr;
        g_rando_marker = 0;
        g_slot_taken = false;
        g_start_count = 0;
    }
    ModeScreen(const ModeScreen&) = delete;
    ModeScreen& operator=(const ModeScreen&) = delete;

    // The button pressed: its callback, as the game's widget code calls it (event 6).
    void press() { reinterpret_cast<zap::WidgetCallback>(panel.callback())(panel.child, 6); }
    void start(const zap::NewGamePlan& plan) {
        game.arm_newgame(plan);
        game.watch_menu(&chosen);
        press();
    }
    void visit_again() {
        g_steps.clear();
        g_transition_fn = nullptr;
        panel.reset_label();
    }
};

void test_archipelago_choice() {
    zap::Game game;
    ModeScreen screen(game);
    stand_in::ModePanel& panel = screen.panel;
    g_randomizer_menu[0x88] = 5;  // the player's own menu: Easy's starting Energy

    zap::NewGamePlan without_choice;
    zap::parse_newgame("ap_newgame\nwrite 0x310 110022\nmenu 0x88 0300\nidentify 0x658 41",
                       without_choice);
    game.arm_newgame(without_choice);
    game.watch_menu(&chosen);
    check(!game.menu_installed() && panel.text() == "RANDOMIZER ",
          "menu lines without choice offer no ARCHIPELAGO (a RANDOMIZER seed)");
    screen.press();
    check(g_steps.empty(), "and its press does nothing");

    // The seed's mode starts with 3 Energy: the menu line sets it, the placement copies it. The
    // AP seed starts with 5, written over it afterwards.
    const char* request =
        "ap_newgame\nexpect 0x6C0 0300\nwrite 0x26C 2E1B\nwrite 0x310 110022\nmenu 0x88 0300\n"
        "write 0x6C0 0500\nidentify 0x658 41\nchoice";
    zap::NewGamePlan plan;
    check(zap::parse_newgame(request, plan).empty(), "parses menu lines");
    game.arm_newgame(plan);
    game.watch_menu(&chosen);
    check(game.menu_installed() && panel.text() == "ARCHIPELAGO", "offers ARCHIPELAGO");
    screen.press();
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

    // Another mode: the placement's amounts are not the seed's.
    screen.visit_again();
    zap::NewGamePlan other;
    zap::parse_newgame(
        "ap_newgame\nexpect 0x6C0 0300\nwrite 0x26C 2E1B\nwrite 0x310 110022\nmenu 0x88 0500\n"
        "identify 0x658 41\nchoice",
        other);
    screen.start(other);
    check(g_steps == "placement ", "another mode: no new game queued");
    check(game.newgame_status().rfind("refused the placement", 0) == 0, "and reported");
    check(g_randomizer_menu[0x88] == 5, "the player's menu is put back");
    game.watch_menu(&chosen);
    check(!game.menu_installed() && panel.text() == "RANDOMIZER ", "and the plan dropped");

    g_steps.clear();
    g_slot_taken = true;
    screen.start(plan);
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
}

void test_start_table() {
    auto refused = [](const char* table) {
        zap::NewGamePlan bad;
        return !zap::parse_newgame(std::string("ap_newgame\nidentify 0x658 41\nwrite 0x10 00\n"
                                               "menu 0x88 00\nstart_table ") +
                                       table,
                                   bad)
                    .empty();
    };
    check(refused("0100190001ff"), "a start table not in 5-byte rooms is refused");
    check(refused("3600000000"), "a room off the 54 x 54 map is refused");
    check(refused("0100190002"), "a flag other than 0 or 1 is refused");

    zap::Game game;
    ModeScreen screen(game);
    g_start_count = 0;
    std::memset(g_start_cells, 0, sizeof(g_start_cells));
    std::memset(g_start_flags, 0, sizeof(g_start_flags));
    const char* request =
        "ap_newgame\nexpect 0x6C0 0000\nwrite 0x26C 2E1B\nmenu 0x88 0000\n"
        "start_table 01001900010600290000\nidentify 0x658 41\nchoice";
    zap::NewGamePlan plan;
    check(zap::parse_newgame(request, plan).empty() && plan.start_table.size() == 2,
          "parses the start table");

    // The menu never opened: the table is built as the menu would build it.
    screen.start(plan);
    check(g_start_count == 2 && g_start_cells[0] == 1 && g_start_cells[1] == 25 &&
              g_start_cells[2] == 6 && g_start_cells[3] == 41 && g_start_flags[0] == 1 &&
              g_start_flags[1] == 0,
          "an empty start table is filled in");
    check(g_steps == "placement queue ", "and the new game started");
    game.watch_newgame();

    screen.visit_again();
    screen.start(plan);
    check(g_steps == "placement queue ", "a matching table: the new game started");
    game.watch_newgame();

    screen.visit_again();
    g_start_flags[1] = 1;
    screen.start(plan);
    check(g_steps.empty(), "a differing table: nothing placed or queued");
    check(game.newgame_status().rfind("refused the game's start table differs", 0) == 0,
          "and reported");
    check(screen.panel.text() == "RANDOMIZER ", "RANDOMIZER is back");

    g_steps.clear();
    g_start_count = 0;
    zap::NewGamePlan outside;
    zap::parse_newgame(std::string(request) + "\nwrite 0x6CC 0500", outside);
    screen.panel.reset_label();
    screen.start(outside);
    check(g_steps.empty() && g_start_count == 0, "a second start outside: nothing written");
    check(game.newgame_status().rfind("refused the second start is outside", 0) == 0,
          "and reported");
}

void test_scanner_line() {
    zap::Game game;
    check(configure(game).empty(), "configures");
    reset_texts();
    auto text = [](BYTE* entry) { return std::string(reinterpret_cast<char*>(entry)); };
    auto tail_zero = [](BYTE* entry) {
        for (int i = 21; i < 32; ++i) {
            if (entry[i] != 0) return false;
        }
        return true;
    };
    game.watch_discs();
    check(text(text_12()) == "ENERGY KEYS DETECTED!" && !game.scan_written(), "off: untouched");

    game.set_discs(0, 15, "COLLECT 15 DATA DISCS!");
    game.watch_discs();
    check(text(text_12()) == "COLLECT 15 DATA DISCS!" && game.scan_written(), "reworded");
    game.set_discs(3, 3, "COLLECT 3 DATA DISCS!");
    game.watch_discs();
    check(text(text_12()) == "COLLECT 3 DATA DISCS!" && text_12()[21] == 0,
          "a new line over its own, the rest zeros");
    game.set_discs(0, 0, "");
    game.watch_discs();
    check(text(text_12()) == "ENERGY KEYS DETECTED!" && tail_zero(text_12()) &&
              !game.scan_written(),
          "off again: the game's text back");

    std::memcpy(text_12(), "ENERGIESCHLUESSEL!\0\0\0", 21);  // another language, say
    game.set_discs(0, 15, "COLLECT 15 DATA DISCS!");
    for (int i = 0; i < 31; ++i) game.watch_discs();
    check(text(text_12()) == "ENERGIESCHLUESSEL!" && !game.scan_written(), "another text untouched");

    // Restore puts back only the table it wrote, and only while it still reads as written.
    reset_texts();
    for (int i = 0; i < 31; ++i) game.watch_discs();
    static BYTE other[20 * 32];
    std::memset(other, 0, sizeof(other));
    std::memcpy(other + 12 * 32, "ENERGY KEYS DETECTED!", 21);
    g_text_table = other;
    for (int i = 0; i < 31; ++i) game.watch_discs();
    check(text(other + 12 * 32) == "COLLECT 15 DATA DISCS!", "a reloaded table is written too");
    game.restore_discs();
    check(text(other + 12 * 32) == "ENERGY KEYS DETECTED!", "restored before an unload");
    g_text_table = g_texts;
    reset_texts();

    int count = -1, required = -1;
    std::string line;
    bool told = false;
    auto parses = [&](const std::string& request) {
        return zap::parse_discs(request, count, required, line, told).empty();
    };
    check(parses("discs") && count == 0 && required == 0 && line.empty(), "discs alone: off");
    check(parses("discs 3 15 434f4c4c454354") && count == 3 && required == 15 &&
              line == "COLLECT" && told,
          "discs with a count, the required number and a line");
    check(parses("discs 3 15 434f4c4c454354 0") && !told,
          "and whether the player has been told how many are needed");
    check(!parses("discs 3 15 434f4c4c454354 2"), "told is 0 or 1");
    // Lower case is kept as sent (English(HD) shows it), written in capitals for the font.
    check(parses("discs 3 15 436f6c6c656374") && line == "Collect" &&
              zap::in_capitals(line) == "COLLECT",
          "a line in mixed case");
    check(!parses("discs 3 15 43c3a9"), "a letter the font lacks is not");
    check(!parses("discs 3 0 41"), "none required");
    check(!parses("discs 3 15"), "no line");
    check(!parses("discs 3 15 " + hex(std::string(32, 'A'))),
          "32 characters do not fit with the zero after them");
}

// The width the pickup popup's `mov edx, imm32` holds.
int32_t popup_width_in_code() {
    int32_t width = 0;
    for (int i = 4; i >= 1; --i) width = (width << 8) | code_byte(g_popup_width_code, i);
    return width;
}

void test_pickup_popup_width() {
    zap::Game game;
    ready(game);
    own_stand_ins(g_records);
    std::map<int, zap::Label> labels;
    // "Map Width" and "For Finder" on two lines, at map cell (5, 2).
    check(zap::parse_labels("ap_labels\nlabel 5 2 4d61702057696474680a466f722046696e646572",
                            labels)
              .empty(),
          "a two-line label parses");
    check(labels[2 * 54 + 5].width == 10, "its width is its widest line");
    game.set_labels(labels);
    player_cell(5, 2);
    game.watch_ap_items();
    check(game.popup_width() == 10 && popup_width_in_code() == 10,
          "in the label's cell the pickup popup is the label's width");
    check(game.ap_items_restyled(), "and unload has something to put back");
    g_menu = 2;  // a menu open (the equipment screen, say)
    game.watch_ap_items();
    check(get<int16_t>(g_records, 48 * 100 + 0x02) == 142 && record_name(48) == "SCRAP CHUNK" &&
              popup_width_in_code() == 24,
          "in a menu the stand-ins are the game's own, and the popup width the game's");
    g_menu = 0;
    game.watch_ap_items();
    check(popup_width_in_code() == 10, "back in the world, ours again");
    player_cell(5, 3);
    game.watch_ap_items();
    check(popup_width_in_code() == 24, "elsewhere the game's 24");
    player_cell(5, 2);
    game.watch_ap_items();
    game.restore_ap_items();
    check(popup_width_in_code() == 24 && !game.ap_items_restyled(), "restore puts 24 back");

    // A width an earlier helper left behind is adopted and put back to 24.
    {
        set_code_byte(g_popup_width_code, 1, 12);
        zap::Game later;
        configure(later);
        later.set_labels(labels);
        player_cell(5, 3);  // not the label's cell
        later.watch_ap_items();
        check(popup_width_in_code() == 24, "a later helper puts 24 back over a width left behind");
        later.restore_ap_items();
    }

    set_code_byte(g_popup_width_code, 0, 0xB9);  // mov ecx, not the traced mov edx
    game.set_labels(labels);
    game.watch_ap_items();
    check(popup_width_in_code() == 24 && game.popup_width() == 24, "other code: not written");
    game.restore_ap_items();
    set_code_byte(g_popup_width_code, 0, 0xBA);
}

void test_zexion_item_labels() {
    zap::Game game;
    ready(game);
    own_stand_ins(g_records);
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
    player_cell(5, 2);
    game.watch_ap_items();
    check(record_name(47) == "\xC5MISSILE BOX" "FOR ZEXION2",
          "named from the item's record: its icon and the game's name, then whom it is for");
    check(popup_width_in_code() == 12, "the box as wide as the item's line");
    player_cell(6, 2);
    game.watch_ap_items();
    check(record_name(47) == "\xEE" "SCRAP ENERGY FOR ZEXION2",
          "a stand-in item of theirs takes the stand-in's own name and the logo glyph");
    player_cell(7, 2);  // item 37, whose record holds no name here
    game.watch_ap_items();
    check(record_name(47) == " \xEE" "AP ITEM" "  FOR ZEXION2", "a record with no name: AP ITEM");

    // "@36 OS INSTALL DISK", for no one: the item's icon, the client's name for it.
    std::map<int, zap::Label> own;
    check(zap::parse_labels("ap_labels\nlabel 8 2 " + hex("@36 os install disk"), own).empty() &&
              own[2 * 54 + 8].item == 36 && own[2 * 54 + 8].item_name == "os install disk" &&
              own[2 * 54 + 8].who.empty(),
          "an own item's label: the item and its name");
    game.set_labels(own);
    player_cell(8, 2);
    game.watch_ap_items();
    check(record_name(47) == "\xC5OS INSTALL DISK",
          "named as the client says, after the item's icon");
    std::map<int, zap::Label> disk;  // item 44, whose record has no icon
    zap::parse_labels("ap_labels\nlabel 8 2 " + hex("@44 os install disk"), disk);
    game.set_labels(disk);
    game.watch_ap_items();
    check(record_name(47) == "\xFBOS INSTALL DISK", "a disk: the disk glyph, not the logo");
    std::map<int, zap::Label> bad;
    zap::parse_labels("ap_labels\nlabel 8 2 40333658", bad);  // "@36X": not an item label
    check(bad[2 * 54 + 8].item == -1 && bad[2 * 54 + 8].item_name.empty(), "@36X is text");
    // Pickup texts the client never sends, pinned: empty after the '/' (item 36 shown as itself),
    // a name then nothing, one cut to the popup's width, characters the font lacks dropped, and
    // a '/' in the second line kept as text.
    auto one = [](const std::string& text) {
        std::map<int, zap::Label> labels;
        zap::parse_labels("ap_labels\nlabel 8 2 " + hex(text), labels);
        return labels[2 * 54 + 8];
    };
    zap::Label l = one("@36/");
    check(l.item == 36 && l.pickup.empty() && l.item_name.empty(), "@36/: no pickup text");
    l = one("@36 Name/");
    check(l.item == 36 && l.item_name == "Name" && l.pickup.empty(), "@36 Name/: the name only");
    l = one("@36/" + std::string(30, 'x'));
    check(l.item == 36 && l.pickup == std::string(zap::kGamePopupWidth, 'x'), "cut to 24");
    l = one("@36/Tr~a#p\xE9");
    check(l.item == 36 && l.pickup == "Trap", "characters outside the font dropped");
    l = one("@36/Disorient Trap\nfor A/B");
    check(l.item == 36 && l.pickup == "Disorient Trap" && l.who == "for A/B",
          "a '/' in the second line is its text");

    // "@36/Disorient Trap" for Bob: a trap disguised as the box. The look and the map's name
    // stay the box's; the pickup popup reads the trap's name alone, no icon, then whom it is for.
    std::map<int, zap::Label> trap;
    check(zap::parse_labels("ap_labels\nlabel 8 2 " + hex("@36/Disorient Trap\nfor Bob"), trap)
                  .empty() &&
              trap[2 * 54 + 8].item == 36 && trap[2 * 54 + 8].pickup == "Disorient Trap" &&
              trap[2 * 54 + 8].item_name.empty() && trap[2 * 54 + 8].who == "for Bob",
          "a disguised trap's label: the item, its pickup text, whom it is for");
    game.set_labels(trap);
    game.watch_ap_items();
    const zap::Label& shown = game.labels().at(2 * 54 + 8);
    check(record_name(47) == "DISORIENT TRAP" "   FOR BOB",
          "the pickup names the trap, no icon, whom it is for centered under it");
    check(zap::map_name(shown) == "MISSILE BOX", "the map's cursor keeps the disguise");

    // "@44 OS Install Disk/Disruption Trap": a trap disguised as a disk, under the disk's name.
    std::map<int, zap::Label> as_disk;
    check(zap::parse_labels("ap_labels\nlabel 8 2 " + hex("@44 OS Install Disk/Disruption Trap"),
                            as_disk)
                  .empty() &&
              as_disk[2 * 54 + 8].item == 44 &&
              as_disk[2 * 54 + 8].item_name == "OS Install Disk" &&
              as_disk[2 * 54 + 8].pickup == "Disruption Trap",
          "a disguise under a name of the client's, and the trap's pickup text");
    game.set_labels(as_disk);
    game.watch_ap_items();
    const zap::Label& trap_as_disk = game.labels().at(2 * 54 + 8);
    check(record_name(47) == "DISRUPTION TRAP", "the pickup names the trap");
    check(trap_as_disk.glyph == 0xFB && zap::map_name(trap_as_disk) == "OS INSTALL DISK",
          "the look, the disk's glyph; the map's cursor, the name the client gave");
    game.restore_ap_items();
}

// The record icons 0x6E and 0x77 lie in the lower-case range: the text is put in capitals for
// the 8 x 8 font, never the icon before it (text.h, in_capitals).
void test_lower_case_icons() {
    zap::Game game;
    ready(game);
    g_records[36 * 100 + 0x08] = 0x6E;
    apply(game, "apply\npopup 36 from alice");
    game.pump_popups();
    check(g_text == "\x6E" "MISSILE BOX FROM ALICE",
          "a popup: the icon 0x6E kept, the sender in capitals", g_text);
    own_stand_ins(g_records);
    g_records[36 * 100 + 0x08] = 0x77;
    std::map<int, zap::Label> labels;
    zap::parse_labels("ap_labels\nlabel 5 2 " + hex("@36\nfor bob"), labels);
    game.set_labels(labels);
    player_cell(5, 2);
    game.watch_ap_items();
    check(record_name(47) == "\x77" "MISSILE BOX  FOR BOB",
          "another Zexion player's item: the icon 0x77 kept, whom it is for in capitals",
          record_name(47));
    game.restore_ap_items();
}

BYTE* pool_slot(int k) { return g_item_pool + k * 0x3B0; }
BYTE* general_slot(int k) { return g_object_pool + k * 0x5C8; }

// A live object's sprite fields as the game sets a stand-in's: the item sprites' 152 frames,
// palette 8, and its spawn.
void item_sprite(BYTE* o, int32_t spawn) {
    put(o, 0x1A8, static_cast<BYTE*>(g_frames));
    put(o, 0x198, g_palette_table[8]);
    put<int16_t>(o, 0x1A0, 8);
    put<int32_t>(o, 0x1BC, 152);
    put<int32_t>(o, 0x1D8, spawn);
}

void put_object(int k, int32_t spawn, int16_t record, int x, int y) {
    BYTE* o = pool_slot(k);
    std::memset(o, 0, 0x3B0);
    item_sprite(o, spawn);
    put<int16_t>(o, 0x1E8, record);
    o[0x3AC] = static_cast<BYTE>(y);
    o[0x3AD] = static_cast<BYTE>(x);
}

BYTE* object_base(int k) { return get<BYTE*>(pool_slot(k), 0x1A8); }
BYTE* object_palette(int k) { return get<BYTE*>(pool_slot(k), 0x198); }
int16_t object_palette_number(int k) { return get<int16_t>(pool_slot(k), 0x1A0); }

// An item shown by a general-pool object: an item object's sprite fields, its cell at +0x210
// (row) and +0x211 (column), and the frame its stand-in record gives.
void put_flipped(int k, int32_t spawn, int32_t frame, int x, int y) {
    BYTE* o = general_slot(k);
    std::memset(o, 0, 0x5C8);
    item_sprite(o, spawn);
    put(o, 0x1B0, g_frames + frame * 0x38);
    put<int32_t>(o, 0x1C4, frame);
    o[0x210] = static_cast<BYTE>(y);
    o[0x211] = static_cast<BYTE>(x);
}

BYTE* general_ptr(int k, ptrdiff_t at) { return get<BYTE*>(general_slot(k), at); }

// The Missile Box's record animated: frames 39 to 42, 4 ticks each, in palette 1.
void animated_missile_box() {
    BYTE* box = g_records + 36 * 100;
    put<int16_t>(box, 0x02, 39);
    put<int16_t>(box, 0x04, 4);
    put<int16_t>(box, 0x06, 4);
    box[0x09] = 1;
}

// (5, 2): "@36" (the Missile Box) for ZEXION2; (6, 2): "AP ITEM" for ZEXION2.
constexpr char kBoxAndApItem[] =
    "ap_labels\nlabel 5 2 4033360a464f52205a4558494f4e32\n"
    "label 6 2 4150204974656d0a464f52205a4558494f4e32";

void test_flipped_items_show_as_themselves() {
    zap::Game game;
    ready(game);
    own_stand_ins(g_records);
    animated_missile_box();
    g_frame_clock = 1000;  // 250 % 4 = 2: frame 41
    put_flipped(12, 501, 95, 5, 2);  // a progression stand-in in the Missile Box's cell
    put_flipped(13, 502, 95, 6, 2);  // one in a cell labeled AP ITEM
    put_flipped(14, 503, 95, 5, 2);  // one in that cell drawing another sprite set: not an item
    put(general_slot(14), 0x1A8, g_frames + 0x38);
    put_flipped(15, 504, 40, 5, 2);  // one at a frame that is not a logo's: not a stand-in
    std::map<int, zap::Label> labels;
    zap::parse_labels(kBoxAndApItem, labels);
    game.set_labels(labels);
    player_cell(5, 2);
    game.watch_ap_items();
    check(general_ptr(12, 0x1A8) == g_frames && general_ptr(12, 0x1B0) == g_frames + 41 * 0x38,
          "a reversed-gravity stand-in shown as the Missile Box's frame 41, its list untouched");
    check(general_ptr(12, 0x198) == g_palette_table[1] &&
              get<int16_t>(general_slot(12), 0x1A0) == 1,
          "in its palette");
    check(general_ptr(13, 0x1A8) == g_frames && general_ptr(13, 0x198) == g_palette_table[26] &&
              get<int32_t>(general_slot(13), 0x1D0) == 34,
          "another game's item keeps the logo, in palette 26 at style 34");
    check(general_ptr(14, 0x1B0) == g_frames + 95 * 0x38 && general_ptr(15, 0x1B0) == g_frames + 40 * 0x38,
          "objects not showing a logo are left alone");
    check(game.ap_items_restyled(), "and unload has something to put back");

    game.restore_ap_items();
    check(general_ptr(12, 0x1A8) == g_frames && general_ptr(12, 0x1B0) == g_frames + 95 * 0x38 &&
              general_ptr(12, 0x198) == g_palette_table[8],
          "restore puts it back");
    check(!game.ap_items_restyled(), "and nothing is left to put back");
}

// A stand-in already in palette 26 (the helper's own) is still dressed when a label names a
// Zexion item, in both pools.
void test_colored_stand_ins_dressed_later() {
    zap::Game game;
    ready(game);
    own_stand_ins(g_records);
    animated_missile_box();
    g_frame_clock = 1000;  // the Missile Box's frame 41
    put_object(4, 78, 47, 6, 2);
    put_flipped(12, 501, 63, 6, 2);
    std::map<int, zap::Label> plain;
    zap::parse_labels("ap_labels\nlabel 6 2 4150204974656d0a464f52205a4558494f4e32", plain);
    game.set_labels(plain);
    player_cell(6, 2);
    game.watch_ap_items();
    check(object_palette(4) == g_palette_table[26] && general_ptr(12, 0x198) == g_palette_table[26],
          "both colored first");
    std::map<int, zap::Label> box;
    zap::parse_labels("ap_labels\nlabel 6 2 4033360a464f52205a4558494f4e32", box);  // "@36"
    game.set_labels(box);
    game.watch_ap_items();
    check(object_base(4) == g_frames + (41 - 63) * 0x38 && object_palette(4) == g_palette_table[1],
          "an item-pool stand-in in palette 26 dressed as the Missile Box");
    check(general_ptr(12, 0x1B0) == g_frames + 41 * 0x38 &&
              general_ptr(12, 0x198) == g_palette_table[1],
          "and a general-pool one");
    game.restore_ap_items();
}

// Another Zexion player's Scrap Chunk or Scrap Energy ("@48", "@47") is dressed as the game loads
// it, not from its record, which the helper has restyled to a logo frame (the owner's in-game
// check of 1.1.0: at Kresezoa 19,11 the arrow logo showed in palette 8, the room's colors).
void test_zexion_scrap_shown_as_itself() {
    zap::Game game;
    ready(game);
    own_stand_ins(g_records);
    g_frame_clock = 1006;  // 1006 / 6 = 167, odd: each scrap's second frame
    put_object(3, 77, 47, 5, 2);     // in the Scrap Chunk's cell
    put_object(4, 78, 47, 6, 2);     // in the Scrap Energy's cell
    put_object(5, 79, 48, 6, 2);     // a progression stand-in (logo frame 95) there too
    put_flipped(12, 501, 63, 5, 2);  // a general-pool one in the Scrap Chunk's cell
    std::map<int, zap::Label> labels;
    zap::parse_labels("ap_labels\nlabel 5 2 " + hex("@48\nfor bob") + "\nlabel 6 2 " +
                          hex("@47\nfor bob"),
                      labels);
    game.set_labels(labels);
    player_cell(5, 2);
    game.watch_ap_items();
    check(get<int16_t>(g_records + 48 * 100, 0x02) == 95, "the records are restyled");
    check(object_base(3) == g_frames + (143 - 63) * 0x38 &&
              object_palette(3) == g_palette_table[8] && object_palette_number(3) == 8,
          "dressed as Scrap Chunk's own frame 143, in palette 8, not the arrow logo");
    check(object_base(4) == g_frames + (135 - 63) * 0x38 && object_palette(4) == g_palette_table[8],
          "and as Scrap Energy's own frame 135");
    check(object_base(5) == g_frames + (135 - 95) * 0x38 && object_palette(5) == g_palette_table[8],
          "from a progression stand-in's logo frame too");
    check(general_ptr(12, 0x1A8) == g_frames && general_ptr(12, 0x1B0) == g_frames + 143 * 0x38 &&
              general_ptr(12, 0x198) == g_palette_table[8],
          "a general-pool one too");
    check(game.looks().colored() == 0, "none drawn in palette 26: they are Zexion items");

    game.restore_ap_items();
    check(object_base(3) == g_frames && general_ptr(12, 0x1B0) == g_frames + 63 * 0x38 &&
              get<int16_t>(g_records + 48 * 100, 0x02) == 142,
          "restore puts the objects and the records back");
}

// Stand-ins not dressed as a Zexion item draw in palette 26 at style 34 while restyled; palette 8
// and their own style come back once not restyled, and on restore.
void test_stand_ins_in_their_own_colors() {
    zap::Game game;
    ready(game);
    own_stand_ins(g_records);
    animated_missile_box();
    put_object(3, 77, 48, 5, 2);  // a progression stand-in in the Missile Box's cell: dressed
    put_object(4, 78, 47, 6, 2);  // a stand-in for another game's item
    put<int32_t>(pool_slot(4), 0x1D0, 7);  // its style: the template's texture name
    put_object(5, 79, 1, 6, 2);   // this world's Missile Container
    put_object(6, 80, 47, 6, 2);  // a stand-in with a base not the template's
    put(pool_slot(6), 0x1A8, g_frames + 0x38);
    put_flipped(12, 501, 63, 6, 2);  // a general-pool stand-in
    std::map<int, zap::Label> labels;
    zap::parse_labels(kBoxAndApItem, labels);
    game.set_labels(labels);
    player_cell(6, 2);
    game.watch_ap_items();
    check(object_palette(4) == g_palette_table[26] && object_palette_number(4) == 26 &&
              get<int32_t>(pool_slot(4), 0x1D0) == 34,
          "a stand-in in palette 26 at style 34");
    check(general_ptr(12, 0x198) == g_palette_table[26] &&
              get<int32_t>(general_slot(12), 0x1D0) == 34,
          "and a general-pool one");
    check(object_palette(3) == g_palette_table[1], "one dressed as a Zexion item keeps its look");
    check(object_palette(5) == g_palette_table[8] && object_palette(6) == g_palette_table[8],
          "this world's items and an object not as expected are left alone");
    check(game.looks().colored() == 2, "two colored");

    // The marker's type showing a stand-in in palette 8 (location 81's reward display).
    g_flags[154] = 0;
    put_flipped(14, 503, 63, 6, 2);
    put<int16_t>(general_slot(14), 0x32E, 8210);
    general_slot(14)[0x120] = 1;
    for (int i = 0; i < 33; ++i) game.watch_ap_items();  // the next scan, then two frames
    check(general_ptr(14, 0x198) == g_palette_table[26] && general_slot(14)[0x120] == 1,
          "the reward display in its own colors, not hidden");
    check(game.stand_ins_seen(g_item_pool, g_frames).size() == 5, "and counted for names");

    BYTE* r = g_records + 47 * 100;  // the records no longer the helper's
    put<int16_t>(r, 0x06, 2);
    game.watch_ap_items();
    check(object_palette(4) == g_palette_table[8] && object_palette_number(4) == 8 &&
              get<int32_t>(pool_slot(4), 0x1D0) == 7,
          "not restyled: palette 8 and its own style again");
    game.restore_ap_items();

    own_stand_ins(g_records);
    put_object(4, 81, 47, 6, 2);
    put<int32_t>(pool_slot(4), 0x1D0, 7);
    game.set_labels(labels);
    game.watch_ap_items();
    check(object_palette(4) == g_palette_table[26], "colored");
    game.restore_ap_items();
    check(object_palette(4) == g_palette_table[8] && get<int32_t>(pool_slot(4), 0x1D0) == 7 &&
              general_ptr(12, 0x198) == g_palette_table[8] && game.looks().colored() == 0,
          "restore puts palette 8 and the style back");
}

// The logo-frame marker at R'Talva X34 Y51 is hidden (+0x120 cleared) each frame while restyled
// and flag 154 is clear, and shown again on the flag, off the logo frame, or on restore
// (docs/re/memory.md, "Other players' items").
void test_the_marker_hidden() {
    zap::Game game;
    ready(game);
    own_stand_ins(g_records);
    g_flags[154] = 0;
    auto object = [](int k, int32_t spawn, int32_t frame, int palette, int16_t first) {
        put_flipped(k, spawn, frame, 34, 51);
        put(general_slot(k), 0x198, g_palette_table[palette]);
        put<int16_t>(general_slot(k), 0x1A0, static_cast<int16_t>(palette));
        put<int16_t>(general_slot(k), 0x32E, first);
        general_slot(k)[0x120] = 1;
    };
    auto marker = [&](int k, int32_t spawn, int32_t frame) { object(k, spawn, frame, 4, 8210); };
    auto shown = [](int k) { return general_slot(k)[0x120]; };
    marker(20, 601, 63);
    put_flipped(21, 602, 63, 6, 2);  // a stand-in for another game's item
    general_slot(21)[0x120] = 1;
    object(23, 801, 63, 12, 13599);  // a boss's drop showing an AP item in its own palette
    std::map<int, zap::Label> labels;
    zap::parse_labels(kBoxAndApItem, labels);
    game.set_labels(labels);
    player_cell(34, 51);
    game.watch_ap_items();
    check(shown(20) == 0 && general_ptr(20, 0x1B0) == g_frames + 63 * 0x38 &&
              general_ptr(20, 0x1A8) == g_frames && general_ptr(20, 0x198) == g_palette_table[4],
          "the marker hidden, its frame, frame list and palette the game's");
    check(shown(21) == 1 && general_ptr(21, 0x198) == g_palette_table[26],
          "a stand-in keeps the logo, in its own colors");
    check(shown(23) == 1, "another type on a logo frame is left alone");
    check(game.looks().hidden_markers() == 1, "one marker hidden");
    const std::vector<zap::StandInSeen> seen = game.stand_ins_seen(g_item_pool, g_frames);
    check(seen.size() == 2, "the marker is no stand-in for names; the others count");

    general_slot(20)[0x120] = 1;  // the game shows it again (a blink after a hit)
    game.watch_ap_items();
    check(shown(20) == 0, "hidden again the next frame, not at the next scan");

    g_flags[154] = 1;  // the boss beaten: the marker goes on
    game.watch_ap_items();
    check(shown(20) == 1 && game.looks().hidden_markers() == 0, "shown again once flag 154 is set");
    game.watch_ap_items();
    check(shown(20) == 1, "and left shown");

    g_flags[154] = 0;
    game.watch_ap_items();
    check(shown(20) == 0, "hidden while it waits");
    put<int32_t>(general_slot(20), 0x1C4, 19);
    game.watch_ap_items();
    check(shown(20) == 1 && game.looks().hidden_markers() == 0,
          "off the logo frame: shown again the next frame, on its new frame");

    marker(22, 701, 63);
    general_slot(22)[0x120] = 0;
    for (int i = 0; i < 31; ++i) game.watch_ap_items();
    check(game.looks().hidden_markers() == 0, "one the game hid is not taken as hidden");
    game.restore_ap_items();
    check(shown(22) == 0, "nor shown on restore");

    game.set_labels(labels);
    marker(20, 603, 63);
    for (int i = 0; i < 31; ++i) game.watch_ap_items();
    check(shown(20) == 0, "hidden");
    marker(20, 604, 63);
    general_slot(20)[0x120] = 0;
    game.restore_ap_items();
    check(shown(20) == 0, "another spawn is not shown");

    game.set_labels(labels);
    marker(20, 605, 63);
    for (int i = 0; i < 31; ++i) game.watch_ap_items();
    check(shown(20) == 0, "hidden");
    game.restore_ap_items();
    check(shown(20) == 1 && game.looks().hidden_markers() == 0, "restore shows it again");

    // A multiworld with no other Zexion player: labels, none of them an item to dress (no "@N").
    std::map<int, zap::Label> plain;
    zap::parse_labels("ap_labels\nlabel 6 2 4150204974656d0a464f52205a4558494f4e32", plain);
    game.set_labels(plain);
    std::memset(g_item_pool, 0, sizeof(g_item_pool));  // no item-pool stand-in dressed either
    marker(20, 606, 63);
    for (int i = 0; i < 31; ++i) game.watch_ap_items();
    check(shown(20) == 0 && game.looks().hidden_markers() == 1,
          "hidden with no Zexion item to dress");
    g_flags[154] = 1;
    game.watch_ap_items();
    check(shown(20) == 1, "and shown again on the flag");
    g_flags[154] = 0;
    game.restore_ap_items();
}

void test_zexion_items_show_as_themselves() {
    zap::Game game;
    ready(game);
    own_stand_ins(g_records);
    animated_missile_box();
    put_object(3, 77, 48, 5, 2);  // a progression stand-in in the Missile Box's cell
    put_object(4, 78, 48, 6, 2);  // a stand-in in a cell labeled AP ITEM
    put_object(5, 79, 1, 5, 2);   // a Missile Container of this world, in the same cell
    std::map<int, zap::Label> labels;
    zap::parse_labels(kBoxAndApItem, labels);
    game.set_labels(labels);
    player_cell(5, 2);
    game.watch_ap_items();
    // clock 1000 / 4 ticks = 250, % 4 frames = 2: frame 41, and the game computes 95.
    check(object_base(3) == g_frames + (41 - 95) * 0x38,
          "the stand-in's frame list moved so frame 95 lands on the Missile Box's frame 41");
    check(object_palette(3) == g_palette_table[1] && object_palette_number(3) == 1,
          "and the Missile Box's palette");
    check(object_base(4) == g_frames && object_palette(4) == g_palette_table[26] &&
              object_palette_number(4) == 26 && get<int32_t>(pool_slot(4), 0x1D0) == 34,
          "a stand-in for another game's item keeps the logo, in palette 26 at style 34");
    check(object_base(5) == g_frames && object_palette(5) == g_palette_table[8],
          "this world's own items are left alone");
    check(game.ap_items_restyled(), "and unload has something to put back");

    g_frame_clock = 1004;  // 251 % 4 = 3: frame 42
    game.watch_ap_items();
    check(object_base(3) == g_frames + (42 - 95) * 0x38, "the item animates with the clock");

    put_object(3, 80, 48, 5, 2);  // its slot taken by a new spawn
    game.watch_ap_items();
    check(object_base(3) == g_frames + (42 - 95) * 0x38, "a new spawn is dressed too");

    put_object(6, 81, 48, 5, 2);
    BYTE* odd = g_frames + 0x38;
    put(pool_slot(6), 0x1A8, odd);
    game.watch_ap_items();
    check(object_base(6) == odd && object_palette(6) == g_palette_table[8],
          "an object not as expected is not written");

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

    game.set_labels(labels);
    game.watch_ap_items();
    put(pool_slot(3), 0x1A8, static_cast<BYTE*>(g_frames));
    game.watch_ap_items();
    check(object_base(3) == g_frames + (42 - 95) * 0x38, "a base the game set back is redressed");

    // Scrap Chunk's record no longer ours: the frame the game computes is no longer the logo's.
    put_object(7, 91, 48, 5, 2);
    put<int16_t>(g_records, 48 * 100 + 0x40, 3551);  // neither own nor ours
    put<int16_t>(g_records, 48 * 100 + 0x02, 142);
    put<int16_t>(g_records, 48 * 100 + 0x06, 2);
    game.watch_ap_items();
    check(object_base(3) == g_frames && object_palette(3) == g_palette_table[8],
          "a stand-in whose record is not restyled is put back");
    check(object_base(7) == g_frames, "and not dressed");
    game.restore_ap_items();
    own_stand_ins(g_records);

    game.set_labels(labels);
    game.watch_ap_items();
    put_object(3, 90, 1, 9, 9);  // now a Missile Container somewhere else
    BYTE* moved = g_frames + 5 * 0x38;
    put(pool_slot(3), 0x1A8, moved);
    game.restore_ap_items();
    check(object_base(3) == moved, "a reused slot is left alone on restore");
}

// A received item's pickup event (3548 to 3550, 3553) as B1's AreaEvents holds it: type 11,
// subtype 1, spawning `object` at the cell's corner plus (5, 8), no sound, then `next`.
void spawn_event(int16_t event, int16_t object, int16_t next) {
    const BYTE b1[0x28] = {0x0B, 0x00, 0x01, 0x00, 0x00, 0x00, 0xF3, 0x03, 0xFF, 0xFF,
                           0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x05, 0x00,
                           0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
                           0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    BYTE* ev = g_events + event * 0x28;
    std::memcpy(ev, b1, sizeof(b1));
    put<int16_t>(ev, 4, object);
    put<int16_t>(ev, 0x20, next);
    put<int16_t>(ev, 0x24, event);
}

// Scrap Chunk's next event, 3552, as B1 holds it: a fade of layer 7.
void chunk_fade_event() {
    const BYTE b1[0x28] = {0x01, 0x00, 0x07, 0x00, 0xF6, 0xFF, 0x08, 0x00, 0x03, 0x00,
                           0x07, 0x00, 0x00, 0x00, 0xE9, 0xFF, 0xFD, 0xFF, 0xFC, 0xFF,
                           0xFC, 0xFF, 0x04, 0x00, 0x04, 0x00, 0xFF, 0xFF, 0x00, 0x00,
                           0x80, 0x3F, 0xFF, 0xFF, 0x00, 0x00, 0xE0, 0x0D, 0x00, 0x00};
    std::memcpy(g_events + 3552 * 0x28, b1, sizeof(b1));
}

void shield_records(int16_t object) {
    reset_events();
    spawn_event(3553, object, -1);
    BYTE* shield = g_records + 50 * 100;
    std::memcpy(shield + 0x0A, "ENERGY SHIELD", sizeof("ENERGY SHIELD"));
    put<int16_t>(shield, 0x40, 3553);
    put<int16_t>(shield, 0x5A, -1);  // no sound
    player_cell(7, 4);
}

void place(BYTE* o, double x, double y) {
    put(o, 0x40, x);
    put(o, 0x48, y);
}

// A received Scrap Energy or Scrap Chunk (this player's own filler) pops up under the game's own
// name, while the stand-ins' records are restyled as other players' items.
void test_received_stand_in_popup() {
    zap::Game game;
    ready(game);
    own_stand_ins(g_records);
    std::map<int, zap::Label> labels;
    // (5, 2): "MAP WIDTH" for FINDER, the player's cell.
    zap::parse_labels("ap_labels\nlabel 5 2 4d61702057696474680a466f722046696e646572", labels);
    game.set_labels(labels);
    player_cell(5, 2);
    game.watch_ap_items();
    check(record_name(47).find("FINDER") != std::string::npos &&
              record_name(48).find("FINDER") != std::string::npos,
          "both stand-ins are named for the label");
    int width = 0;
    const std::string energy = zap::popup_text("SCRAP ENERGY", "FROM BOB", 24, &width);
    apply(game, "apply\npopup 47 FROM BOB");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_text == energy && g_message_args[0] == width,
          "a received Scrap Energy reads SCRAP ENERGY FROM BOB, laid out at the game's width");
    check(g_text.find("FINDER") == std::string::npos && g_text.find('\xEE') == std::string::npos,
          "not the restyled name, nor its logo glyph");
    const std::string chunk = zap::popup_text("SCRAP CHUNK", "FROM BOB", 24, &width);
    apply(game, "apply\npopup 48 FROM BOB");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_text == chunk && g_message_args[0] == width, "and Scrap Chunk SCRAP CHUNK FROM BOB");
    player_cell(9, 3);
    game.watch_ap_items();
    check(record_name(47).find("AP ITEM") != std::string::npos, "the record reads AP ITEM");
    apply(game, "apply\npopup 47 FROM BOB");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_text.rfind("SCRAP ENERGY", 0) == 0 && g_text.find("AP ITEM") == std::string::npos,
          "and the popup still SCRAP ENERGY");
    game.restore_ap_items();
}

// A received OS Install Disk (item 44) pops up after the disk glyph as OS INSTALL DISK, not as its
// record's DATA DISC with no icon; its count notice follows it.
void test_received_disk_popup() {
    zap::Game game;
    ready(game);
    std::memcpy(g_records + 44 * 100 + 0x0A, "DATA DISC", sizeof("DATA DISC"));
    int width = 0;
    const std::string disk = zap::popup_text("\xFB" "OS INSTALL DISK", "FROM BOB", 24, &width);
    apply(game, "apply\npopup 44 FROM BOB\nnotice 44 OS INSTALL DISK 1 OF 3");
    game.pump_popups();
    check(g_text == disk && g_message_args[0] == width && game.queued_popups() == 1,
          "the disk glyph, OS INSTALL DISK, then FROM BOB; its count waiting");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_text == zap::notice_text("OS INSTALL DISK 1 OF 3", 24) && game.queued_popups() == 0,
          "then its count, alone");
}

// A received Area Map from a seed with Intel Chips (`intel`) pops up as INTEL CHIP, though Intel
// Chips are off when it shows (a load before the client turns them on again); otherwise as its
// record names it.
void test_received_intel_chip_popup() {
    zap::Game game;
    ready(game);
    // As B1's record holds it: no popup icon (docs/re/memory.md, "Intel Chips").
    std::memcpy(g_records + 49 * 100 + 0x0A, "AREA MAP", sizeof("AREA MAP"));
    int width = 0;
    const std::string chip = zap::popup_text("INTEL CHIP", "FROM BOB", 24, &width);
    apply(game, "apply\nintel\npopup 49 FROM BOB");
    game.pump_popups();
    check(g_text == chip && g_message_args[0] == width, "an Intel Chip: INTEL CHIP");
    apply(game, "apply\npopup 49 FROM BOB");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_text == zap::popup_text("AREA MAP", "FROM BOB", 24), "an Area Map: AREA MAP");
}

void test_stand_ins_named_by_their_own_cell() {
    zap::Game game;
    ready(game);
    own_stand_ins(g_records);
    std::map<int, zap::Label> labels;
    // (5, 2): "MAP WIDTH" for FINDER; (6, 2): "AP ITEM" for ZEXION2.
    zap::parse_labels("ap_labels\nlabel 5 2 4d61702057696474680a466f722046696e646572\n"
                      "label 6 2 4150204974656d0a464f52205a4558494f4e32",
                      labels);
    game.set_labels(labels);
    player_cell(9, 3);  // a cell with no label, at (1000, 600)
    field<double>(0x1100) = 1000;
    field<double>(0x1108) = 600;
    put_object(3, 77, 48, 5, 2);  // a drop for (5, 2), far away
    place(pool_slot(3), 3000, 600);
    put_object(4, 78, 48, 6, 2);  // a drop for (6, 2), close by
    place(pool_slot(4), 1010, 610);
    game.watch_ap_items();
    check(record_name(48).find("ZEXION2") != std::string::npos,
          "a stand-in is named for the location of the nearest one, not the player's cell");
    check(record_name(47).find("FINDER") == std::string::npos &&
              record_name(47).find("ZEXION2") == std::string::npos,
          "the other stand-in, with none of its own loaded, for the player's cell: AP ITEM");
    field<double>(0x1100) = 2990;
    game.watch_ap_items();
    check(record_name(48).find("FINDER") != std::string::npos, "and follows the player");
    check(game.popup_width() == 10, "the popup as wide as the label of the stand-in within reach");

    place(pool_slot(3), -5, 600);
    game.watch_ap_items();
    check(record_name(48).find("ZEXION2") != std::string::npos, "a position off the map is not used");
    put_object(3, 79, 48, 7, 2);
    place(pool_slot(3), 2990, 600);
    game.watch_ap_items();
    check(record_name(48).find("ZEXION2") != std::string::npos, "nor a cell with no label");

    put_flipped(12, 501, 63, 5, 2);  // found by the scan every 31st frame
    place(general_slot(12), 2995, 600);
    for (int i = 0; i < 32; ++i) game.watch_ap_items();  // through the next scan
    check(record_name(47).find("FINDER") != std::string::npos,
          "a general object's stand-in is named for its own cell");

    player_cell(5, 2);  // FINDER
    field<double>(0x1100) = 1600;
    game.watch_ap_items();
    check(record_name(48).find("FINDER") != std::string::npos,
          "a stand-in out of reach does not beat the player's labeled cell");
    check(game.popup_width() == 10, "and the popup is that cell's width");

    put_flipped(12, 501, 40, 5, 2);  // no longer a logo, before the next scan
    player_cell(9, 3);
    field<double>(0x1100) = 2995;
    game.watch_ap_items();
    check(record_name(47).find("FINDER") == std::string::npos,
          "a general object is checked again before its cell is used");
    game.restore_ap_items();
}

void test_received_shield() {
    zap::Game game;
    ready(game);
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
    shield_records(2018);
    put<int16_t>(g_events + 3553 * 0x28, 0x20, 3552);  // a next event the trace did not see
    check(game.spawn_shield(13500) == "refused event not as traced", "nor one that runs another");
    shield_records(2018);
    put<int16_t>(g_records + 50 * 100, 0x40, 3549);  // the record names another event
    check(game.spawn_shield(13500) == "refused event not as traced", "nor another record's");
    check(g_spawn_calls == 1, "nothing else spawned");
    reset_events();
}

void scrap_records() {
    reset_events();
    spawn_event(3548, 2010, -1);
    spawn_event(3549, 2016, -1);
    spawn_event(3550, 2017, 3552);
    chunk_fade_event();
    own_stand_ins(g_records);
    BYTE* metal = g_records + 46 * 100;
    std::memcpy(metal + 0x0A, "SCRAP METAL", sizeof("SCRAP METAL"));
    put<int16_t>(metal, 0x40, 3548);
    put<int16_t>(metal, 0x5A, -1);  // no sound
    player_cell(7, 4);
}

// The next queued popup shown (the helper's gap between two, nothing of the game's up).
void show_next(zap::Game& game) {
    const int before = g_messages;
    for (int i = 0; i < 100 && g_messages == before; ++i) game.pump_popups();
}

// A received scrap's drop: its pickup's event, run once after its popup shows, once the player
// has been in one cell for a second, in the save its popup was for.
void test_received_scrap_drops() {
    zap::Game game;
    ready(game);
    scrap_records();
    check(game.drop_status() == "queued 0; last none", "nothing yet");
    apply(game, "apply\npopup 46 FROM ALICE\npopup 47 FROM BOB");
    game.note_play(5000);
    game.pump_drops(9000);
    check(game.queued_drops() == 0 && g_spawn_calls == 0, "nothing before its popup shows");
    game.pump_popups();
    check(g_messages == 1 && game.queued_drops() == 1 && g_spawn_calls == 0,
          "queued as Scrap Metal's popup shows");
    field<uint8_t>(0x107A) = 5;  // another cell (a room change): settles again
    game.note_play(9100);
    game.pump_drops(9500);
    check(g_spawn_calls == 0, "not before the player has settled");
    game.pump_drops(10100);
    check(g_spawn_calls == 1 && g_spawn_row == 5 && g_spawn_column == 7 &&
              g_spawn_event == g_events + 3548 * 0x28 && game.queued_drops() == 0 &&
              game.drop_status() == "queued 0; last spawned 46",
          "a second in one cell: Scrap Metal's event 3548, from the player's cell");
    game.pump_drops(12000);
    check(g_spawn_calls == 1, "once");
    player_cell(7, 4);
    game.note_play(12000);

    // The stand-ins restyled as other players' items: their records name no event (-1).
    std::map<int, zap::Label> labels;
    zap::parse_labels("ap_labels\nlabel 5 2 4d61702057696474680a466f722046696e646572", labels);
    game.set_labels(labels);
    game.watch_ap_items();
    check(at<int16_t>(g_records + 47 * 100, 0x40) == -1 &&
              at<int16_t>(g_records + 48 * 100, 0x40) == -1,
          "the stand-ins' records restyled");
    show_next(game);
    game.pump_drops(13000);
    check(g_spawn_calls == 2 && g_spawn_event == g_events + 3549 * 0x28,
          "a received Scrap Energy: event 3549, though its record is a stand-in's");
    apply(game, "apply\npopup 48 FROM BOB");
    show_next(game);
    g_events[3552 * 0x28 + 8] = 4;  // its next event's step not as traced
    game.pump_drops(13000);
    check(g_spawn_calls == 2 && game.queued_drops() == 0 &&
              game.drop_status() == "queued 0; last refused event not as traced 48",
          "Scrap Chunk refused, and dropped, while the fade it runs next is not as traced");
    chunk_fade_event();
    apply(game, "apply\npopup 48 FROM BOB");
    show_next(game);
    game.pump_drops(13000);
    check(g_spawn_calls == 3 && g_spawn_event == g_events + 3550 * 0x28,
          "a received Scrap Chunk: event 3550 (which runs 3552 itself)");
    game.restore_ap_items();

    apply(game, "apply\nnotice 46 HINT: X\npopup 49 FROM ALICE\npopup 50 FROM ALICE");
    for (int i = 0; i < 3; ++i) show_next(game);
    check(game.queued_popups() == 0 && game.queued_drops() == 0,
          "no drop for a notice, an Area Map, or an Energy Shield (the client's `shield`)");

    // Dead, or off the map: it waits, queued.
    apply(game, "apply\npopup 46 FROM ALICE");
    show_next(game);
    field<int16_t>(0xCEA) = 0;
    game.note_play(14000);
    game.pump_drops(16000);
    check(g_spawn_calls == 3 && game.queued_drops() == 1, "not while dead");
    field<int16_t>(0xCEA) = 48;
    field<uint8_t>(0x107A) = 0xFF;  // the raw cell can be -1
    game.note_play(16000);
    game.pump_drops(17500);
    check(g_spawn_calls == 3 && game.queued_drops() == 1, "off the map: kept");
    player_cell(7, 4);
    game.note_play(17500);
    game.pump_drops(18500);
    check(g_spawn_calls == 4 && game.queued_drops() == 0, "then spawned, on the map");

    // In another save than its popup's: it waits for its own.
    std::memset(g_section, 0, sizeof(g_section));
    g_section[0x26C] = 0x11;  // this seed's item table
    g_rando_marker = 1;
    apply(game, "apply\npopup 47 FROM BOB");
    show_next(game);
    g_section[0x26C] = 0x22;  // another seed's save
    game.pump_drops(20000);
    check(g_spawn_calls == 4 && game.queued_drops() == 1, "another seed's save: it waits");
    g_rando_marker = 0;
    game.pump_drops(20000);
    check(g_spawn_calls == 4 && game.queued_drops() == 1, "and in a normal save");
    g_rando_marker = 1;
    g_section[0x26C] = 0x11;
    // The client gone meanwhile: its drops stay, as its popups do (their items are given).
    game.client_gone(&chosen);
    game.pump_drops(20000);
    check(g_spawn_calls == 5 && game.queued_drops() == 0,
          "spawned in its own save, the client gone");
    g_rando_marker = 0;
    reset_events();
}

// A drop held through the game's ending, which reads as play, and not doubled by a popup the game
// could not take yet; one whose record names no event refused unless it is a stand-in's.
void test_received_scrap_drops_held() {
    zap::Game game;
    ready(game);
    scrap_records();
    g_flags[250] = 0;
    game.note_play(1000);
    game.pump_drops(1000);  // the load's first frame in play: flag 250 clear
    apply(game, "apply\npopup 46 FROM ALICE");
    g_message_fails = true;  // all four message slots busy
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(game.queued_popups() == 1 && game.queued_drops() == 0, "no drop for a popup not shown");
    g_message_fails = false;
    show_next(game);
    check(game.queued_popups() == 0 && game.queued_drops() == 1, "one, once it shows");
    g_flags[250] = 1;  // the ending began in this load
    game.pump_drops(5000);
    check(g_spawn_calls == 0 && game.queued_drops() == 1, "held through the ending");
    g_save_loaded = 0xFF;  // MISSION COMPLETE, then the title
    game.pump_drops(6000);
    g_save_loaded = 3;  // the save loaded again, past its ending
    game.note_play(7000);
    game.pump_drops(8000);
    check(g_spawn_calls == 1 && g_spawn_event == g_events + 3548 * 0x28,
          "spawned at the next load, though flag 250 stays set");
    apply(game, "apply\npopup 46 FROM ALICE");
    show_next(game);
    game.pump_drops(9000);
    check(g_spawn_calls == 2, "a save past its ending gets its drops");
    g_flags[250] = 0;

    // A popup that shows only after the grab (queued behind others, or applied just before it):
    // the load began before its ending, so its drop waits too.
    g_save_loaded = 0xFF;
    game.pump_drops(9100);
    g_save_loaded = 3;  // a load before its ending
    game.note_play(9200);
    game.pump_drops(9200);  // the load's first frame in play: flag 250 clear
    apply(game, "apply\npopup 47 FROM ALICE");
    g_flags[250] = 1;  // Zexion grabbed
    show_next(game);
    check(game.queued_drops() == 1, "its popup shown in the ending");
    game.pump_drops(11000);
    check(g_spawn_calls == 2 && game.queued_drops() == 1,
          "a drop queued after the ending began in its load waits too");
    g_save_loaded = 0xFF;
    game.pump_drops(12000);
    g_save_loaded = 3;
    game.note_play(12000);
    game.pump_drops(13000);
    check(g_spawn_calls == 3 && g_spawn_event == g_events + 3549 * 0x28,
          "and is spawned at the next load");

    // A save past its ending playing it again: the escape sets flag 211, the ending clears it.
    game.note_play(13000);
    game.pump_drops(13000);
    check(game.queued_drops() == 0, "nothing waiting");
    apply(game, "apply\npopup 48 FROM ALICE");
    g_menu = 3;  // at the ship: a frame not in play
    g_flags[211] = 1;
    game.pump_drops(13100);
    g_menu = 0;
    show_next(game);
    g_flags[211] = 0;  // the ending's first cutscene
    game.pump_drops(15000);
    check(g_spawn_calls == 3 && game.queued_drops() == 1, "held through the ending played again");
    g_save_loaded = 0xFF;
    game.pump_drops(15100);
    g_save_loaded = 3;
    game.note_play(15200);
    game.pump_drops(16500);
    check(g_spawn_calls == 4 && g_spawn_event == g_events + 3550 * 0x28,
          "and spawned at the next load");
    g_flags[250] = 0;

    put<int16_t>(g_records + 46 * 100, 0x40, -1);  // Scrap Metal's record names no event
    apply(game, "apply\npopup 46 FROM ALICE");
    show_next(game);
    game.pump_drops(17000);
    check(g_spawn_calls == 4 && game.drop_status() == "queued 0; last refused event not as traced 46",
          "-1 is only a restyled stand-in's");
    put<int16_t>(g_records + 47 * 100, 0x40, 3550);  // Scrap Energy's names Scrap Chunk's
    apply(game, "apply\npopup 47 FROM ALICE");
    show_next(game);
    game.pump_drops(17000);
    check(g_spawn_calls == 4 && game.drop_status() == "queued 0; last refused event not as traced 47",
          "nor another item's event");
    scrap_records();
    g_event_count = 3552;  // a table without 3552
    apply(game, "apply\npopup 48 FROM ALICE");
    show_next(game);
    game.pump_drops(17000);
    check(g_spawn_calls == 4 && game.drop_status() == "queued 0; last refused no event 48",
          "Scrap Chunk refused without the fade it runs next");
    reset_events();
}

void test_received_death() {
    zap::Game game;
    check(game.kill(1000) == "refused not configured", "nothing before configure");
    ready(game);
    check(game.kill(1000) == "refused not settled", "not before the player was seen in play");
    game.note_play(1000);
    check(game.kill(1500) == "refused not settled", "half a second is not enough");
    field<uint8_t>(0x107A) = 9;  // another cell (a room change): settles again
    game.note_play(1600);
    check(game.kill(2000) == "refused not settled", "a new cell settles again");
    message_showing(30);
    check(game.kill(2700) == "refused message showing" && g_deaths == 0,
          "not while a message shows: the death routine posts one unchecked");
    message_showing(0);
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

// Scrap Metal's record, whose sound a trap's popup plays (it has no record of its own).
void trap_records() {
    scrap_records();
    put<int16_t>(g_records + 46 * 100, 0x5A, 61);
    put<float>(g_records + 46 * 100, 0x5C, 1.0f);
}

// In play for a moment, as a trap waits for (not the frame of a load).
void in_play_since(zap::Game& game, uint64_t now) { game.note_play(now - 1000); }

// The stand-in's AILMENTS tests, each jne as the game has it (75) or patched (EB).
bool hit_jumps_are(BYTE opcode) {
    return code_byte(g_ailments_hit_code, 0x0B) == opcode &&
           code_byte(g_ailments_hit_code, 0x29) == opcode;
}
bool wave_jumps_are(BYTE opcode) {
    return code_byte(g_ailments_wave_code, 0x0B) == opcode &&
           code_byte(g_ailments_wave_code, 0x2A) == opcode;
}

// Frame calls as dllmain.cpp makes them (pump_traps, then pump_popups) until the next popup is
// taken off the queue, at most 100.
void next_frames(zap::Game& game, uint64_t now) {
    const size_t before = game.queued_popups();
    for (int i = 0; i < 100 && game.queued_popups() == before; ++i) {
        game.pump_traps(now);
        game.pump_popups();
    }
}

// What a trap's popup plays: Scrap Metal's sound (trap_records).
const std::string kTrapPopupSound = "61/10/0/8 ";

// A received trap: its popup queued in its turn, the trap set off as the popup shows, once; as
// the game's touch does but longer, past a shield, and with AILMENTS's tests patched while its
// timer runs.
void test_received_traps() {
    zap::Game game;
    ready(game);
    trap_records();
    in_play_since(game, 5000);
    check(apply(game, "apply\npopup 102 FROM ALICE") == "applied", "a Disruption Trap's popup");
    check(game.queued_traps() == 1 && game.queued_popups() == 1,
          "queued with its popup as the apply takes it");
    game.pump_popups();
    check(g_messages == 0 && field<int16_t>(0xDC2) == 0 && game.queued_popups() == 1,
          "its popup is left to pump_traps");
    g_sound_log.clear();
    game.pump_traps(5000);
    check(field<int16_t>(0xDC2) == 660 && game.trap_status() == "queued 0; last set 102",
          "set as its popup shows, 660 frames");
    check(g_messages == 1 && g_text.find("DISRUPTION TRAP") != std::string::npos &&
              g_text.find("FROM ALICE") != std::string::npos && game.queued_popups() == 0,
          "its popup: its name over its sender");
    check(g_sound_log == kTrapPopupSound + "26/10/8/16 ",
          "Scrap Metal's sound, then the Phazo's contact sound, as the state plays it");
    check(hit_jumps_are(0xEB) && wave_jumps_are(0x75),
          "the disruption's AILMENTS tests patched, the disorientation's not");
    game.pump_traps(6000);
    check(game.trap_status() == "queued 0; last set 102", "once");
    check(std::string(game.unload_blocker() ? game.unload_blocker() : "") ==
              "the AILMENTS tests are still the helper's",
          "no unload while they are patched");
    game.watch_all(&chosen);
    check(hit_jumps_are(0xEB), "kept while the timer runs");
    field<int16_t>(0xDC2) = 0;  // run out
    game.watch_all(&chosen);
    check(hit_jumps_are(0x75), "put back once it has run out");

    // Not drawn (every message slot busy): the trap waits with its popup.
    apply(game, "apply\npopup 102 FROM BOB");
    g_message_fails = true;
    for (int i = 0; i < 100; ++i) {
        game.pump_traps(6500);
        game.pump_popups();
    }
    check(field<int16_t>(0xDC2) == 0 && game.queued_traps() == 1 && game.queued_popups() == 1,
          "a popup not drawn: its trap not set, both kept");
    g_message_fails = false;
    next_frames(game, 6500);
    check(field<int16_t>(0xDC2) == 660 && game.queued_traps() == 0, "set once it is drawn");
    field<int16_t>(0xDC2) = 0;
    game.watch_all(&chosen);

    // An Energy Shield up does not stop it (the owner's choice).
    field<int32_t>(0x103C) = 80;
    apply(game, "apply\npopup 102 FROM BOB");
    next_frames(game, 7000);
    check(field<int16_t>(0xDC2) == 660 && field<int32_t>(0x103C) == 80,
          "past a shield, its strength untouched");
    field<int32_t>(0x103C) = 0;
    // A longer timer is left as it is (as the op does), and nothing is patched for it.
    field<int16_t>(0xDC2) = 0;
    game.watch_all(&chosen);
    field<int16_t>(0xDC2) = 700;
    apply(game, "apply\npopup 102 FROM BOB");
    next_frames(game, 8000);
    check(field<int16_t>(0xDC2) == 700 && game.trap_status() == "queued 0; last nothing 102" &&
              hit_jumps_are(0x75),
          "a longer one is left as it is, AILMENTS not patched for it");
    field<int16_t>(0xDC2) = 0;
    game.watch_all(&chosen);

    // Several of one kind at once (a reconnect's): one at a time, each popup showing as its trap
    // starts; the popups behind a waiting one, one of the other kind's too, go ahead meanwhile.
    for (int i = 0; i < 200; ++i) game.pump_popups();
    const int shown_before = g_messages;
    apply(game,
          "apply\npopup 102 FROM BOB\npopup 102 FROM BOB\npopup 46 FROM CAROL\n"
          "popup 103 FROM BOB");
    check(game.queued_popups() == 4 && game.queued_traps() == 3, "each popup queued in its turn");
    next_frames(game, 8000);
    check(field<int16_t>(0xDC2) == 660 && game.queued_traps() == 2 &&
              g_messages == shown_before + 1 && g_text.find("DISRUPTION TRAP") != std::string::npos,
          "the first set as its popup shows");
    field<int16_t>(0xDC2) = 300;  // counting down
    game.watch_all(&chosen);
    next_frames(game, 8000);
    check(field<int16_t>(0xDC2) == 300 && game.queued_traps() == 2 &&
              g_text.find("FROM CAROL") != std::string::npos,
          "the second waits for it, its popup too; the scrap's behind it goes ahead");
    next_frames(game, 8000);
    check(field<int16_t>(0xDC4) == 660 && game.queued_traps() == 1 &&
              g_text.find("DISORIENT TRAP") != std::string::npos,
          "and the other kind's, set as its popup shows");
    for (int i = 0; i < 200; ++i) {
        game.pump_traps(8000);
        game.pump_popups();
    }
    check(field<int16_t>(0xDC2) == 300 && g_messages == shown_before + 3 &&
              game.queued_popups() == 1,
          "nothing more while the first runs");
    field<int16_t>(0xDC2) = 0;  // run out
    game.watch_all(&chosen);
    next_frames(game, 8000);
    check(field<int16_t>(0xDC2) == 660 && game.queued_traps() == 0 && hit_jumps_are(0xEB) &&
              g_text.find("DISRUPTION TRAP") != std::string::npos,
          "then the second, its full 660, patched again, its popup now");
    field<int16_t>(0xDC2) = 0;
    field<int16_t>(0xDC4) = 0;
    game.watch_all(&chosen);
    // An enemy's own effect running does not hold a trap: it restarts it, as the touch would.
    field<int16_t>(0xDC2) = 100;
    apply(game, "apply\npopup 102 FROM BOB");
    next_frames(game, 8000);
    check(field<int16_t>(0xDC2) == 660, "over an enemy's effect: as its popup shows");
    field<int16_t>(0xDC2) = 0;
    game.watch_all(&chosen);
    for (int i = 0; i < 200; ++i) game.pump_popups();

    // The disorientation, as op 5 but longer and whatever AILMENTS reads.
    for (int16_t ailments : {int16_t{0}, int16_t{1}, int16_t{2}}) {
        g_ailments = ailments;
        field<int16_t>(0xDC4) = 0;
        field<int8_t>(0xDDB) = 6;
        field<int16_t>(0xD20) = 0;
        game.watch_all(&chosen);
        g_sound_log.clear();
        apply(game, "apply\npopup 103 FROM CAROL");
        next_frames(game, 9000);
        check(field<int16_t>(0xDC4) == 660 && field<int8_t>(0xDDB) == 2 &&
                  field<int16_t>(0xD20) == 2 && game.trap_status() == "queued 0; last set 103",
              "the disorientation: 660 frames, the byte turned, +0xD20 2, at any AILMENTS");
        check(g_sound_log == kTrapPopupSound + "82/10/8/16 ",
              "its popup's sound, then the sound of the state the wave's chains to");
        check(wave_jumps_are(0xEB) && hit_jumps_are(0x75), "its AILMENTS tests patched");
    }
    g_ailments = 0;
    // Over an enemy's wave running: 660, the byte not turned; +0xD20 left above 2.
    field<int16_t>(0xDC4) = 0;
    game.watch_all(&chosen);  // the trap's ended
    field<int16_t>(0xDC4) = 40;
    field<int16_t>(0xD20) = 9;
    apply(game, "apply\npopup 103 FROM CAROL");
    next_frames(game, 10000);
    check(field<int16_t>(0xDC4) == 660 && field<int8_t>(0xDDB) == 2 && field<int16_t>(0xD20) == 9,
          "over an enemy's wave: 660, the byte as it was");
    // A negative byte: C's % keeps the sign, as the game's code does.
    field<int16_t>(0xDC4) = 0;
    game.watch_all(&chosen);
    field<int8_t>(0xDDB) = -7;
    apply(game, "apply\npopup 103 FROM CAROL");
    next_frames(game, 11000);
    check(field<int8_t>(0xDDB) == -3, "(-7 + 4) % 8 is -3");
    // A load (no save, then the reset) puts the patch back.
    g_save_loaded = 0xFF;
    game.watch_all(&chosen);
    check(wave_jumps_are(0x75), "put back with no save loaded");
    g_save_loaded = 3;
    field<int16_t>(0xDC4) = 0;

    // Code not as traced, or reading another word than AILMENTS: never patched.
    build_ailments_code(&g_ailments, false);  // a byte of the wave's tests not as traced
    apply(game, "apply\npopup 103 FROM CAROL");
    next_frames(game, 12000);
    game.watch_all(&chosen);
    check(field<int16_t>(0xDC4) == 660 && wave_jumps_are(0x75), "other code: set, not patched");
    build_ailments_code();
    field<int16_t>(0xDC4) = 0;
    game.watch_all(&chosen);
    build_ailments_code(&g_halt_flag);  // its loads read another word
    apply(game, "apply\npopup 103 FROM CAROL");
    next_frames(game, 12000);
    game.watch_all(&chosen);
    check(field<int16_t>(0xDC4) == 660 && wave_jumps_are(0x75), "another word: not patched");
    build_ailments_code();
    field<int16_t>(0xDC4) = 0;
    game.watch_all(&chosen);
    for (int i = 0; i < 100; ++i) game.pump_popups();

    // Halted, dead, in a menu, or in another save than its apply's: it waits, and its popup.
    field<int16_t>(0xDC2) = 0;
    apply(game, "apply\npopup 102 FROM DAN");
    g_halt_flag = 1;
    next_frames(game, 13000);
    check(field<int16_t>(0xDC2) == 0 && game.queued_traps() == 1 && game.queued_popups() == 1,
          "not while halted, nor its popup");
    g_halt_flag = 0;
    field<int16_t>(0xCEA) = 0;
    game.note_play(13000);
    next_frames(game, 15000);
    check(field<int16_t>(0xDC2) == 0 && game.queued_traps() == 1, "not while dead");
    field<int16_t>(0xCEA) = 48;
    game.note_play(15000);
    next_frames(game, 15100);
    check(field<int16_t>(0xDC2) == 0 && game.queued_popups() == 1,
          "not in the first moment back in play (a load's frame), nor its popup");
    next_frames(game, 15300);
    check(field<int16_t>(0xDC2) == 660 && game.queued_traps() == 0, "then with its popup");
    field<int16_t>(0xDC2) = 0;
    game.watch_all(&chosen);
    std::memset(g_section, 0, sizeof(g_section));
    g_section[0x26C] = 0x11;  // this seed's item table
    g_rando_marker = 1;
    apply(game, "apply\npopup 103 FROM DAN");
    check(std::string(game.unload_refusal() ? game.unload_refusal() : "") ==
              "a trap is waiting to go off",
          "an unload is refused while a trap for the save loaded waits");
    check(std::string(game.unload_blocker() ? game.unload_blocker() : "") ==
              "a trap is waiting to go off",
          "and one no frame came for, by the blocker published each frame");
    g_section[0x26C] = 0x22;  // another seed's save
    next_frames(game, 16000);
    check(field<int16_t>(0xDC4) == 0 && game.queued_traps() == 1, "another seed's save: it waits");
    check(game.unload_refusal() == nullptr && game.unload_blocker() == nullptr,
          "one for another save does not hold an unload");
    g_section[0x26C] = 0x11;
    game.client_gone(&chosen);  // the client gone meanwhile: kept, as its popups are
    next_frames(game, 16000);
    check(field<int16_t>(0xDC4) == 660 && game.queued_traps() == 0, "set in its own save");
    game.client_gone(&chosen);
    game.watch_all(&chosen);
    check(wave_jumps_are(0xEB), "a client going away leaves a running trap's patch");
    check(std::string(game.unload_refusal() ? game.unload_refusal() : "") == "a trap is running",
          "an unload is refused while it is held, before anything is put back");
    game.put_back_traps();
    check(wave_jumps_are(0x75) && game.unload_blocker() == nullptr, "put back for an unload");
    g_rando_marker = 0;

    // A trap's popup is never a notice, and only the two traps' codes are taken.
    check(apply(game, "apply\nnotice 102 HELLO").rfind("parse: bad line", 0) == 0, "no notice");
    check(apply(game, "apply\npopup 104 FROM X").rfind("parse: bad line", 0) == 0, "not 104");
    check(apply(game, "apply\ntrap 46").rfind("parse: bad line", 0) == 0, "no trap 46");
    reset_events();
}

// A received trap's popup takes its turn: after the popups queued before it (an OS Install
// Disk's and its count, as in check 2 of the 2026-11-11 run), the trap going off as its popup
// shows, not before; past popups waiting for another save; and the popups behind it waiting
// while it does.
void test_a_trap_popup_takes_its_turn() {
    zap::Game game;
    ready(game);
    trap_records();
    in_play_since(game, 5000);
    apply(game,
          "apply\npopup 44 FROM BOB\nnotice 44 DISK 1 OF 3\npopup 103 FROM BOB\n"
          "popup 46 FROM DAN");
    next_frames(game, 5000);
    check(g_text.find("FROM BOB") != std::string::npos && field<int16_t>(0xDC4) == 0,
          "the disk's popup first, the trap not set");
    next_frames(game, 5000);
    check(g_text.find("DISK 1 OF 3") != std::string::npos && field<int16_t>(0xDC4) == 0,
          "its count next, the trap still waiting");
    next_frames(game, 5000);
    check(g_text.find("DISORIENT TRAP") != std::string::npos && field<int16_t>(0xDC4) == 660,
          "then the trap's popup, the trap set as it shows");
    next_frames(game, 5000);
    check(g_text.find("FROM DAN") != std::string::npos, "then the rest");
    field<int16_t>(0xDC4) = 0;
    game.watch_all(&chosen);

    // A popup waiting for another save at the head does not hold it.
    std::memset(g_section, 0, sizeof(g_section));
    g_section[0x26C] = 0x11;
    g_rando_marker = 1;
    apply(game, "apply\npopup 46 FROM ERIN");  // for the 0x11 save
    g_section[0x26C] = 0x22;                     // another save loaded
    apply(game, "apply\npopup 103 FROM ALICE");
    next_frames(game, 6000);
    check(g_text.find("DISORIENT TRAP") != std::string::npos && field<int16_t>(0xDC4) == 660,
          "past ERIN's popup, waiting for its own save");
    g_section[0x26C] = 0x11;
    next_frames(game, 6000);
    check(g_text.find("FROM ERIN") != std::string::npos, "ERIN's in its own save");
    g_rando_marker = 0;
    field<int16_t>(0xDC4) = 0;
    game.watch_all(&chosen);

    // Not in play for a moment yet (a load's frame): the popups behind it wait with it.
    field<int16_t>(0xCEA) = 0;
    game.note_play(7000);
    field<int16_t>(0xCEA) = 48;
    game.note_play(7000);
    apply(game, "apply\npopup 103 FROM ALICE\npopup 46 FROM DAN");
    const int shown = g_messages;
    next_frames(game, 7100);
    check(g_messages == shown && game.queued_popups() == 2, "nothing shown before it");
    next_frames(game, 7300);
    check(g_text.find("DISORIENT TRAP") != std::string::npos && field<int16_t>(0xDC4) == 660,
          "then it, set as it shows");
    next_frames(game, 7300);
    check(g_text.find("FROM DAN") != std::string::npos, "then the one behind it");
    game.put_back_traps();
}

// This world's own trap, found in it: no popup of the helper's, set off at once, even while the
// game's pickup popup shows.
void test_own_traps() {
    zap::Game game;
    ready(game);
    trap_records();
    check(apply(game, "apply\ntrap 103") == "applied", "applied");
    check(game.queued_traps() == 1 && game.queued_popups() == 0, "queued, with no popup");
    message_showing(60);  // the game's pickup popup
    in_play_since(game, 3000);
    game.pump_traps(3000);
    check(field<int16_t>(0xDC4) == 660 && game.queued_traps() == 0, "set at once");
}

// A config with none of the AILMENTS keys (an offline cache written before the traps): the traps
// run, with AILMENTS as the game applies it (nothing patched).
void test_traps_without_ailments() {
    HMODULE self = GetModuleHandleW(nullptr);
    auto older = stand_in_config();
    for (const char* key : {"ailments", "ailments_hit_code", "ailments_wave_code"}) {
        older.erase(key);
    }
    reset_game();
    zap::Game game;
    check(game.configure(self, older, false).empty(), "configures without them");
    trap_records();
    apply(game, "apply\ntrap 103\ntrap 102");
    in_play_since(game, 3000);
    game.pump_traps(3000);
    game.pump_traps(3000);
    check(field<int16_t>(0xDC4) == 660 && field<int16_t>(0xDC2) == 660, "both set");
    check(hit_jumps_are(0x75) && wave_jumps_are(0x75), "nothing patched");
}

// Traps held through the game's ending, which reads as play, as drops are.
void test_traps_held_for_the_ending() {
    zap::Game game;
    ready(game);
    trap_records();
    g_flags[250] = 0;
    in_play_since(game, 1000);
    game.pump_traps(1000);  // the load's first frame in play: flag 250 clear
    apply(game, "apply\ntrap 102");
    g_flags[250] = 1;  // the ending began in this load
    game.pump_traps(5000);
    check(field<int16_t>(0xDC2) == 0 && game.queued_traps() == 1, "held through the ending");
    g_save_loaded = 0xFF;  // MISSION COMPLETE, then the title
    game.pump_traps(6000);
    g_save_loaded = 3;  // the save loaded again, past its ending
    game.note_play(7000);
    game.pump_traps(8000);
    check(field<int16_t>(0xDC2) == 660, "set at the next load, though flag 250 stays set");
    g_flags[250] = 0;
    game.put_back_traps();
    field<int16_t>(0xDC2) = 0;
    game.watch_all(&chosen);

    // A received one: its popup waits with it, and the popups behind it show meanwhile (they
    // show over the ending's cutscenes, as before).
    g_save_loaded = 0xFF;
    game.pump_traps(9000);
    g_save_loaded = 3;
    game.note_play(9000);
    game.pump_traps(10000);  // in play, flag 250 clear
    apply(game, "apply\npopup 102 FROM BOB\npopup 46 FROM DAN");
    g_flags[250] = 1;
    next_frames(game, 11000);
    check(field<int16_t>(0xDC2) == 0 && g_text.find("FROM DAN") != std::string::npos &&
              game.queued_traps() == 1,
          "held through the ending, the popup behind it shown");
    g_save_loaded = 0xFF;
    game.pump_traps(12000);
    g_save_loaded = 3;
    game.note_play(13000);
    next_frames(game, 14000);
    check(field<int16_t>(0xDC2) == 660 && g_text.find("DISRUPTION TRAP") != std::string::npos,
          "set at the next load as its popup shows");
    g_flags[250] = 0;
    game.put_back_traps();
}

void test_assist_watch() {
    zap::Game game;
    check(configure(game).empty(), "configures");
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

// Whether the stand-in game's on-screen marker write holds the game's bytes.
bool seen_marker_games() {
    return std::memcmp(g_seen_marker_code, kSeenMarkerGame, sizeof(kSeenMarkerGame)) == 0;
}

// The game's rule for listing a pickup (0x1F761 to 0x1F7A2), on the B1 records' icons.
static_assert(zap::pickups::listed(3, 0x77) && zap::pickups::listed(24, 0xC7) &&
              zap::pickups::listed(26, 0xE3) && zap::pickups::listed(32, 0xD2) &&
              zap::pickups::listed(44, 0xC0));
// The counts the list is read and written by: the game's, 0 to 26.
static_assert(zap::pickups::count_ok(0) && zap::pickups::count_ok(25) &&
              zap::pickups::count_ok(26));
static_assert(!zap::pickups::count_ok(-1) && !zap::pickups::count_ok(-128) &&
              !zap::pickups::count_ok(27) && !zap::pickups::count_ok(127));
static_assert(!zap::pickups::listed(0, 0xE7) && !zap::pickups::listed(2, 0xC0) &&
              !zap::pickups::listed(27, 0xE4) && !zap::pickups::listed(31, 0xC0) &&
              !zap::pickups::listed(45, 0xE6) && !zap::pickups::listed(36, 0xE8) &&
              !zap::pickups::listed(37, 0x6E) && !zap::pickups::listed(35, 0));

// In play in an AP save, each owned item the game lists joins the save's major-item list once,
// by item ID, up to the game's 26; nothing in other saves, menus, or a count the game never writes.
void test_pickup_list() {
    zap::Game game;
    ready(game);
    g_rando_marker = 1;
    for (int i = 0; i < 6; ++i) g_section[0x310 + i] = static_cast<uint8_t>(0x40 + i);
    auto icon = [](int item, uint8_t code) { g_records[item * 100 + 0x08] = code; };
    icon(24, 0xC7);  // Gravity Harness
    icon(16, 0xC0);  // Slide Mod
    icon(28, 0xE5);  // Beta Key: not listed
    icon(37, 0x6E);  // Bomb Box: not listed
    icon(36, 0xE8);  // Missile Box: not listed (reset_game gives it 0xC5)
    g_inventory[24] = 1;
    g_inventory[16] = 1;
    g_inventory[28] = 1;
    g_inventory[37] = 1;
    g_inventory[36] = 1;
    icon(3, 0x77);  // Machine Gun, which this new game started with (start-with entry 4)
    g_inventory[3] = 1;
    std::memset(g_section + 0x689, 0, 27);
    g_section[0x689 + 4] = 1;
    game.watch_all(&chosen);
    check(g_pickups[zap::pickups::kCount] == 0, "a randomizer save of no known seed: untouched");

    game.add_known_seeds({zap::offline_seed_of(g_section)});
    g_menu = 1;
    game.watch_all(&chosen);
    check(g_pickups[zap::pickups::kCount] == 0, "a menu open: not yet");
    g_menu = 0;
    game.watch_all(&chosen);
    check(g_pickups[zap::pickups::kCount] == 2 && g_pickups[0] == 0xC0 && g_pickups[1] == 0xC7,
          "the owned listed items, by item ID, but the started-with Machine Gun");
    game.watch_all(&chosen);
    check(g_pickups[zap::pickups::kCount] == 2, "each once");

    icon(20, 0xC3);  // Phaser, received
    g_inventory[20] = 1;
    game.watch_all(&chosen);
    check(g_pickups[zap::pickups::kCount] == 3 && g_pickups[2] == 0xC3, "a new one at the end");

    std::memset(g_pickups, 0, sizeof(g_pickups));
    g_pickups[0] = 0xC7;
    g_pickups[1] = 0xD2;
    g_pickups[zap::pickups::kCount] = 2;
    game.watch_all(&chosen);
    check(g_pickups[0] == 0xC7 && g_pickups[1] == 0xD2 && g_pickups[2] == 0xC0 &&
              g_pickups[3] == 0xC3 && g_pickups[zap::pickups::kCount] == 4,
          "the game's own entries kept, in their order");

    std::memset(g_pickups, 0x77, zap::pickups::kMax);
    g_pickups[zap::pickups::kCount] = zap::pickups::kMax;
    game.watch_all(&chosen);
    check(g_pickups[zap::pickups::kCount] == zap::pickups::kMax, "a full list stays full");
    g_pickups[zap::pickups::kCount] = 30;
    game.watch_all(&chosen);
    check(g_pickups[zap::pickups::kCount] == 30, "a count out of range: not touched");
    g_pickups[zap::pickups::kCount] = static_cast<BYTE>(-1);
    game.watch_all(&chosen);
    check(g_pickups[zap::pickups::kCount] == 0xFF, "nor a negative one");

    std::memset(g_pickups, 0, sizeof(g_pickups));
    g_rando_marker = 0;
    game.watch_all(&chosen);
    check(g_pickups[zap::pickups::kCount] == 0, "a normal save: untouched");
}

// The map's squares are raised in the labels' seed's save with a menu open too (the player may
// pause and open the map at once), after two looks in a row (every 16th frame) read that seed.
void test_map_marks_with_a_menu_open() {
    zap::Game game;
    ready(game);
    g_rando_marker = 1;
    std::memset(g_section, 0, 4);
    g_section[0] = 3;  // location 0 at (3, 4)
    g_section[2] = 4;
    g_section[0x26C] = 48;  // a progression stand-in
    g_map_grid[4 * 54 + 3] = 0x19;  // shown as a minor item
    std::map<int, zap::Label> labels;
    zap::parse_labels("ap_labels\nlabel 3 4 414C494345", labels);
    game.set_labels(labels);
    g_menu = 1;  // paused
    game.watch_all(&chosen);
    check(g_map_grid[4 * 54 + 3] == 0x19, "the first look: not yet (a load may not have settled)");
    check(seen_marker_games(), "the on-screen write not yet either");
    for (int i = 0; i < 16; ++i) game.watch_all(&chosen);
    check(g_map_grid[4 * 54 + 3] == 0x1A, "the next: raised, with a menu open");
    check(!seen_marker_games() && code_byte(g_seen_marker_code, 0) == 0x80 &&
              code_byte(g_seen_marker_code, 2) == 0x0C,
          "and the on-screen write the helper's");
    check(game.unload_blocker() != nullptr &&
              std::string(game.unload_blocker()) ==
                  "the map's on-screen marker is still the helper's",
          "an unload waits for it");
    g_map_grid[4 * 54 + 3] = 0x19;
    const BYTE was = g_section[0x26D];
    g_section[0x26D] = 0x2F;
    game.watch_all(&chosen);
    g_section[0x26D] = was;
    for (int i = 0; i < 16; ++i) game.watch_all(&chosen);
    check(g_map_grid[4 * 54 + 3] == 0x19 && seen_marker_games(),
          "another save for one frame between passes: the next pass is the first again");
    for (int i = 0; i < 16; ++i) game.watch_all(&chosen);
    check(g_map_grid[4 * 54 + 3] == 0x1A && !seen_marker_games(), "and the one after raises");
    g_map_grid[4 * 54 + 3] = 0x19;
    g_section[0x26D] = 0x2F;  // another seed's save
    game.watch_all(&chosen);
    check(seen_marker_games(), "another seed's save: the game's write in its first frame");
    for (int i = 0; i < 16; ++i) game.watch_all(&chosen);
    check(g_map_grid[4 * 54 + 3] == 0x19, "not in another seed's save");
    g_section[0x26D] = was;  // the labels' seed's again: counted afresh
    for (int i = 0; i < 16; ++i) game.watch_all(&chosen);
    check(g_map_grid[4 * 54 + 3] == 0x19, "a look of another seed starts the count again");
    check(seen_marker_games(), "nor the write yet");
    for (int i = 0; i < 16; ++i) game.watch_all(&chosen);
    check(g_map_grid[4 * 54 + 3] == 0x1A, "then the second look raises them");
    check(!seen_marker_games(), "and patches the write again");
    game.client_gone(&chosen);
    check(seen_marker_games() && game.unload_blocker() == nullptr,
          "a client going away: the game's write");
    for (int i = 0; i < 32; ++i) game.watch_all(&chosen);
    check(seen_marker_games(), "and no labels: not patched again");
    g_menu = 0;
    g_rando_marker = 0;
    g_map_grid[4 * 54 + 3] = 0;
}

// Item 49 as the game loads it: AREA MAP, its event the map reveal (as intel_tests.cpp).
void area_map_as_loaded() {
    BYTE* r = g_records + 49 * 100;
    put<int16_t>(r, 0x00, 34);
    put<int16_t>(r, 0x02, 25);
    put<int16_t>(r, 0x04, 6);
    put<int16_t>(r, 0x06, 2);
    r[0x08] = 0;
    r[0x09] = 3;
    std::memcpy(r + 0x0A, "AREA MAP", 9);
    put<int16_t>(r, 0x3C, 46);
    put<int16_t>(r, 0x40, 3551);
    put<int16_t>(r, 0x58, 1);
}

// An unload's put back leaves the game's memory as the game's but keeps what the client asked
// for, so a refused unload's watches apply it all again (the client does not know to resend it);
// a client gone drops it.
void test_a_refused_unload_keeps_what_the_client_asked() {
    zap::Game game;
    ready(game);
    reset_sprites();
    reset_texts();
    reset_events();
    own_stand_ins(g_records);
    area_map_as_loaded();
    stand_in::OptionsMenu options(reinterpret_cast<void*>(&widget_handler_fn),
                                  reinterpret_cast<void*>(&assist_menu_fn));
    g_current_menu = options.menu;
    g_rando_marker = 1;
    g_section[0x26C] = 0x2F;  // the loaded seed the labels are for
    std::map<int, zap::Label> labels;
    zap::parse_labels("ap_labels\nlabel 3 4 414C494345", labels);
    game.set_labels(labels);
    zap::NewGamePlan plan;
    zap::parse_newgame("ap_newgame\nexpect 0 00\nwrite 2 01\nmenu 0 01\nidentify 0x658 41\nchoice",
                       plan);
    game.arm_newgame(plan);
    game.set_assists_locked(true);
    game.set_intel(true);
    game.set_discs(3, 15, "COLLECT 15 OS INSTALL DISKS!", true);
    game.set_disk_slots({{2, 3}, {5, 3}});
    player_cell(3, 4);  // the label's cell
    // The stand-ins and INTEL CHIP restyled, ASSIST locked, the scanner's line written.
    auto applied = [&] {
        return game.ap_items_restyled() && record_name(49) == "INTEL CHIP" &&
               options.callback() == nullptr && game.scan_written();
    };
    auto games = [&] {
        return !game.ap_items_restyled() && record_name(47) == "SCRAP ENERGY" &&
               record_name(48) == "SCRAP CHUNK" && record_name(49) == "AREA MAP" &&
               options.callback() == &assist_menu_fn && !game.scan_written();
    };
    game.watch_all(&chosen);
    check(applied(), "in play: applied");
    game.request_mission(kPodColumn, kPodRow);  // asked for, not yet opened

    game.put_back_game(&chosen);
    check(games() && game.unload_blocker() == nullptr,
          "put back: the game's records, ASSIST and scanner line, nothing left to wait for");
    check(game.newgame_status() == "armed" && game.assists_locked() && game.intel_on() &&
              game.disc_count() == 3 && game.discs_required() == 15 && game.discs_told() &&
              game.scan_line() == "COLLECT 15 OS INSTALL DISKS!" &&
              game.files().counts() == std::map<int, int>{{2, 3}, {5, 3}} &&
              game.labels().count(4 * 54 + 3) == 1 && game.mission_status() == "asked",
          "what the client asked for kept: the new game, ASSIST, INTEL CHIP, the disks, the "
          "file select's counts, the labels, RETURN HOME WITH THE DISKS?");
    for (int i = 0; i < 32; ++i) game.watch_all(&chosen);  // the scanner's look every 30th
    check(applied(), "the unload refused: the watches apply it all again");

    game.client_gone(&chosen);
    check(games() && game.newgame_status() == "idle" && !game.assists_locked() &&
              !game.intel_on() && game.scan_line().empty() && game.files().counts().empty() &&
              game.labels().empty() && game.mission_status() == "not asked",
          "a client gone: put back and dropped");
    for (int i = 0; i < 32; ++i) game.watch_all(&chosen);
    check(games(), "and not applied again");
    g_current_menu = nullptr;
    g_rando_marker = 0;
}

// An unload refused after ARCHIPELAGO's new game was queued: still started, so the plan is
// applied without the mode check and ARCHIPELAGO is not offered again.
void test_refused_unload_keeps_a_start() {
    zap::Game game;
    ModeScreen screen(game);
    zap::NewGamePlan plan;
    zap::parse_newgame(
        "ap_newgame\nexpect 0x6C0 0300\nwrite 0x26C 2E1B\nmenu 0x88 0300\nwrite 0x6C0 0500\n"
        "identify 0x658 41\nchoice",
        plan);
    screen.start(plan);
    check(g_steps == "placement queue ", "ARCHIPELAGO pressed: the new game queued");

    game.put_back_game(&chosen);
    game.watch_menu(&chosen);
    check(!game.menu_installed(), "kept as started: ARCHIPELAGO not offered again");
    game.watch_newgame();
    check(game.newgame_status() == "applied slot 3" && g_section[0x6C0] == 5,
          "the queued new game gets the plan, the mode check skipped as before");
}

// The pause menu's way back from OPTION MENU wrapped, as dllmain.cpp's wrapper: the game's, then
// options_returned.
zap::Game* g_back_game = nullptr;
void options_back(void* menu) {
    reinterpret_cast<void (*)(void*)>(g_back_game->options_back_fn())(menu);
    g_back_game->options_returned(menu);
}

// A known disk seed's save loaded before the client sets its count gets its pause row from the
// seed's file, so a pause screen opened at once shows it.
void test_pause_row_from_the_seed_file() {
    zap::Game game;
    ready(game);  // a client, its count not set yet
    g_rando_marker = 1;
    zap::OfflineSeed seed = zap::offline_seed_of(g_section);
    seed.discs = true;
    seed.count = 2;
    seed.required = 15;
    seed.unfound = {4 * 54 + 3};
    g_map_grid[4 * 54 + 3] = zap::kCollectedCell;  // one more found since the file
    game.set_offline({seed});
    stand_in::PauseScreen p(reinterpret_cast<void*>(&equipment_fn), nullptr,
                            reinterpret_cast<void*>(&widget_handler_fn));
    g_template_list[1] = p.font;
    g_current_menu = p.menu;
    game.watch_all(&chosen);
    check(game.keys_branch_patched() && game.disc_count() == 0, "the row built, no count set");
    check(p.frame(p.sprite[1]) == '3' && p.frame(p.sprite[2]) == '?' &&
              p.frame(p.sprite[4]) == 829,
          "the file's count on the pause screen at once (\" 3 OF ? \")");
    // OPTION MENU over it: an unload waits for the wrapped way back.
    zap::Hooks hooks;
    hooks.options_back = reinterpret_cast<void*>(&options_back);
    game.set_hooks(hooks);
    g_back_game = &game;
    void* const game_back = reinterpret_cast<void*>(&options_back_fn);
    BYTE options[0x80] = {};
    put<BYTE*>(options, 0x78, p.menu);
    put<void*>(p.menu, 0x68, game_back);
    g_current_menu = options;
    game.watch_all(&chosen);
    check(game.menu_installed() && game.unload_blocker() != nullptr &&
              get<void*>(p.menu, 0x68) == hooks.options_back,
          "OPTION MENU open: the pause menu's way back is the helper's, an unload waits");
    const int backs = g_options_backs;
    g_current_menu = p.menu;
    reinterpret_cast<void (*)(void*)>(get<void*>(p.menu, 0x68))(p.menu);
    check(g_options_backs == backs + 1 && !game.menu_installed() &&
              p.frame(p.sprite[1]) == '3' && p.frame(p.glyph[1]) == 'O',
          "closed: the game's way back ran, the row as it was, nothing left to put back");
    put<void*>(p.menu, 0x68, game_back);
    g_current_menu = options;
    game.watch_all(&chosen);
    check(game.menu_installed(), "opened again: wrapped again");
    game.put_back_game(&chosen);
    check(!game.keys_branch_patched() && !game.menu_installed() &&
              get<void*>(p.menu, 0x68) == game_back,
          "put back: the branch and the way back");
    g_current_menu = p.menu;

    g_section[0x26D] ^= 1;  // another seed's save
    stand_in::PauseScreen q(reinterpret_cast<void*>(&equipment_fn), nullptr,
                            reinterpret_cast<void*>(&widget_handler_fn));
    g_current_menu = q.menu;
    game.watch_all(&chosen);
    check(!game.keys_branch_patched() && q.frame(q.glyph[0]) == 'K', "no known seed: KEYS");
    g_section[0x26D] ^= 1;
    game.put_back_game(&chosen);
    g_current_menu = nullptr;
    g_template_list[1] = nullptr;
    g_map_grid[4 * 54 + 3] = 0;
    g_rando_marker = 0;
}

// A client going away leaves its popups to show (their items are given) and takes its
// other-seed notice with it.
void test_queued_popups() {
    zap::Game game;
    check(game.queued_popups() == 0, "none before a config");
    ready(game);
    apply(game, "apply\npopup 36 FROM ALICE\npopup 36 FROM BOB\nnotice 36 HINT: X");
    check(game.queued_popups() == 3, "three from the client");
    game.pump_popups();
    check(g_messages == 1 && game.queued_popups() == 2, "one shown: two queued");
    g_rando_marker = 1;
    game.set_offline({zap::offline_seed_of(g_section)});  // an AP save of a known seed
    check(game.other_save() == "told" && game.queued_popups() == 3,
          "the helper's own notice queued");
    g_rando_marker = 0;
    game.client_gone(&chosen);
    check(!game.active() && game.queued_popups() == 2,
          "the client gone: its popups stay queued, its other-seed notice withdrawn");
    for (int i = 0; i < 100; ++i) game.pump_popups();
    check(game.queued_popups() == 0 && g_messages >= 3, "and show without it");
    zap::Batch batch;
    zap::parse_batch("apply\npopup 36 X\nnotice 36 Y", batch);
    check(batch.popups.size() == 2 && batch.popups[0].counted && batch.popups[1].counted,
          "a batch's popups are counted");
}

// A client's popups are for the save loaded when they came: while another save is in play they
// wait (still queued), and the helper's own notices still show.
void test_popups_wait_for_their_save() {
    zap::Game game;
    ready(game);
    std::memset(g_section, 0, sizeof(g_section));
    g_section[0x26C] = 0x11;  // this seed's item table
    g_rando_marker = 1;
    apply(game, "apply\npopup 36 FROM ALICE");
    g_section[0x26C] = 0x22;  // the player left: another seed's save in play
    game.set_offline({zap::offline_seed_of(g_section)});
    check(game.other_save() == "told", "the other seed's notice queued behind it");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_messages == 1 && game.queued_popups() == 1,
          "the notice shows; the client's popup waits, queued");
    g_rando_marker = 0;  // a normal save
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_messages == 1 && game.queued_popups() == 1, "and in a normal save");
    g_rando_marker = 1;
    g_section[0x26C] = 0x11;  // its save again
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_messages == 2 && game.queued_popups() == 0, "shown in its own save");
    // A client of another seed: the first's popups held for their own save (the client never
    // sends them again).
    apply(game, "apply\npopup 36 FROM ALICE");
    g_section[0x26C] = 0x22;
    check(game.queued_popups() == 1, "held for its seed");
    apply(game, "apply\npopup 36 FROM BOB");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_messages == 3 && game.queued_popups() == 1,
          "another seed's client: its own shown, the first's still held");
    g_section[0x26C] = 0x11;
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_messages == 4 && game.queued_popups() == 0, "and shown in its own save");
    g_rando_marker = 0;
}

// RETURN HOME WITH THE DISKS? (mission.h) through the Game, with stand-in routines. Each has a
// body of its own: the linker folds identical functions into one address.
int g_routine_runs = 0;
void idle_routine() { g_routine_runs += 1; }
void prompt_routine() { g_routine_runs += 2; }  // builds nothing: the opener then reports it
void opener() { g_routine_runs += 3; }  // the helper's (dllmain.cpp); not run here
void ender() { g_routine_runs += 5; }   // the helper's ending wrapper: Game::mission_end stands in
void after_ending() { g_routine_runs += 6; }  // 0x948A0
// As B1's with no menu open: the level to 4 (0x95230), 0x948A0 the transition with timer 1.
void ending_routine() {
    g_routine_runs += 4;
    g_fade_level = 4;
    g_transition_fn = reinterpret_cast<void*>(&after_ending);
    g_transition_timer = 1;
}
struct MissionGame {
    zap::Game game;
    zap::Hooks hooks;
    MissionGame() {
        ready(game);
        reset_events();
        std::memset(g_section, 0, sizeof(g_section));
        player_cell(13, 16);  // at the pod
        hooks.mission_open = reinterpret_cast<void*>(&opener);
        hooks.mission_end = reinterpret_cast<void*>(&ender);
        game.set_hooks(hooks);
        g_routine_table[zap::kIdleRoutine] = reinterpret_cast<void*>(&idle_routine);
        g_routine_table[zap::kPromptRoutine] = reinterpret_cast<void*>(&prompt_routine);
        g_routine_table[zap::kEndingRoutine] = reinterpret_cast<void*>(&ending_routine);
        idle();
    }
    ~MissionGame() {
        for (int i : {zap::kIdleRoutine, zap::kPromptRoutine, zap::kEndingRoutine}) {
            g_routine_table[i] = nullptr;
        }
        g_transition_fn = nullptr;
        g_transition_timer = 0;
        g_rando_marker = 0;
        std::memset(g_section, 0, sizeof(g_section));
        reset_game();
    }
    MissionGame(const MissionGame&) = delete;
    MissionGame& operator=(const MissionGame&) = delete;

    static void idle() {
        g_transition_fn = g_routine_table[zap::kIdleRoutine];
        g_transition_timer = 0;
    }
    // All of it put back, then the events fresh, idle again and the hooks set again.
    void again() {
        game.put_back_game(&chosen);
        reset_events();
        idle();
        game.set_hooks(hooks);
    }
};

// Popups wait from the fade until the opener runs; the fade waits until the last message has
// kFadeMessageLeft frames left (GAME SAVED's, half a second in).
void test_popups_wait_for_the_mission_fade() {
    MissionGame m;
    zap::Game& game = m.game;
    game.request_mission(kPodColumn, kPodRow);
    message_showing(zap::kFadeMessageLeft + 1);  // GAME SAVED, 180 frames: 29 in
    game.watch_all(&chosen);
    check(g_transition_fn == g_routine_table[zap::kIdleRoutine] && g_fade_calls == 0,
          "not before the message is kFadeMessageLeft frames from its end");
    message_showing(zap::kFadeMessageLeft);  // 30 in
    game.watch_all(&chosen);
    check(g_transition_timer == zap::kFadeDelay && g_fade_calls == 1 &&
              g_fade_event == event_3667() && g_transition_fn == reinterpret_cast<void*>(&opener) &&
              game.mission_status() == "opening after the fade",
          "idle: the fade, the opener 60 frames away");
    message_showing(1);  // GAME SAVED over: only the fade holds a popup back
    game.note_play(1000);
    check(game.kill(3000) == "refused RETURN HOME WITH THE DISKS? opening" && g_deaths == 0,
          "no received death between the fade and the prompt");
    apply(game, "apply\npopup 36 FROM ALICE");
    for (int i = 0; i < 60; ++i) game.pump_popups();
    check(g_messages == 0 && game.queued_popups() == 1, "a popup waits through the fade");
    game.mission_open();  // the game runs the opener
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_messages == 1, "then shows");

    // The helper's own popup is never cut short: the fade waits for its end.
    m.again();
    reset_game();
    player_cell(13, 16);  // the player state cleared: back at the pod
    game.request_mission(kPodColumn, kPodRow);
    g_slots_live = true;  // the game makes each new message its last one (0x182CEB)
    g_message_is_last = true;
    BYTE* own = g_slots + 0x1628 * g_message_slot;
    apply(game, "apply\npopup 36 FROM BOB");
    game.pump_popups();
    check(g_messages == 1 && g_last_message.last == own, "the helper's popup shows");
    for (int i = 0; i < 300; ++i) game.pump_popups();  // more calls than frames: a fast monitor
    put<int32_t>(own, 0x1EC, zap::kFadeMessageLeft);
    game.watch_all(&chosen);
    check(g_fade_calls == 0, "not with kFadeMessageLeft frames of the helper's popup left");
    put<int32_t>(own, 0x1EC, 2);
    game.watch_all(&chosen);
    check(g_fade_calls == 0, "nor with 2 left");
    put<int32_t>(own, 0x1EC, 1);
    game.watch_all(&chosen);
    check(g_fade_calls == 1, "once it has ended");
    g_slots_live = false;
    g_message_is_last = false;

    // Dead when the opener runs: the fade turned back (a step every 6 frames), asked again.
    field<int16_t>(0xCEA) = -1;
    game.mission_open();
    check(game.mission_status() == "asked again: not in play when it opened" &&
              g_transition_fn == g_routine_table[zap::kIdleRoutine] && g_transition_timer == 0 &&
              g_fade_calls == 2 && g_fade[zap::fade_state::kTarget] == 4 &&
              g_fade[zap::fade_state::kStep] == 6,
          "a death during the fade: put off, the fade turned back");
    field<int16_t>(0xCEA) = 48;  // respawned
    game.watch_all(&chosen);
    check(g_fade_calls == 3 && g_transition_timer == zap::kFadeDelay, "then offered again");
    game.put_back_game(&chosen);
}

// A received trap's popup waiting for one of its kind to run out does not hold RETURN HOME's
// fade, as no popup of its did before it went off.
void test_a_parked_trap_popup_does_not_hold_the_mission() {
    MissionGame m;
    zap::Game& game = m.game;
    trap_records();
    player_cell(13, 16);  // back at the pod (scrap_records moves the player)
    in_play_since(game, 5000);
    apply(game, "apply\npopup 102 FROM ALICE");
    next_frames(game, 5000);
    check(field<int16_t>(0xDC2) == 660, "the first trap set");
    apply(game, "apply\npopup 102 FROM BOB");
    for (int i = 0; i < 30; ++i) game.pump_popups();  // the helper's gap since its popup
    game.request_mission(kPodColumn, kPodRow);
    game.watch_all(&chosen);
    check(g_fade_calls == 1 && game.queued_popups() == 1,
          "the second's popup, waiting for the first to run out, does not hold the fade");
    game.put_back_traps();
    game.put_back_game(&chosen);
}

// A drop waits through RETURN HOME WITH THE DISKS?'s fade, as the popups do.
void test_drops_wait_for_the_mission_fade() {
    MissionGame m;
    zap::Game& game = m.game;
    scrap_records();
    player_cell(13, 16);  // at the pod
    game.note_play(1000);
    apply(game, "apply\npopup 46 FROM ALICE");
    show_next(game);
    check(game.queued_drops() == 1, "a drop queued");
    for (int i = 0; i < 30; ++i) game.pump_popups();  // the gap after the helper's popup
    game.request_mission(kPodColumn, kPodRow);
    message_showing(zap::kFadeMessageLeft);
    game.watch_all(&chosen);
    check(game.mission_status() == "opening after the fade", "the fade");
    message_showing(1);
    game.pump_drops(5000);
    check(g_spawn_calls == 0 && game.queued_drops() == 1, "the drop waits through the fade");
    reset_events();
}

// A save during the fade puts the pause opener back: the fade turned back and the prompt asked
// again, except under a door's own fade.
void test_a_save_during_the_mission_fade() {
    for (bool door : {false, true}) {
        MissionGame m;
        zap::Game& game = m.game;
        game.request_mission(kPodColumn, kPodRow);
        message_showing(zap::kFadeMessageLeft);
        game.watch_all(&chosen);
        check(g_fade_calls == 1 && g_transition_fn == reinterpret_cast<void*>(&opener),
              "the fade, the opener 60 frames away");
        g_transition_timer = 40;  // the main loop's count (the stand-in's fade does not step)
        game.watch_all(&chosen);
        if (door) {
            g_transition_fn = reinterpret_cast<void*>(&prompt_routine);  // a door, with its fade
            g_fade[zap::fade_state::kStep] = 12;
        } else {
            g_transition_fn = g_routine_table[zap::kIdleRoutine];  // saved again
        }
        game.watch_all(&chosen);
        const std::string turned_back = door ? "" : ", the fade turned back";
        check(g_fade_calls == (door ? 1 : 2) && !game.menu_installed() &&
                  game.mission_status() == "asked again: another transition came first" +
                                               turned_back,
              door ? "a door's fade left alone" : "a save: the fade turned back");
        if (!door) {
            check(g_fade[zap::fade_state::kTarget] == 4 && g_fade[zap::fade_state::kStep] == 6,
                  "toward the normal level, a step every 6 frames");
        }
        game.put_back_game(&chosen);
    }
}

// The `mission_prompt` request: home's save point in tiles, column then row, on the map.
void test_the_mission_request_gives_home() {
    int column = 0, row = 0;
    check(zap::parse_mission("mission_prompt 191 329", column, row).empty() && column == 191 &&
              row == 329,
          "the Landing Pod's save point");
    check(zap::parse_mission("mission_prompt 755 1079", column, row).empty(), "the map's last");
    for (const char* bad : {"mission_prompt", "mission_prompt 191", "mission_prompt 191 329 1",
                            "mission_prompt 756 329", "mission_prompt 191 1080",
                            "mission_prompt -1 329", "mission_prompt 191 x"}) {
        check(!zap::parse_mission(bad, column, row).empty() && column == -1 && row == -1, bad);
    }
}

// Home the Landing Pod, the seed starting elsewhere: offered only at the pod, dropped once the
// player is in play outside its cell; after YES the ending waits for the helper's own popup.
void test_return_home_lapses_away_from_the_pod() {
    MissionGame m;
    zap::Game& game = m.game;
    g_rando_marker = 1;
    // The start: candidate 17 (Open Cave), tiles (333, 449), cell row 22, column 23. The helper
    // does not read it: home is what the request gives.
    put<uint16_t>(g_section, 0x310, 17);
    put<uint16_t>(g_section, 0x312, 333);
    put<uint16_t>(g_section, 0x314, 449);

    check(!game.request_mission(-1, kPodRow) && !game.request_mission(kPodColumn, 1080) &&
              game.mission_status() == "not asked",
          "a home off the map refused");

    player_cell(23, 22);  // the seed's start
    message_showing(1);
    game.request_mission(kPodColumn, kPodRow);
    game.watch_all(&chosen);
    check(g_fade_calls == 0 && game.mission_status() == "lapsed: the player left the landing pod",
          "at the seed's start, not the pod: lapsed");

    player_cell(13, 16);  // the pod
    message_showing(1);
    game.request_mission(kPodColumn, kPodRow);
    game.watch_all(&chosen);
    check(g_fade_calls == 1 && game.mission_status() == "opening after the fade",
          "in the start cell: offered");

    // Popups hold it; the player walks into the next cell meanwhile.
    m.again();
    const int fades = g_fade_calls;
    game.request_mission(kPodColumn, kPodRow);
    apply(game, "apply\npopup 36 FROM ALICE\npopup 36 FROM BOB");
    game.pump_popups();
    game.watch_all(&chosen);
    check(g_fade_calls == fades && game.mission_status() == "asked", "held by the popups");
    player_cell(0xFF, 0xFF);  // the raw cell -1 for a moment: not known
    game.watch_all(&chosen);
    check(game.mission_status() == "asked", "off the map: not lapsed");
    player_cell(14, 16);
    game.watch_all(&chosen);
    check(game.mission_status() == "lapsed: the player left the landing pod",
          "outside the start cell: lapsed");
    for (int i = 0; i < 600; ++i) {
        game.pump_popups();
        message_showing(1);
        game.watch_all(&chosen);
    }
    check(game.queued_popups() == 0 && g_fade_calls == fades &&
              g_transition_fn == g_routine_table[zap::kIdleRoutine],
          "the popups shown, and no fade or prompt after them");
    player_cell(13, 16);
    game.watch_all(&chosen);
    check(g_fade_calls == fades, "back at the pod: not offered again until asked again");

    // Dead outside the cell: not in play, so not lapsed; respawned at the pod, offered.
    game.request_mission(kPodColumn, kPodRow);
    player_cell(13, 17);
    field<int16_t>(0xCEA) = -1;
    game.watch_all(&chosen);
    check(game.mission_status() == "asked", "dead: not lapsed");
    player_cell(13, 16);
    field<int16_t>(0xCEA) = 48;
    game.watch_all(&chosen);
    check(g_fade_calls == fades + 1, "respawned at the pod: offered");

    // Dead when the opener runs, the death having set a fade of its own: left alone.
    field<int16_t>(0xCEA) = -1;
    g_fade[zap::fade_state::kTarget] = 4;  // not event 3667's
    game.mission_open();
    check(game.mission_status() == "asked again: not in play when it opened" &&
              g_fade_calls == fades + 1 && g_transition_fn == g_routine_table[zap::kIdleRoutine],
          "put off, the death's fade not turned back");
    field<int16_t>(0xCEA) = 48;

    m.again();
    g_slots_live = true;
    g_message_is_last = true;
    BYTE* popup = g_slots + 0x1628 * g_message_slot;
    g_last_message = {nullptr, 0};  // GAME SAVED over
    int shown = g_messages;
    apply(game, "apply\npopup 36 FROM BOB");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_messages == shown + 1 && g_last_message.last == popup, "the helper's popup up");
    game.mission_yes();
    put<int32_t>(popup, 0x1EC, 2);
    game.watch_all(&chosen);
    check(g_transition_fn == g_routine_table[zap::kIdleRoutine],
          "with the helper's popup up: the ending waits");
    put<int32_t>(popup, 0x1EC, 1);
    game.watch_all(&chosen);
    check(g_transition_fn == m.hooks.mission_end, "then comes as it ends");
    g_slots_live = false;
    g_message_is_last = false;
    // The ending waits for no message of the game's (the ending routine ends them, 0x1F9CE0).
    m.again();
    check(!game.ending_pending() && game.unload_refusal() == nullptr, "nothing pending");
    g_menu = 2;
    check(game.unload_refusal() == zap::kMenuOpenRefusal, "a menu open: an unload refuses");
    g_menu = 0;
    game.mission_yes();
    check(game.ending_pending() && game.unload_refusal() == zap::kEndingRefusal,
          "YES: the ending pending, and an unload refuses");
    message_showing(150);  // GAME SAVED, 30 frames in
    game.watch_all(&chosen);
    check(g_transition_fn == m.hooks.mission_end && game.mission_status() == "ending started",
          "with the game's message still up: the ending at once");
    check(!game.ending_pending() && game.unload_refusal() == nullptr,
          "started, with no ending patch: nothing an unload would undo");

    game.put_back_game(&chosen);
}

// A last message the helper's call did not make new (the game makes each new message the last
// one, with a new sequence number, 0x182CEB) is not taken for its popup: the ending does not wait.
void test_own_popup_needs_a_new_last_message() {
    MissionGame m;
    zap::Game& game = m.game;
    message_showing(1);  // the game's message, the last one, at its end
    const int shown = g_messages;
    apply(game, "apply\npopup 36 FROM BOB");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_messages == shown + 1 && g_last_message.last == g_message &&
              g_last_message.sequence == 7,
          "the popup shown into the same slot, the last message unchanged");
    put<int32_t>(g_message, 0x1EC, 150);  // that message up again, as if still showing
    game.mission_yes();
    game.watch_all(&chosen);
    check(g_transition_fn == m.hooks.mission_end,
          "not the helper's popup: the ending at once after YES");
    game.put_back_game(&chosen);
}

// After YES the screen stays black past NO's close into the ending, refusing deaths, popups held
// for the next load; once started, not offered again until another save is loaded.
void test_the_black_after_yes() {
    MissionGame m;
    zap::Game& game = m.game;
    game.mission_yes();
    set_fade(4, -1, 6);  // NO's close: the builder's fade put back
    game.mission_after_no();
    check(g_fade_level == -1 && at<int8_t>(g_fade, zap::fade_state::kTarget) == -1 &&
              g_fade[zap::fade_state::kStep] == 0 &&
              game.mission_status() == "yes: ending when idle, the screen kept black",
          "YES: black, the fade off");
    message_showing(1);
    game.note_play(1000);
    check(game.kill(3000) == "refused RETURN HOME WITH THE DISKS? ending" && g_deaths == 0,
          "no received death in the black");
    apply(game, "apply\npopup 36 FROM ALICE");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_messages == 0 && game.queued_popups() == 1, "a popup waits in the black");
    game.watch_all(&chosen);
    check(g_fade_level == -1 && g_fade[zap::fade_state::kStep] == 0 &&
              g_transition_fn == m.hooks.mission_end &&
              game.mission_status() == zap::kEndingStarted,
          "the ending at once, from black, the popup still waiting");
    g_transition_timer = 0;
    game.mission_end();  // the game runs the wrapper
    g_transition_fn = nullptr;
    g_save_loaded = 0xFF;  // MISSION COMPLETE: no save loaded
    game.watch_all(&chosen);
    g_save_loaded = 3;  // loaded again
    game.watch_all(&chosen);
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_messages == 1 && game.queued_popups() == 0, "the popup at the next load");

    // YES with the fade not as NO's close leaves it: not kept black, the popup still kept for
    // the next load and the ending at once.
    m.again();
    message_showing(1);
    game.mission_yes();
    set_fade(4, -1, 12);  // not NO's fade
    game.mission_after_no();
    check(g_fade[zap::fade_state::kStep] == 12 && game.mission_black() != "kept black",
          "not kept black: the fade left as it is");
    apply(game, "apply\npopup 36 FROM BOB");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_messages == 1 && game.queued_popups() == 1, "the popup waits after YES");
    game.watch_all(&chosen);
    check(g_transition_fn == m.hooks.mission_end &&
              game.mission_status() == zap::kEndingStarted,
          "the ending at once, the popup still waiting");
    g_transition_timer = 0;
    game.mission_end();
    g_transition_fn = nullptr;
    g_save_loaded = 0xFF;
    game.watch_all(&chosen);
    g_save_loaded = 3;
    game.watch_all(&chosen);
    for (int i = 0; i < 30; ++i) game.pump_popups();
    check(g_messages == 2 && game.queued_popups() == 0, "shown at the next load");

    // Canceled (`discs` off) under a menu that is not the game's menu state.
    m.again();
    game.mission_yes();
    set_fade(4, -1, 6);
    game.mission_after_no();
    BYTE pause[8] = {};
    g_current_menu = pause;
    game.set_discs(0, 3, "", false);
    check(!game.mission_status().empty() && game.ending_pending() &&
              game.unload_refusal() == zap::kEndingRefusal,
          "the black alone: an unload refuses");
    g_current_menu = nullptr;
    game.watch_all(&chosen);
    check(!game.ending_pending() && game.unload_refusal() == nullptr &&
              at<int8_t>(g_fade, zap::fade_state::kTarget) == 4,
          "the menu closed: back in, nothing pending");
    game.set_discs(3, 3, "COLLECT 3 OS INSTALL DISKS!", true);

    m.again();
    game.mission_yes();
    set_fade(4, -1, 6);
    game.mission_after_no();
    message_showing(1);
    game.watch_all(&chosen);
    check(g_transition_fn == m.hooks.mission_end && g_fade_level == -1 &&
              g_fade[zap::fade_state::kStep] == 0,
          "the ending, from black");
    g_transition_timer = 0;  // the game runs the wrapper, which runs the routine
    int runs = g_routine_runs;
    game.mission_end();
    check(g_routine_runs == runs + 4 && g_transition_fn == reinterpret_cast<void*>(&after_ending) &&
              g_fade_level == -1 && at<int8_t>(g_fade, zap::fade_state::kTarget) == 4 &&
              g_fade[zap::fade_state::kStep] == zap::kEndingFadeStep && !game.ending_pending(),
          "run: drawn black, fading in from there; nothing pending");
    g_transition_fn = nullptr;  // 0x948A0 run: the transition cleared
    game.watch_all(&chosen);
    MissionGame::idle();  // idle-looking later in its cutscenes
    game.request_mission(kPodColumn, kPodRow);
    game.watch_all(&chosen);
    check(g_transition_fn == g_routine_table[zap::kIdleRoutine] &&
              game.mission_status() == zap::kEndingStarted,
          "not offered during the ending");
    const char* refused = "refused RETURN HOME WITH THE DISKS? ending";
    game.note_play(1000);
    check(apply(game, "apply\npopup 36 FROM BOB") == refused &&
              apply(game, "apply\ncas 36 0 1") == refused && game.spawn_shield(5000) == refused &&
              game.kill(5000) == refused && g_deaths == 0 && game.queued_popups() == 0,
          "in the ending: grants, popups, shields and received deaths refused");
    g_save_loaded = 0xFF;  // MISSION COMPLETE: no save loaded
    game.watch_all(&chosen);
    g_save_loaded = 3;  // loaded again
    game.watch_all(&chosen);
    check(game.mission_status() == "not asked", "another load");
    check(apply(game, "apply\npopup 36 FROM BOB") == "applied", "given then");
    for (int i = 0; i < 30; ++i) game.pump_popups();
    message_showing(1);
    game.request_mission(kPodColumn, kPodRow);
    game.watch_all(&chosen);
    check(g_transition_fn == reinterpret_cast<void*>(&opener), "offered again");
    game.put_back_game(&chosen);
}

// The game's window over a message's text (0xA8210), as disassembled: glyph i in row i / w and
// column i % w, tested at x 8c and y 10r against a window centered on 4w and on half the rows' 8
// pixels each; a glyph inside is shown and cut at the top edge mod 8 (with no row offset;
// `repaired`: less 2r, as TextCut's patch) and at the bottom edge less 2r, mod 8, its pixel rows
// drawn `top` to `bottom`; a glyph outside is hidden, keeping its last cut. (The left and right
// cuts, and the icon glyph's 10 x 9 cut mod 9, are left out.)
struct GlyphCut {
    bool shown = false;
    int top = 0, bottom = 8;
};

void window_cut(std::vector<GlyphCut>& glyphs, int w, int wx, int wy, bool repaired) {
    const int count = static_cast<int>(glyphs.size());
    const int half_h = 8 * ((count - 1) / w + 1) / 2;
    const int left = 4 * w - wx, right = 4 * w + wx, top = half_h - wy, bottom = half_h + wy;
    for (int i = 0; i < count; ++i) {
        const int r = i / w, x = 8 * (i % w), y = 10 * r;
        GlyphCut& g = glyphs[i];
        if (x + 8 <= left || x >= right || y + 8 <= top || y >= bottom) {
            g.shown = false;
            continue;
        }
        g = GlyphCut{true, 0, 8};
        if (y + 8 > bottom) g.bottom = (bottom + 8 - 2 * r) % 8;
        if (y < top) g.top = (repaired ? top - 2 * r : top) % 8;
    }
}

// Whether, in a message of `rows` rows of `w` (its box as the game sizes it, or fitted as
// fit_notice_box does), some row shows more of itself on a frame while the box closes than the
// frame before, or less while it opens: a line flashing. The message's life as its update
// (0x181E70) runs it, one call a frame, from the message routine's own window call (0x182C89,
// the window empty): while frames left are under the half-height the window steps down, else
// while the box is under the half-height the box and the window step up, else every glyph is
// shown as last cut.
bool flashes(int rows, int w, bool fitted, bool repaired) {
    const int count = fitted ? (rows - 1) * w + 1 : rows * w;
    const int half = fitted ? 4 * (rows - 1) + 4 : 4 * (rows - 1) + 7;
    std::vector<GlyphCut> glyphs(static_cast<size_t>(count));
    std::vector<int> last(static_cast<size_t>(rows), 0);
    int wx = 0, wy = 0, box = 0;
    bool flash = false;
    window_cut(glyphs, w, wx, wy, repaired);
    for (int frames = 179; frames >= 0; --frames) {
        const bool closing = frames < half, opening = !closing && half > box;
        if (closing) {
            window_cut(glyphs, w, wx -= 100, wy -= 1, repaired);
        } else if (opening) {
            ++box;
            window_cut(glyphs, w, wx += 100, wy += 1, repaired);
        } else {
            for (GlyphCut& g : glyphs) g.shown = true;
        }
        for (int r = 0; r < rows; ++r) {  // each row's first glyph
            const GlyphCut& g = glyphs[static_cast<size_t>(r * w)];
            const int now = g.shown ? (std::max)(0, g.bottom - g.top) : 0;
            if ((closing && now > last[r]) || (opening && now < last[r])) flash = true;
            last[r] = now;
        }
    }
    return flash;
}

void test_text_cut() {
    // The game's own cut: from five rows, a line grows back while the box closes, as filmed in
    // game (2026-11-08) for the four-line reminder (five rows with its blank one, 23 wide). With
    // four, the edge reaches 16 inside row 1 only on the last frame, the window shut across.
    check(flashes(5, 23, true, false), "the game's cut: the four-line reminder flashes");
    check(flashes(5, 24, false, false), "the game's cut: a five-line popup flashes");
    check(!flashes(4, 24, false, false) && !flashes(4, 24, true, false),
          "the game's cut: four rows do not");
    check(!flashes(3, 24, false, false) && !flashes(2, 24, false, false),
          "the game's cut: two or three lines do not");
    // Repaired, no row of any height flashes.
    bool any = false;
    for (int rows = 1; rows <= 11; ++rows) {
        for (int w : {12, 23, 24}) {
            any = any || flashes(rows, w, false, true) || (rows >= 4 && flashes(rows, w, true, true));
        }
    }
    check(!any, "the repaired cut: no line flashes, at any height");

    // The patch: built from the game's bytes only, put back as it was.
    BYTE game[zap::kTextCutSize], out[zap::kTextCutSize] = {};
    std::memcpy(game, kTextCutGame, sizeof(game));
    check(zap::text_cut_patch(game, out), "the patch is built from the game's cut");
    const BYTE ours[] = {0x8B, 0xC5, 0x2B, 0xC6, 0x2B, 0xC6, 0x66, 0x44, 0x89, 0xA9, 0xA0, 0x01,
                         0x00, 0x00, 0x83, 0xE0, 0x07, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00};
    check(std::memcmp(out, ours, sizeof(ours)) == 0,
          "mov eax, ebp; sub eax, esi twice; the game's store; and eax, 7; a 7-byte nop");
    game[10] = 0x24;
    check(!zap::text_cut_patch(game, out), "other bytes are not patched");

    check_code_patch<zap::TextCut>("the text cut", kTextCutGame, ours);
    // Through the helper: patched while configured, put back with the game, and an unload refused
    // while it is still the helper's.
    {
        zap::Game game_;
        ready(game_);
        game_.watch_all(&chosen);
        check(std::memcmp(g_text_cut_code, ours, sizeof(ours)) == 0, "the helper patches it");
        check(game_.unload_blocker() != nullptr, "an unload waits for it");
        game_.put_back_game(&chosen);
        check(std::memcmp(g_text_cut_code, kTextCutGame, sizeof(kTextCutGame)) == 0 && game_.unload_blocker() == nullptr,
              "put back with the game");
    }
}

}  // namespace

int main() {
    test_text_cut();
    test_the_black_after_yes();
    test_own_popup_needs_a_new_last_message();
    test_popups_wait_for_the_mission_fade();
    test_a_parked_trap_popup_does_not_hold_the_mission();
    test_drops_wait_for_the_mission_fade();
    test_a_save_during_the_mission_fade();
    test_the_mission_request_gives_home();
    test_return_home_lapses_away_from_the_pod();
    test_map_marks_with_a_menu_open();
    test_pickup_list();
    test_popups_wait_for_their_save();
    test_a_refused_unload_keeps_what_the_client_asked();
    test_refused_unload_keeps_a_start();
    test_queued_popups();
    test_pause_row_from_the_seed_file();
    test_configure();
    test_apply();
    test_popups();
    test_pickup_popup_width();
    test_zexion_item_labels();
    test_lower_case_icons();
    test_zexion_items_show_as_themselves();
    test_flipped_items_show_as_themselves();
    test_the_marker_hidden();
    test_stand_ins_in_their_own_colors();
    test_colored_stand_ins_dressed_later();
    test_zexion_scrap_shown_as_itself();
    test_stand_ins_named_by_their_own_cell();
    test_received_stand_in_popup();
    test_received_disk_popup();
    test_received_intel_chip_popup();
    test_received_shield();
    test_received_scrap_drops();
    test_received_scrap_drops_held();
    test_received_death();
    test_received_traps();
    test_own_traps();
    test_a_trap_popup_takes_its_turn();
    test_traps_without_ailments();
    test_traps_held_for_the_ending();
    test_assist_watch();
    test_newgame_watch();
    test_randomizer_by_seed();
    test_archipelago_choice();
    test_scanner_line();
    test_start_table();
    return report("game tests");
}
