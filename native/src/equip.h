// The disk count on the equipment screen (the OS Install Disks goal; docs/re/memory.md, "The
// equipment screen"). While on, EQUIPMENT's callback is the helper's wrapper: it calls the
// game's, then EquipmentScreen::add moves the Energy Tank left and adds a widget for the disk
// (the Data Disc's record, the count as its label) in a row the cursor reaches; the panel's
// handler then writes the disk's name and description on hover. The new children array comes
// from the game's malloc, and the game frees it with the menu. Every value is checked first.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "addresses.h"

namespace zap {

// The pause menu's EQUIPMENT entry, pointed at the helper's callback while `discs` is on (add
// itself refuses a count of 0).
class EquipmentHook {
public:
    // Game thread, each frame while `discs` is on: if the menu on top has EQUIPMENT with
    // the game's callback, points it at the hook; a hooked entry whose menu is no longer on top
    // gets the game's callback back (if its menu is still open). Returns true when it hooked
    // one this frame.
    bool install(const GameAddresses& addresses);
    // Game thread: the game's callback back on the entry this hooked, if its menu is still open
    // and the entry still holds the hook; otherwise just forgets it. Returns true when it wrote.
    bool restore(const GameAddresses& addresses);
    bool installed() const { return child_ != nullptr; }

private:
    // The EQUIPMENT widget in `menu` (its first panel's children) holding `callback`, or null.
    static BYTE* entry_in(BYTE* menu, const GameAddresses& addresses, void* callback);

    BYTE* menu_ = nullptr;
    BYTE* child_ = nullptr;
};

// The equipment screen with the disk added: the menu, the Energy Tank panel holding the panel
// hook, and the disk's widget.
class EquipmentScreen {
public:
    // Game thread, right after EQUIPMENT's callback has built the screen (the menu on top): with
    // `count` 1 or more, moves the Energy Tank and adds the disk. Returns "" or why not (nothing
    // is changed then, unless a step past the checks failed, which it names).
    std::string add(const GameAddresses& addresses, int count);

    // Game thread, from the panel hook after the game's handler: on the disk hovered or
    // selected (events 0 and 3), its name and description in the box below.
    void on_event(const GameAddresses& addresses, void* widget, intptr_t event);

    // Game thread: the game's handler back on the panel, if its menu is still open; otherwise
    // just forgets it (the menu freed it).
    void restore(const GameAddresses& addresses);
    // Game thread, each frame: forgets a screen whose menu has closed (the game freed it).
    void tidy(const GameAddresses& addresses);
    bool holds_hook() const { return panel_ != nullptr; }

private:
    BYTE* menu_ = nullptr;
    BYTE* panel_ = nullptr;
    BYTE* disk_ = nullptr;
};

// The characters of the disk's label, as the game's bomb count: a blank, then two digits.
std::string disk_count_text(int count);

// The name and description shown for the disk (the description box: 34 characters a line).
extern const char kDiskName[];
extern const char kDiskDescription[];

// The disk's popup icon, painted into font glyph 0xFB (empty in the game's art) while `discs`
// is on (apitem.h FontGlyph): the Data Disc's record has no icon.
constexpr uint8_t kDiskGlyph = 0xFB;
constexpr int kDiskItem = 44;
extern const char kDiskRows[8][9];

}  // namespace zap
