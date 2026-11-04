// The ARCHIPELAGO choice on the game-mode panel, and the ASSIST lock in the options menu
// (native/README.md, "New games" and "The assist menu"; docs/re/memory.md, "The game-mode panel"
// and "The assist menu"). A closed menu frees its widgets without clearing them, so the helper
// writes only into a widget whose menu is still on the open list and that is not released, and
// forgets it otherwise. Closing sends no event 6 or 7, so no freed widget calls the helper's
// callback. Every read is fault-safe and checked before a write.
#pragma once

#include <windows.h>

#include <cstdint>

#include "addresses.h"
#include "translation.h"  // BuiltMark

namespace zap {

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

// The options menu's ASSIST entry, locked (native/README.md and docs/re/memory.md, "The assist
// menu"): drawn like STYLE: AUTO with a padlock in its eighth glyph, its callback cleared (so
// nothing in the menu points into the helper), moved past the panel's selectable entries. Each
// frame finds the locked entry again (still_locked) before writing to it; unlocking puts all of
// it back, only into that entry.
constexpr int kAssistLength = 12;
// The padlock drawn after ASSIST while locked: font glyph 0xEF, empty in the game's art (the
// helper paints it, apitem.h FontGlyph), with a blank before it.
constexpr uint8_t kLockGlyph = 0xEF;
extern const char kLockRows[8][9];

class AssistLock {
public:
    // Game thread, each frame while locked. Returns true when it locked an entry this frame.
    bool install(const GameAddresses& addresses);
    // Game thread: unlocks the entry this locked if still_locked, else forgets it unwritten.
    // Returns true when it wrote the callback back.
    bool restore(const GameAddresses& addresses);
    bool installed() const { return child_ != nullptr; }

private:
    // The ASSIST widget in `menu` holding `expected_callback`, or null; with its panel and place.
    static BYTE* assist_in(BYTE* menu, const GameAddresses& addresses, void* expected_callback,
                           BYTE** panel = nullptr, int32_t* index = nullptr);
    // Moves `panel`'s entry `index` past its selectable entries, renumbering those that shift.
    void move_out(BYTE* panel, int32_t index);
    // Puts it back, if the panel is as move_out left it.
    void move_back();
    // Whether the widget this locked is, found afresh, the locked ASSIST of its open menu, in the
    // panel it was locked in. Never the kept pointer alone: a menu built again may put another
    // widget in its place. The lock writes only into a widget that passes.
    bool still_locked(const GameAddresses& addresses) const;
    // In a translation: the label's translated text the entry's (SmallText 137) with the padlock
    // after it, built again whenever it is not the helper's, the padlock left out where it would
    // pass the box's right edge; every glyph at style 0x15 and alpha 0.45, set again each call.
    void lock_translated(const GameAddresses& addresses, BYTE* label);
    // Its translated text the entry's again, if it is still the helper's.
    void unlock_translated(const GameAddresses& addresses, BYTE* label);

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
    BuiltLabel hd_;  // the helper's translated text, while it is (translation.h)
};

}  // namespace zap
