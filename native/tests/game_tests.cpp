// Tests for Game against a stand-in game made of this program's own data and functions, so
// configure's checks and every call are real.

#include <windows.h>

#include <cstring>
#include <map>
#include <string>

#include "check.h"
#include "game_calls.h"
#include "stand_in.h"
#include "stand_in_menu.h"

namespace {

using namespace stand_in;

std::string apply(zap::Game& game, const std::string& request) {
    zap::Batch batch;
    std::string error = zap::parse_batch(request, batch);
    return error.empty() ? game.apply(batch) : "parse: " + error;
}

// Configured as a client configures it, and the stand-in game as a save in play leaves it.
void ready(zap::Game& game) {
    check(configure(game).empty(), "configures");
    reset_game();
}

// The player in map cell (column, row).
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
    // The game reports the popup still showing: the next waits.
    message_showing(100);
    for (int i = 0; i < 50; ++i) game.pump_popups();
    check(g_messages == 1 && game.queued_popups() == 1, "waits while the last popup shows");
    message_showing(1);
    game.pump_popups();
    check(g_messages == 2 &&
              g_text == "\xC5MISSILE BOX  FROM BOB",
          "then shows the next");

    // A notice: its text alone, left-aligned and wrapped at 24, with the item's sound.
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
    check(get<int32_t>(g_message, 0x1FC) == 4 * 1 + 7 && get<double>(g_message, 0x48) == 0x58 - 1,
          "a two-line notice keeps the game's box");

    // Three lines and the blank one: the box fitted to the three.
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

    // A notice past the 256-byte message slot: cut to the whole lines that fit (here 10 lines
    // of 24), then the blank line.
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

void test_newgame_watch() {
    zap::Game game;
    ready(game);
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

// A new game the player started from RANDOMIZER, with a plan that has menu lines: identified by
// its seed text, then placed again as the seed (the menu set to its settings), as ARCHIPELAGO
// places it, and the plan written over it.
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

    // Another seed's text: left as the game made it.
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

    // Double Teleport: the game's own start table is used, not the plan's; the second start
    // must be one of its rooms.
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
    // `plan` armed, ARCHIPELAGO offered, and pressed.
    void start(const zap::NewGamePlan& plan) {
        game.arm_newgame(plan);
        game.watch_menu(&chosen);
        press();
    }
    // The file screen visited again: nothing queued, the label built afresh.
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

    zap::NewGamePlan without_menu;
    zap::parse_newgame("ap_newgame\nwrite 0x310 110022", without_menu);
    game.arm_newgame(without_menu);
    game.watch_menu(&chosen);
    check(!game.menu_installed() && panel.text() == "RANDOMIZER ",
          "a plan without menu lines offers no ARCHIPELAGO");
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

    // The file screen closes as the new game starts; a later visit builds the panel afresh.
    // Another mode: the placement's amounts are not the seed's. Nothing is queued.
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

    // A slot the game will not start a new game in.
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
        return !zap::parse_newgame(std::string("ap_newgame\nwrite 0x10 00\nstart_table ") + table,
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

    // The menu never opened: the table is built as it would build it, before the placement.
    screen.start(plan);
    check(g_start_count == 2 && g_start_cells[0] == 1 && g_start_cells[1] == 25 &&
              g_start_cells[2] == 6 && g_start_cells[3] == 41 && g_start_flags[0] == 1 &&
              g_start_flags[1] == 0,
          "an empty start table is filled in");
    check(g_steps == "placement queue ", "and the new game started");
    game.watch_newgame();

    // The menu built it already, the same: accepted as it is.
    screen.visit_again();
    screen.start(plan);
    check(g_steps == "placement queue ", "a matching table: the new game started");
    game.watch_newgame();

    // A table that differs (another build's rooms, say): refused before anything is placed.
    screen.visit_again();
    g_start_flags[1] = 1;
    screen.start(plan);
    check(g_steps.empty(), "a differing table: nothing placed or queued");
    check(game.newgame_status().rfind("refused the game's start table differs", 0) == 0,
          "and reported");
    check(screen.panel.text() == "RANDOMIZER ", "RANDOMIZER is back");

    // A second start outside the table: refused before anything is written.
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

    // Another text in the entry (another language, say): left alone.
    std::memcpy(text_12(), "ENERGIESCHLUESSEL!\0\0\0", 21);
    game.set_discs(0, 15, "COLLECT 15 DATA DISCS!");
    for (int i = 0; i < 31; ++i) game.watch_discs();
    check(text(text_12()) == "ENERGIESCHLUESSEL!" && !game.scan_written(), "another text untouched");

    // The game loads its texts again into a new table: the new one is written; restore puts
    // back only the table it wrote, and only while it still reads as written.
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
    check(!parses("discs 3 15 636f6c6c656374"), "lower case is not the game's font");
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

    // Code that is not the traced instruction is left alone.
    set_code_byte(g_popup_width_code, 0, 0xB9);  // mov ecx, not mov edx
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
              own[2 * 54 + 8].item == 36 && own[2 * 54 + 8].item_name == "OS INSTALL DISK" &&
              own[2 * 54 + 8].who.empty(),
          "an own item's label: the item and its name");
    game.set_labels(own);
    player_cell(8, 2);
    game.watch_ap_items();
    check(record_name(47) == "\xC5OS INSTALL DISK",
          "named as the client says, after the item's icon");
    // An OS Install Disk (item 44, whose record has no icon): the helper's disk glyph.
    std::map<int, zap::Label> disk;
    zap::parse_labels("ap_labels\nlabel 8 2 " + hex("@44 os install disk"), disk);
    game.set_labels(disk);
    game.watch_ap_items();
    check(record_name(47) == "\xFBOS INSTALL DISK", "a disk: the disk glyph, not the logo");
    std::map<int, zap::Label> bad;
    zap::parse_labels("ap_labels\nlabel 8 2 40333658", bad);  // "@36X": not an item label
    check(bad[2 * 54 + 8].item == -1 && bad[2 * 54 + 8].item_name.empty(), "@36X is text");
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
    put(pool_slot(6), 0x1A8, odd);
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
    put(pool_slot(3), 0x1A8, static_cast<BYTE*>(g_frames));
    game.watch_ap_items();
    check(object_base(3) == g_frames + (42 - 95) * 0x38, "a base the game set back is redressed");

    // Scrap Chunk's record is no longer ours (left as another helper left it, say): its objects
    // are put back and not dressed, since the frame the game computes is no longer the logo's.
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

    // A dressed object whose slot was reused by another spawn is not "put back" on restore.
    game.set_labels(labels);
    game.watch_ap_items();
    put_object(3, 90, 1, 9, 9);  // now a Missile Container somewhere else
    BYTE* moved = g_frames + 5 * 0x38;
    put(pool_slot(3), 0x1A8, moved);
    game.restore_ap_items();
    check(object_base(3) == moved, "a reused slot is left alone on restore");
}

void shield_records(int16_t object) {
    reset_events();
    BYTE* ev = g_events + 3553 * 0x28;
    ev[0] = 11;
    put<int16_t>(ev, 2, 1);
    put<int16_t>(ev, 4, object);
    put<int16_t>(ev, 0x24, 3553);
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
    // The player in a cell with no label, at (1000, 600).
    player_cell(9, 3);
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

    // An object off the map, or in a cell with no label, is not taken.
    place(pool_slot(3), -5, 600);
    game.watch_ap_items();
    check(record_name(48).find("ZEXION2") != std::string::npos, "a position off the map is not used");
    put_object(3, 79, 48, 7, 2);
    place(pool_slot(3), 2990, 600);
    game.watch_ap_items();
    check(record_name(48).find("ZEXION2") != std::string::npos, "nor a cell with no label");

    // A stand-in shown by a general object (found by the scan every 31st frame) counts too.
    put_flipped(12, 501, 63, 5, 2);
    place(general_slot(12), 2995, 600);
    for (int i = 0; i < 32; ++i) game.watch_ap_items();  // through the next scan
    check(record_name(47).find("FINDER") != std::string::npos,
          "a general object's stand-in is named for its own cell");

    // Out of reach of every stand-in, a labeled cell of the player's own comes first.
    player_cell(5, 2);  // FINDER
    field<double>(0x1100) = 1600;
    game.watch_ap_items();
    check(record_name(48).find("FINDER") != std::string::npos,
          "a stand-in out of reach does not beat the player's labeled cell");
    check(game.popup_width() == 10, "and the popup is that cell's width");

    // A general object no longer showing a logo is not taken, even before the next scan.
    put_flipped(12, 501, 40, 5, 2);
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
    check(g_spawn_calls == 1, "nothing else spawned");
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

// The map's important squares are raised in the labels' seed's save with a menu open too (the
// player may pause and open the map at once), once two looks in a row (every 16th frame) have
// read that seed loaded, and not in another seed's save. The on-screen marker write is the
// helper's from the raise on, and the game's again in the first frame another save is loaded.
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
    // Another save for one frame call between two passes: counted afresh all the same.
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
    game.put_back_all(&chosen);
    check(seen_marker_games() && game.unload_blocker() == nullptr,
          "a client going away: the game's write");
    for (int i = 0; i < 32; ++i) game.watch_all(&chosen);
    check(seen_marker_games(), "and no labels: not patched again");
    g_menu = 0;
    g_rando_marker = 0;
    g_map_grid[4 * 54 + 3] = 0;
}

// A refused unload: put_back_all drops what clients set, and rearm sets all of it again, since
// the client does not know to resend it.
void test_refused_unload_rearms() {
    zap::Game game;
    ready(game);
    reset_sprites();
    reset_texts();
    reset_events();
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
    game.request_mission(kPodColumn, kPodRow);

    zap::ClientState saved = game.snapshot();
    game.put_back_all(&chosen);
    check(game.newgame_status() == "idle" && !game.assists_locked() && !game.intel_on() &&
              game.scan_line().empty() && game.files().counts().empty() &&
              game.label_names().empty() && game.mission_status() == "not asked",
          "put_back_all drops what the client set");
    check(game.snapshot().labels.empty(), "the labels too");

    game.rearm(saved);
    zap::ClientState again = game.snapshot();
    check(game.newgame_status() == "armed" && again.newgame.write.size() == 1 &&
              again.newgame.menu.size() == 1,
          "the new-game plan armed again, its menu lines too (ARCHIPELAGO offered again)");
    check(game.assists_locked() && game.intel_on(), "ASSIST locked and INTEL CHIP again");
    check(game.disc_count() == 3 && game.discs_required() == 15 && game.discs_told() &&
              game.scan_line() == "COLLECT 15 OS INSTALL DISKS!",
          "the disk count and the scanner's line again");
    check(game.files().counts() == std::map<int, int>{{2, 3}, {5, 3}}, "the file select's counts");
    check(game.label_names().count(4 * 54 + 3) == 1 && again.labels.size() == 1 &&
              saved.labels_seed && again.labels_seed &&
              zap::offline_matches(*again.labels_seed, *saved.labels_seed),
          "the labels again, for the seed they were set for (the map names follow them)");
    check(game.mission_status() == "asked", "RETURN HOME WITH THE DISKS? asked for again");
    game.put_back_all(&chosen);
    g_rando_marker = 0;
}

// A refused unload after ARCHIPELAGO's new game was queued: rearm keeps it started, so the next
// frame applies the plan without the mode check and ARCHIPELAGO is not offered again.
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

    zap::ClientState saved = game.snapshot();
    check(saved.newgame_armed && saved.starting, "the snapshot holds the start");
    game.put_back_all(&chosen);
    check(game.newgame_status() == "idle", "put_back_all drops it");
    game.rearm(saved);
    game.watch_menu(&chosen);
    check(!game.menu_installed(), "set again as started: ARCHIPELAGO not offered again");
    game.watch_newgame();
    check(game.newgame_status() == "applied slot 3" && g_section[0x6C0] == 5,
          "the queued new game gets the plan, the mode check skipped as before");
}

// A known disk seed's save loaded before the client has set its count: the pause row is
// prepared from the seed's file (its count with the map as it is), so a pause screen opened at
// once shows it; a save of no known seed gets no row.
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
    game.put_back_all(&chosen);
    check(!game.keys_branch_patched(), "put back");

    g_section[0x26D] ^= 1;  // another seed's save
    stand_in::PauseScreen q(reinterpret_cast<void*>(&equipment_fn), nullptr,
                            reinterpret_cast<void*>(&widget_handler_fn));
    g_current_menu = q.menu;
    game.watch_all(&chosen);
    check(!game.keys_branch_patched() && q.frame(q.glyph[0]) == 'K', "no known seed: KEYS");
    g_section[0x26D] ^= 1;
    game.put_back_all(&chosen);
    g_current_menu = nullptr;
    g_template_list[1] = nullptr;
    g_map_grid[4 * 54 + 3] = 0;
    g_rando_marker = 0;
}

// The popups queued, the client's and the helper's own notices; a client going away leaves its
// popups to show (their items are given) and takes its other-seed notice with it.
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
    // A client of another seed: the first's popups still held for their own save, and shown
    // there (the client counts them handed over and never sends them again).
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

// RETURN HOME WITH THE DISKS? (mission.h) through the Game: configured, the opener hooked, the
// routine table's idle, prompt and ending routines stand-ins of their own, idle in play; all put
// back after. Bodies of their own: the linker folds identical functions into one address.
int g_routine_runs = 0;
void idle_routine() { g_routine_runs += 1; }
void prompt_routine() { g_routine_runs += 2; }  // builds nothing: the opener then reports it
void opener() { g_routine_runs += 3; }  // the helper's (dllmain.cpp); not run here
void ending_routine() { g_routine_runs += 4; }
struct MissionGame {
    zap::Game game;
    zap::Hooks hooks;
    MissionGame() {
        ready(game);
        reset_events();
        std::memset(g_section, 0, sizeof(g_section));
        player_cell(13, 16);  // at the pod
        hooks.mission_open = reinterpret_cast<void*>(&opener);
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

    // Idle in play: the idle routine, no transition pending.
    static void idle() {
        g_transition_fn = g_routine_table[zap::kIdleRoutine];
        g_transition_timer = 0;
    }
    // All of it put back, then the events fresh, idle again and the hooks set again.
    void again() {
        game.put_back_all(&chosen);
        reset_events();
        idle();
        game.set_hooks(hooks);
    }
};

// Offered once idle: event 3667's fade with event 3666's delay, popups waiting until the game
// runs the opener; not before the game's last message is down to its last kFadeMessageLeft
// frames (GAME SAVED's, half a second in).
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

    // The helper's own popup the last message: never cut short, the fade waits for its end.
    m.again();
    reset_game();
    player_cell(13, 16);  // the player state cleared: back at the pod
    game.request_mission(kPodColumn, kPodRow);
    apply(game, "apply\npopup 36 FROM BOB");
    game.pump_popups();
    check(g_messages == 1, "the helper's popup shows");
    for (int i = 0; i < 30; ++i) game.pump_popups();  // past the gap between popups
    message_showing(zap::kFadeMessageLeft);
    game.watch_all(&chosen);
    check(g_fade_calls == 0, "not with kFadeMessageLeft frames of the helper's popup left");
    message_showing(1);
    game.watch_all(&chosen);
    check(g_fade_calls == 1, "once it has ended");

    // The player dead when the game runs the opener: no prompt, the fade turned back (toward the
    // normal level, a step every 6 frames), asked again.
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
    game.put_back_all(&chosen);
}

// A save at the save point during the fade sets the pause opener back in the opener's place:
// through the Game (the wiring watch_mission gives the prompt), the fade turned back and the
// prompt asked again; under a door's own fade, nothing turned back.
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
        game.put_back_all(&chosen);
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

// Asked for with home the Landing Pod's save point, in a seed that starts elsewhere: dropped at
// the seed's start, which is not home; offered at the pod; held by popups, dropped once the
// player is in play outside the pod's cell, before it opens; a death's own fade left alone when
// the opener is put off; and after YES, the ending waits for the game's last message to end,
// and an unload refuses until it has started.
void test_return_home_lapses_away_from_the_pod() {
    MissionGame m;
    zap::Game& game = m.game;
    g_rando_marker = 1;
    // The start record: candidate 17 (Open Cave), tile column 333 and row 449, so cell row 22,
    // column 23. The helper does not read it: home is what the request gives.
    put<uint16_t>(g_section, 0x310, 17);
    put<uint16_t>(g_section, 0x312, 333);
    put<uint16_t>(g_section, 0x314, 449);

    // A home off the map: refused, nothing asked.
    check(!game.request_mission(-1, kPodRow) && !game.request_mission(kPodColumn, 1080) &&
              game.mission_status() == "not asked",
          "a home off the map refused");

    // At the seed's start: not home, so the request lapses and nothing opens.
    player_cell(23, 22);
    message_showing(1);
    game.request_mission(kPodColumn, kPodRow);
    game.watch_all(&chosen);
    check(g_fade_calls == 0 && game.mission_status() == "lapsed: the player left the landing pod",
          "at the seed's start, not the pod: lapsed");

    // At the pod: offered.
    player_cell(13, 16);
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

    // YES: the ending waits for the game's last message to end, not just its last
    // kFadeMessageLeft frames.
    m.again();
    check(!game.ending_pending() && game.unload_refusal() == nullptr, "nothing pending");
    g_menu = 2;
    check(game.unload_refusal() == zap::kMenuOpenRefusal, "a menu open: an unload refuses");
    g_menu = 0;
    game.mission_yes();
    check(game.ending_pending() && game.unload_refusal() == zap::kEndingRefusal,
          "YES: the ending pending, and an unload refuses");
    message_showing(zap::kFadeMessageLeft);
    game.watch_all(&chosen);
    check(g_transition_fn == g_routine_table[zap::kIdleRoutine] &&
              game.mission_status() == "yes: ending when idle",
          "not with kFadeMessageLeft frames of a message left");
    message_showing(1);
    game.watch_all(&chosen);
    check(g_transition_fn == g_routine_table[zap::kEndingRoutine] &&
              game.mission_status() == "ending started",
          "once it has ended");
    check(!game.ending_pending() && game.unload_refusal() == nullptr,
          "started, with no ending patch: nothing an unload would undo");
    game.put_back_all(&chosen);
}

}  // namespace

int main() {
    test_popups_wait_for_the_mission_fade();
    test_a_save_during_the_mission_fade();
    test_the_mission_request_gives_home();
    test_return_home_lapses_away_from_the_pod();
    test_map_marks_with_a_menu_open();
    test_popups_wait_for_their_save();
    test_refused_unload_rearms();
    test_refused_unload_keeps_a_start();
    test_queued_popups();
    test_pause_row_from_the_seed_file();
    test_configure();
    test_apply();
    test_popups();
    test_pickup_popup_width();
    test_zexion_item_labels();
    test_zexion_items_show_as_themselves();
    test_flipped_items_show_as_themselves();
    test_stand_ins_named_by_their_own_cell();
    test_received_shield();
    test_received_death();
    test_assist_watch();
    test_newgame_watch();
    test_randomizer_by_seed();
    test_archipelago_choice();
    test_scanner_line();
    test_start_table();
    return report("game tests");
}
