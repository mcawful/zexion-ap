// The ARCHIPELAGO choice on the file screen's game-mode panel (helper phase 3, step B).
//
// After the player picks an empty slot, the game shows NORMAL / RANDOMIZER. While armed, the
// helper finds that panel, rewrites RANDOMIZER's label to ARCHIPELAGO (same length, so its 11
// glyphs are just given other characters) and points the choice at the helper's callback; on
// disarm it puts RANDOMIZER back. Structure layout as traced on B1 (docs/re/memory.md, "The
// game-mode panel"): the most recent menu at `current_menu`, its panels at [menu+0], the
// game-mode panel 3; a panel's handler at +0x2F8, capacity at +0x1E8, children at +0x1E0; a
// child's callback at +0x2C0, handler at +0x2C8, label at +0x1F0; a label's glyph capacity at
// +0x1D8 and glyph pointers at +0x228; a glyph's frame count at +0x1BC, frame at +0x1C4, frame
// table at +0x1A8 and current frame at +0x1B0. Every pointer is read without risk of a fault
// and every expected value checked before anything is written.
#pragma once

#include <windows.h>

#include <cstdint>

namespace zap {

// Copies `size` bytes from `from` into `to`, or returns false if they cannot be read.
bool safe_read(const void* from, void* to, size_t size);

struct MenuAddresses {
    BYTE* current_menu = nullptr;  // holds a pointer to the most recent menu
    void* panel_handler = nullptr;
    void* widget_handler = nullptr;
    void* rando_menu = nullptr;  // RANDOMIZER's own callback
};

using WidgetCallback = void (*)(void* widget, int event);

constexpr int kLabelLength = 11;  // "RANDOMIZER" plus a blank, and "ARCHIPELAGO"

class ArchipelagoChoice {
public:
    // Game thread, each frame while armed. If the choice relabeled before is no longer on the
    // panel that is up, puts RANDOMIZER back on it. Then, if the game-mode panel is up with
    // RANDOMIZER on it, relabels it and points it at `callback`. Returns true when it
    // relabeled this frame.
    bool install(const MenuAddresses& addresses, WidgetCallback callback);

    // Game thread. Puts RANDOMIZER back on the choice this relabeled, whether or not its panel
    // is the one up, if that widget still holds `callback` and the widget handler; otherwise
    // (it is gone) just forgets it. Returns true when it put RANDOMIZER back.
    bool restore(const MenuAddresses& addresses, WidgetCallback callback);

    bool installed() const { return child_ != nullptr; }

private:
    BYTE* find_choice(const MenuAddresses& addresses, void* expected_callback) const;
    bool put_back(const MenuAddresses& addresses, WidgetCallback callback);

    BYTE* child_ = nullptr;
    int32_t frames_[kLabelLength] = {};
    BYTE* frame_ptrs_[kLabelLength] = {};
};

}  // namespace zap
