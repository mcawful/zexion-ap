// The ARCHIPELAGO choice on the game-mode panel, and the ASSIST lock in the options menu
// (docs/re/memory.md, "The game-mode panel").
//
// After an empty slot is picked the game shows NORMAL / RANDOMIZER. While armed, the helper
// relabels RANDOMIZER as ARCHIPELAGO (same 11 glyphs) and points its callback at the helper's.
// While installed, the panel's handler is `hooks.panel`, so `fit_border` can widen the hover
// border (sized for RANDOMIZER) in the same frame. Disarming puts both back.
//
// Closing a menu releases its widgets and frees the menu without clearing them, so a widget's
// old contents prove nothing. The helper writes only into a widget whose menu is still on the
// open list and that is not released; otherwise it forgets it. Closing sends no event 6 or 7,
// so no freed widget calls the helper's callback. Every read is fault-safe and checked before a
// write.
#pragma once

#include <windows.h>

#include <cstdint>

#include "addresses.h"

namespace zap {

using WidgetCallback = void (*)(void* widget, int event);

constexpr int kLabelLength = 11;  // "RANDOMIZER" plus a blank, and "ARCHIPELAGO"

class ArchipelagoChoice {
public:
    // Game thread, each frame while armed. If the choice relabeled before is no longer on the
    // top menu, puts RANDOMIZER back on it (if its menu is still open) or forgets it. Then, if
    // the top menu shows the game-mode panel with RANDOMIZER on it, relabels it and points it
    // at `callback`. Returns true when it relabeled this frame.
    bool install(const GameAddresses& addresses, WidgetCallback callback);

    // Game thread. Puts RANDOMIZER back on the choice this relabeled, if its menu is still
    // open; otherwise (the menu was closed and freed) just forgets it. Returns true when it
    // wrote RANDOMIZER's callback back.
    bool restore(const GameAddresses& addresses, WidgetCallback callback);

    // Game thread, once the choice has started a new game: puts RANDOMIZER's callback back but
    // leaves the label reading ARCHIPELAGO while the menu fades out, then forgets the choice
    // (the menu is about to close). Only into a menu still open, as restore. Returns true when
    // it wrote the callback back.
    bool release(const GameAddresses& addresses, WidgetCallback callback);

    bool installed() const { return child_ != nullptr; }
    // Whether a panel may still hold the helper's hooks.panel (until put back, or its menu
    // closed): the helper must not unload while it does.
    bool holds_hook() const { return hook_panel_ != nullptr; }

    // Game thread, each frame while installed: if the mouse is on ARCHIPELAGO and the panel has
    // just laid its border out around it, moves the border's right-hand pieces 8 pixels right
    // and stretches its top and bottom edges to meet them. Returns true when it moved them.
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
    BYTE* hook_menu_ = nullptr;   // the menu whose panel holds hooks.panel
    BYTE* hook_panel_ = nullptr;  // that panel
    int32_t frames_[kLabelLength] = {};
    BYTE* frame_ptrs_[kLabelLength] = {};
};

// The options menu's ASSIST entry, locked (docs/re/memory.md, "The assist menu"). While locked,
// the helper draws its label like STYLE: AUTO (style 5, palette 0, its grey tint; re-applied
// while its menu is up, as the menu changes palette after some screens) and puts a padlock in
// its eighth glyph. It clears the callback, which the generic widget handler then skips
// (0xD0270), so nothing in the menu points into the helper, and moves the entry past the
// panel's selectable ones (+0x1EC), so the cursor skips it. Unlocking puts all of it back,
// under the same rules as the ARCHIPELAGO choice (a 12-glyph label reading ASSIST and six
// blanks, 0x16285A).
constexpr int kAssistLength = 12;
// The padlock drawn after ASSIST while locked: font glyph 0xEF, empty in the game's art (the
// helper paints it, apitem.h FontGlyph), with a blank before it.
constexpr uint8_t kLockGlyph = 0xEF;
extern const char kLockRows[8][9];

class AssistLock {
public:
    // Game thread, each frame while locked. Returns true when it locked an entry this frame.
    bool install(const GameAddresses& addresses);
    // Game thread: unlocks the entry this locked, if its menu is still open; otherwise forgets
    // it. Returns true when it wrote the callback back.
    bool restore(const GameAddresses& addresses);
    bool installed() const { return child_ != nullptr; }

private:
    // The ASSIST widget in `menu` holding `expected_callback`, or nullptr; with its panel and its
    // place among the panel's children.
    static BYTE* assist_in(BYTE* menu, const GameAddresses& addresses, void* expected_callback,
                           BYTE** panel = nullptr, int32_t* index = nullptr);
    // Moves the entry at `index` of `panel` past the panel's selectable entries (the first
    // [panel+0x1EC] children are the ones the cursor reaches), renumbering those that shift.
    void move_out(BYTE* panel, int32_t index);
    // Puts it back, if the panel is as move_out left it.
    void move_back();
    // Whether the widget this locked is still a live child of a panel of its menu, its
    // callback clear: found by its pointer, so a label changed meanwhile does not hide it.
    bool still_locked(const GameAddresses& addresses) const;

    BYTE* menu_ = nullptr;
    BYTE* child_ = nullptr;
    BYTE* panel_ = nullptr;
    int32_t moved_from_ = -1;  // ASSIST's place before move_out; -1 if not moved
    int32_t selectable_ = 0;   // the panel's selectable count before move_out
    int32_t frames_[kAssistLength] = {};  // ASSIST's glyphs as they were
    int32_t styles_[kAssistLength] = {};
    void* palettes_[kAssistLength] = {};
    int16_t palette_numbers_[kAssistLength] = {};
    float tints_[kAssistLength][6] = {};
};

}  // namespace zap
