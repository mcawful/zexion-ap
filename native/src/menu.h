// The ASSIST lock in the options menu (native/README.md and docs/re/memory.md, "The assist
// menu"); the ARCHIPELAGO choice is choice.h's. A closed menu frees its widgets without clearing
// them, so the helper writes only into a widget whose menu is still on the open list and that is
// not released, and forgets it otherwise. Every read is fault-safe and checked before a write.
#pragma once

#include "platform.h"

#include <cstdint>

#include "addresses.h"
#include "choice.h"  // ArchipelagoChoice, WidgetCallback
#include "translation.h"  // BuiltMark

namespace zap {

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
