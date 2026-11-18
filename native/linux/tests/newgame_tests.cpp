// native/src/newgame.cpp on Linux, against a stand-in game in this program: the RANDOMIZER route
// applying a seed to the new game the player queued, and refusing another seed's or a placement
// that is not the seed's. (The Windows build tests it through Game: native/tests/game_tests.cpp.)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/mman.h>

#include "game_mem.h"  // writable: the memory map, kept a moment
#include "newgame.h"
#include "requests.h"

using namespace zap;

namespace {

int g_failures = 0;

void check(bool ok, const char* what, const std::string& detail = "") {
    if (ok) return;
    std::printf("FAIL: %s %s\n", what, detail.c_str());
    ++g_failures;
}

// The stand-in game's state.
uint8_t g_section[kSectionSize];
uint8_t g_menu[kMenuSize];
void* g_transition = nullptr;
int32_t g_timer = 0;
uint16_t g_marker = 0;
int32_t g_slot = 0;
int16_t g_start_count = 0;
uint8_t g_start_cells[4 * kStartTableMax];
uint8_t g_start_flags[kStartTableMax];
uint8_t g_placed = 0x5A;  // what the stand-in placement writes at +0x26C
int g_placements = 0;

void new_game() {}  // the stand-in's new-game routine: only its address is compared
void placement() {
    ++g_placements;
    g_section[0x26C] = g_placed;  // the item table's first location, as the placement writes it
}

NewGameAddresses addresses() {
    NewGameAddresses a;
    a.rando_blob = g_section;
    a.randomizer_menu = g_menu;
    a.transition_fn = reinterpret_cast<uint8_t*>(&g_transition);
    a.transition_timer = reinterpret_cast<uint8_t*>(&g_timer);
    a.newgame_fn = reinterpret_cast<uint8_t*>(&new_game);
    a.placement_fn = reinterpret_cast<uint8_t*>(&placement);
    a.rando_marker = reinterpret_cast<uint8_t*>(&g_marker);
    a.current_slot = reinterpret_cast<uint8_t*>(&g_slot);
    a.start_count = reinterpret_cast<uint8_t*>(&g_start_count);
    a.start_cells = g_start_cells;
    a.start_flags = g_start_flags;
    return a;
}

// A plan as the client builds one: the seed's text at +0x658 to identify it, the menu set for
// the placement, the placement's first table entry expected, the AP table's first entry written.
NewGamePlan plan() {
    std::string request =
        "ap_newgame\n"
        "identify 0x658 4142434445464748\n"
        "menu 0x88 0300\n"
        "expect 0x26C 5A\n"
        "write 0x26C 2A\n";
    NewGamePlan out;
    std::string error = parse_newgame(request, out);
    check(error.empty(), "the plan parses", error);
    return out;
}

// The player's own new game, queued by START GAME with the seed's text typed in (or not).
void queue_randomizer_game(const char* text) {
    std::memset(g_section, 0, sizeof g_section);
    std::memcpy(g_section + 0x658, text, 8);
    g_section[0x26C] = 0x11;  // the player's placement
    std::memset(g_menu, 0x77, sizeof g_menu);
    g_transition = reinterpret_cast<void*>(&new_game);
    g_timer = 40;
    g_marker = 1;
    g_slot = 4;
    g_placements = 0;
    g_placed = 0x5A;
}

void test_the_seeds_text_gets_the_seed() {
    NewGames games;
    games.arm(plan());
    check(games.status() == "armed", "armed");
    queue_randomizer_game("ABCDEFGH");
    games.watch(addresses());
    check(games.status() == "applied slot 5", "applied to the slot, from 1", games.status());
    check(g_placements == 1, "the seed's placement ran once");
    check(g_section[0x26C] == 0x2A, "the AP table written over the placement");
    check(g_menu[0x88] == 0x77 && g_menu[0x89] == 0x77, "the player's menu put back");
    check(!games.armed(), "the plan used once");
}

void test_another_seeds_text_is_left_alone() {
    NewGames games;
    games.arm(plan());
    queue_randomizer_game("ZZZZZZZZ");
    games.watch(addresses());
    check(games.status().starts_with("refused the new game is not this seed"),
          "another seed's text refused", games.status());
    check(g_placements == 0 && g_section[0x26C] == 0x11, "nothing placed or written");
    check(!games.armed(), "the plan dropped");
}

void test_a_placement_not_the_seeds_puts_the_players_back() {
    NewGames games;
    games.arm(plan());
    queue_randomizer_game("ABCDEFGH");
    g_placed = 0x33;  // the game's placement came out otherwise
    games.watch(addresses());
    check(games.status().starts_with("refused the placement is not the seed's"),
          "a placement not the seed's refused", games.status());
    check(g_section[0x26C] == 0x11, "the player's own placement put back");
}

void test_nothing_happens_until_a_randomizer_game_is_queued() {
    NewGames games;
    games.arm(plan());
    queue_randomizer_game("ABCDEFGH");
    g_transition = nullptr;  // nothing queued
    games.watch(addresses());
    g_transition = reinterpret_cast<void*>(&new_game);
    g_marker = 0;  // a normal new game
    games.watch(addresses());
    g_marker = 1;
    g_timer = 1;  // too close to being built
    games.watch(addresses());
    check(games.status() == "armed" && g_placements == 0, "still armed, nothing placed");
    games.disarm();
    check(games.status() == "idle", "disarmed");
}

}  // namespace

// The memory map is kept a moment between reads (game_mem_linux.cpp), but never for a range
// unmapped since: that one is read afresh, and found not writable.
void test_a_range_unmapped_since_the_last_read_is_not_writable() {
    const size_t page = 4096;
    void* p = mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(p != MAP_FAILED, "a page mapped");
    auto* at = static_cast<BYTE*>(p);
    check(zap::writable(at, 16), "writable once mapped");
    check(zap::writable(at + page, 16), "and its second page, from the same read");
    munmap(at + page, page);
    check(!zap::writable(at + page, 16), "the unmapped page is not writable at once");
    check(zap::writable(at, 16), "the first still is");
    munmap(at, page);
    check(!zap::writable(at, 16), "nor the first once unmapped");
}

int main() {
    test_a_range_unmapped_since_the_last_read_is_not_writable();
    test_the_seeds_text_gets_the_seed();
    test_another_seeds_text_is_left_alone();
    test_a_placement_not_the_seeds_puts_the_players_back();
    test_nothing_happens_until_a_randomizer_game_is_queued();
    if (g_failures == 0) std::printf("newgame: all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
