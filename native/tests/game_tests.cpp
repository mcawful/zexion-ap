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
    check(g_text == "\xC5MISSILE BOX FROM ALICE", "icon, the game's name, then the sender");
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
    check(g_messages == 2 && g_text == "\xC5MISSILE BOX FROM BOB", "then shows the next");

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
    g_steps.clear();
    g_transition_fn = nullptr;
    g_rando_marker = 0;
    g_slot_taken = false;
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

    zap::NewGamePlan plan;
    check(zap::parse_newgame("ap_newgame\nexpect 0x10 00\nwrite 0x310 110022\nmenu 0xCA 02",
                             plan)
              .empty(),
          "parses menu lines");
    game.arm_newgame(plan);
    game.watch_menu(&chosen);
    check(game.menu_installed() && panel.text() == "ARCHIPELAGO", "offers ARCHIPELAGO");
    press();
    check(g_randomizer_menu[0xCA] == 2, "the menu's state is set to the seed's mode");
    check(g_steps == "start placement queue ", "START GAME's steps, in order");
    check(panel.text() == "RANDOMIZER " && panel.callback() == &rando_menu_fn,
          "RANDOMIZER is back once the new game is queued");
    chosen(panel.child, 7);
    check(g_steps == "start placement queue ", "a second press does nothing");
    game.watch_menu(&chosen);
    check(!game.menu_installed(), "not offered again while the new game is queued");
    game.watch_newgame();
    check(g_section[0x310] == 0x11 && g_section[0x312] == 0x22,
          "the AP seed is written over the placement");
    check(game.newgame_status().rfind("applied", 0) == 0, "and reported");

    g_steps.clear();
    g_transition_fn = nullptr;
    g_slot_taken = true;
    game.arm_newgame(plan);
    game.watch_menu(&chosen);
    press();
    check(game.newgame_status().rfind("refused the game did not queue", 0) == 0,
          "a new game the game would not queue is reported");
    game.watch_menu(&chosen);
    check(panel.text() == "ARCHIPELAGO", "and ARCHIPELAGO is offered again");
    game.disarm_newgame();
    game.watch_menu(&chosen);
    check(!game.menu_installed() && panel.text() == "RANDOMIZER " &&
              panel.callback() == &rando_menu_fn,
          "disarming puts RANDOMIZER back");
    g_current_menu = nullptr;
    g_slot_taken = false;
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

int main() {
    test_parsers();
    test_configure();
    test_apply();
    test_popups();
    test_newgame_parser();
    test_newgame_watch();
    test_archipelago_choice();
    test_mailbox();
    if (g_failures == 0) std::printf("all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
