// An AP new game: the plan the client arms (ap_newgame), the seed's placement over the game's,
// and the RANDOMIZER route, which watches for the new game the player queued (native/README.md,
// "New games"). Shared by the Windows helper (Game, which adds the ARCHIPELAGO choice on the
// game-mode panel) and the Linux helper; the logic is the same on both builds, whose layouts
// these addresses agree on (docs/re/linux.md, "Mapped by disassembly: new games, events, the
// map, feats").
#pragma once

#include <cstdint>
#include <string>

#include "requests.h"  // NewGamePlan, SectionBytes, StartRoom, kSectionSize, kMenuSize

namespace zap {

// The game state the new-game logic reads and writes, each checked by the helper that sets it.
struct NewGameAddresses {
    uint8_t* rando_blob = nullptr;        // the loaded randomizer section, kSectionSize bytes
    uint8_t* randomizer_menu = nullptr;   // the randomizer menu's state, kMenuSize bytes
    uint8_t* transition_fn = nullptr;     // holds the routine the game queued
    uint8_t* transition_timer = nullptr;  // int32: frames until it runs
    uint8_t* newgame_fn = nullptr;        // the new-game routine
    uint8_t* placement_fn = nullptr;      // the game's placement, void()
    uint8_t* rando_marker = nullptr;      // uint16: 1 for a randomizer game
    uint8_t* current_slot = nullptr;      // int32, from 0
    uint8_t* start_count = nullptr;       // int16: rooms in the start table
    uint8_t* start_cells = nullptr;       // int16 x, int16 y each
    uint8_t* start_flags = nullptr;       // a byte each
};

// The offset of the first of `edits` that `block` does not hold, or -1.
int first_difference(const uint8_t* block, const std::vector<SectionBytes>& edits);
void write_edits(uint8_t* block, const std::vector<SectionBytes>& edits);
std::string hex_offset(unsigned offset);

class NewGames {
public:
    void arm(NewGamePlan plan);
    // The plan dropped; "armed" back to "idle" (an outcome stays).
    void disarm();
    const std::string& status() const { return status_; }
    bool armed() const { return armed_; }
    const NewGamePlan& plan() const { return plan_; }
    // ARCHIPELAGO was pressed: its placement is done, and the new game it queued is applied
    // without RANDOMIZER's checks.
    bool starting() const { return starting_; }
    void begin_start() { starting_ = true; }
    // "refused <why>", the plan dropped.
    void refuse(const std::string& why);

    // Whether the game has queued its new-game routine.
    static bool queued(const NewGameAddresses& a);

    // Game thread, each frame while armed: a queued randomizer new game is placed as the seed
    // (from RANDOMIZER: if it holds the seed's text) and gets the seed's edits; anything else
    // drops the plan with nothing written.
    void watch(const NewGameAddresses& a);

    // The seed's placement over the section: the start table checked (or filled for
    // ARCHIPELAGO), the menu set to the seed's settings and the plan written, the game's
    // placement run, the menu put back however it ends; then, if the section holds `expect`, the
    // plan written over it. "" when placed, else why not (the section then as the placement left
    // it: the caller puts back what it must).
    std::string place_seed(const NewGameAddresses& a);

private:
    std::string ensure_start_table(const NewGameAddresses& a);

    bool armed_ = false;
    NewGamePlan plan_;
    std::string status_ = "idle";
    bool starting_ = false;
};

}  // namespace zap
