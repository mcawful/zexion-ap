// The disk count on the equipment screen (the OS Install Disk goal, the game's Data Disc item).
//
// The pause menu's EQUIPMENT entry (its first panel's widget whose callback is 0x12BA90) builds
// a menu of seven panels once per opening (docs/re/memory.md, "The equipment screen"). Panel 3
// holds the Energy Tank: one widget, its icon at the panel's position plus (16, 16) and its
// count, a 3-glyph label, 16 pixels left of and below it. The missile and bomb panel below
// moves its two icons to 16 and 48 when both are owned; with one disk or more, the helper does
// the same on the Energy Tank's row: the Energy Tank to 16 (with its count), and a second
// widget for the disk at 48, made by the game's widget constructor with the Data Disc's record
// and the disk count as its label (" 03", as the bomb count). The panel then has two children
// in a row, as the missile panel does, so the cursor and the mouse reach the disk as any item
// (the game moves between panels by position, within one by index).
//
// While the screen shows the disk, the panel's handler is the helper's: it calls the game's,
// then, when the disk is hovered or selected, writes the disk's name and description into the
// description box in place of the Energy Tank's (the game picks panel 3's text by panel, not by
// item). The panel's widget array is replaced with one of two, from the game's own malloc, and
// the game frees it, the widget and its label when the menu closes, as its own.
//
// To build the screen this way the helper puts its own callback in place of EQUIPMENT's on the
// pause menu on top (as ARCHIPELAGO's in place of RANDOMIZER's, menu.h); it calls the game's,
// then EquipmentScreen::add, so the screen's first frame already shows the disk. Every value on
// the way is checked first; anything unexpected leaves the screen as the game built it.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace zap {

struct EquipAddresses {
    BYTE* current_menu = nullptr;      // holds the top menu
    void* widget_handler = nullptr;    // the generic widget handler, EQUIPMENT's
    void* equipment_fn = nullptr;      // EQUIPMENT's callback: builds the screen
    void* equip_panel_fn = nullptr;    // the equipment screen's panel handler
    void* widget_move_fn = nullptr;    // (widget, dx, dy, set): places a widget and its label
    void* text_place_fn = nullptr;     // (text, x, y): places a text object's glyphs
    void* widget_new_fn = nullptr;     // makes a widget (label, glyphs, panel, handler, ...)
    void* sprite_sheet_fn = nullptr;   // (object, template): gives it a sprite definition's frames
    void* panel_link_fn = nullptr;     // (panel): links its widgets into the menu's draw list
    void* text_set_fn = nullptr;       // (text, string, first glyph, glyphs)
    BYTE* malloc_slot = nullptr;       // the import slots of the C runtime's malloc and free
    BYTE* free_slot = nullptr;
    BYTE* sprite_templates = nullptr;  // holds the pointer to the sprite definitions' templates
    BYTE* item_records = nullptr;      // holds the pointer to the item records
    void* hook = nullptr;              // the helper's stand-in for EQUIPMENT's callback
    void* panel_hook = nullptr;        // the helper's stand-in for the Energy Tank panel's handler
};

// The pause menu's EQUIPMENT entry, pointed at the helper's callback while the count is shown.
class EquipmentHook {
public:
    // Game thread, each frame while the count is shown: if the menu on top has EQUIPMENT with
    // the game's callback, points it at the hook; a hooked entry whose menu is no longer on top
    // gets the game's callback back (if its menu is still open). Returns true when it hooked
    // one this frame.
    bool install(const EquipAddresses& addresses);
    // Game thread: the game's callback back on the entry this hooked, if its menu is still open
    // and the entry still holds the hook; otherwise just forgets it. Returns true when it wrote.
    bool restore(const EquipAddresses& addresses);
    bool installed() const { return child_ != nullptr; }

private:
    // The EQUIPMENT widget in `menu` (its first panel's children) holding `callback`, or null.
    static BYTE* entry_in(BYTE* menu, const EquipAddresses& addresses, void* callback);

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
    std::string add(const EquipAddresses& addresses, int count);

    // Game thread, from the panel hook after the game's handler: on the disk hovered or
    // selected (events 0 and 3), its name and description in the box below.
    void on_event(const EquipAddresses& addresses, void* widget, intptr_t event);

    // Game thread: the game's handler back on the panel, if its menu is still open; otherwise
    // just forgets it (the menu freed it).
    void restore(const EquipAddresses& addresses);
    // Game thread, each frame: forgets a screen whose menu has closed (the game freed it).
    void tidy(const EquipAddresses& addresses);
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

// The disk's popup icon. The game's Data Disc record has none (its icon byte is 0), so the
// helper paints one, a ring round a hub after the item's own art (the owner's pick), into font
// glyph 0xFB (empty in the game's art, as 0xEE and 0xEF), while the disk count is shown
// (apitem.h FontGlyph).
constexpr uint8_t kDiskGlyph = 0xFB;
constexpr int kDiskItem = 44;
extern const char kDiskRows[8][9];

}  // namespace zap
