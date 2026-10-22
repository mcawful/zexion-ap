// The ARCHIPELAGO choice on the file screen's game-mode panel (helper phase 3, step B).
//
// After the player picks an empty slot, the game shows NORMAL / RANDOMIZER. While armed, the
// helper finds that panel, rewrites RANDOMIZER's label to ARCHIPELAGO (same length, so its 11
// glyphs are just given other characters) and points the choice at the helper's callback; on
// disarm it puts RANDOMIZER back. Structure layout as traced on B1 (docs/re/memory.md, "The
// game-mode panel"): the open menus form a list from the top one at `current_menu` down
// through [menu+0x78]; a menu's panels at [menu+0], the game-mode panel 3; a panel's handler
// at +0x2F8, capacity at +0x1E8, children at +0x1E0; a child's callback at +0x2C0, handler at
// +0x2C8, label at +0x1F0, released flag at +0x1C8; a label's glyph capacity at +0x1D8 and
// glyph pointers at +0x228; a glyph's released flag at +0x1C8, frame count at +0x1BC, frame
// at +0x1C4, frame table at +0x1A8 and current frame at +0x1B0.
//
// Closing a menu releases its widgets, labels and glyphs (their +0x1C8 set to 2, onto the
// game's dead list) and frees the menu and its panel list, and nothing in them is cleared
// first, so a widget's old contents prove nothing. The helper writes into the choice only
// while the menu it was found in is still on the list of open menus and the widget and its
// glyphs are not released, and otherwise forgets it without writing: the game builds every
// widget with its own callback, and closing a menu does not send events 6 or 7, so nothing
// calls the helper's callback from a freed widget. Every read is fault-safe and every
// expected value is checked before anything is written.
#pragma once

#include <windows.h>

#include <cstdint>

namespace zap {

// Copies `size` bytes from `from` into `to`, or returns false if they are not committed,
// readable memory (or reading them faults).
bool safe_read(const void* from, void* to, size_t size);

struct MenuAddresses {
    BYTE* current_menu = nullptr;  // holds a pointer to the top open menu
    void* panel_handler = nullptr;
    void* widget_handler = nullptr;
    void* rando_menu = nullptr;  // RANDOMIZER's own callback
};

using WidgetCallback = void (*)(void* widget, int event);

constexpr int kLabelLength = 11;  // "RANDOMIZER" plus a blank, and "ARCHIPELAGO"

class ArchipelagoChoice {
public:
    // Game thread, each frame while armed. If the choice relabeled before is no longer on the
    // top menu, puts RANDOMIZER back on it (if its menu is still open) or forgets it. Then, if
    // the top menu shows the game-mode panel with RANDOMIZER on it, relabels it and points it
    // at `callback`. Returns true when it relabeled this frame.
    bool install(const MenuAddresses& addresses, WidgetCallback callback);

    // Game thread. Puts RANDOMIZER back on the choice this relabeled, if its menu is still
    // open; otherwise (the menu was closed and freed) just forgets it. Returns true when it
    // wrote RANDOMIZER's callback back.
    bool restore(const MenuAddresses& addresses, WidgetCallback callback);

    bool installed() const { return child_ != nullptr; }

private:
    // The game-mode panel's choice 1 in `menu`, if it holds `expected_callback`.
    static BYTE* choice_in(BYTE* menu, const MenuAddresses& addresses, void* expected_callback);
    bool menu_open(const MenuAddresses& addresses) const;
    bool put_back(const MenuAddresses& addresses, WidgetCallback callback);

    BYTE* menu_ = nullptr;
    BYTE* child_ = nullptr;
    int32_t frames_[kLabelLength] = {};
    BYTE* frame_ptrs_[kLabelLength] = {};
};

}  // namespace zap
