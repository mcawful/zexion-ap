// The ARCHIPELAGO choice on the game-mode panel (native/README.md, "New games"; docs/re/memory.md,
// "The game-mode panel"), shared by the Windows helper and the Linux helper: RANDOMIZER relabeled
// ARCHIPELAGO and pointed at the helper's callback, its panel's hover border fitted, and what the
// callback does (start_archipelago). A closed menu frees its widgets without clearing them, so
// the helper writes only into a widget whose menu is still on the open list and that is not
// released, and forgets it otherwise. Closing sends no event 6 or 7, so no freed widget calls the
// helper's callback. Every read is fault-safe and checked before a write.
#pragma once

#include "platform.h"

#include <cstdint>
#include <string>

#include "addresses.h"
#include "newgame.h"

namespace zap {

// The game calls a menu entry's callback as (widget, event) on Windows and with no arguments on
// Linux (docs/re/linux.md, "Mapped by disassembly: menus and UI"): the helper's ignores them.
using WidgetCallback = void (*)(void* widget, int event);

constexpr int kLabelLength = 11;  // "RANDOMIZER" plus a blank, and "ARCHIPELAGO"

class ArchipelagoChoice {
public:
    // Game thread, each frame while armed. A choice relabeled before and no longer on the top
    // menu is restored. Then, if the top menu shows the game-mode panel with RANDOMIZER on it,
    // relabels it ARCHIPELAGO, points it at `callback` and gives the panel `hooks.panel` (for
    // fit_border). Returns true when it relabeled this frame.
    bool install(const GameAddresses& addresses, WidgetCallback callback);
    // Game thread. Puts RANDOMIZER and the panel's handler back if the menu is still open; a
    // closed menu's are just forgotten. Returns true when it wrote RANDOMIZER's callback back.
    bool restore(const GameAddresses& addresses, WidgetCallback callback);
    // Game thread, once the choice has started a new game: as restore, but the label is left
    // reading ARCHIPELAGO while the menu fades out. Returns true when it wrote the callback back.
    bool release(const GameAddresses& addresses, WidgetCallback callback);
    bool installed() const { return child_ != nullptr; }
    // Whether a panel may still hold the helper's hooks.panel (until put back, or its menu
    // closed): the helper must not unload while it does.
    bool holds_hook() const { return hook_panel_ != nullptr; }
    // Game thread, each frame while installed: if the panel has just laid its hover border out
    // around ARCHIPELAGO, widens it by 8 pixels. Returns true when it moved it.
    bool fit_border(const GameAddresses& addresses);

private:
    // The game-mode panel's choice 1 in `menu`, if it holds `expected_callback`.
    static BYTE* choice_in(BYTE* menu, const GameAddresses& addresses, void* expected_callback);
    bool menu_open(const GameAddresses& addresses) const;
    // Puts the game's handler back on the panel given the hook, whatever became of the
    // choice, or forgets the panel once its menu is closed (freed with it).
    void put_handler_back(const GameAddresses& addresses);
    bool put_back(const GameAddresses& addresses, WidgetCallback callback);

    BYTE* menu_ = nullptr;
    BYTE* child_ = nullptr;
    WidgetCallback callback_ = nullptr;  // the helper's, given to child_ by install
    BYTE* hook_menu_ = nullptr;   // the menu whose panel holds hooks.panel
    BYTE* hook_panel_ = nullptr;  // that panel
    int32_t frames_[kLabelLength] = {};
};

// Game thread, from ARCHIPELAGO's callback (`callback`, the choice's): the seed's placement
// (games.place_seed), then the new game queued as the play-anyway path does (rando_newgame_fn),
// reported by games.watch next frame. Anything off: "refused <why>", RANDOMIZER back, the plan
// dropped, nothing queued. Once a press; nothing after the plan was dropped or while a new game
// is queued.
void start_archipelago(NewGames& games, ArchipelagoChoice& choice, const GameAddresses& addresses,
                       const NewGameAddresses& newgame, WidgetCallback callback);

}  // namespace zap
