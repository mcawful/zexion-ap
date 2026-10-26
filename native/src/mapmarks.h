// The map in AP saves: item 48's squares drawn as important, and the cursor's name for
// stand-in squares.
//
// A square's marker is its map-grid value (+0x3E469D on B1), set when revealed; the game picks
// major/minor by item ID 3..35 where it writes it (0x7E980 and eleven other places), not by
// record, so item 48 shows as minor. mark_important raises those squares' class to major as the
// game would. The grid is saved, so nothing is put back (docs/re/memory.md, "Map markers").
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>

#include "addresses.h"

namespace zap {

constexpr uint8_t kProgressionStandIn = 48;

// The loaded randomizer section (its 154 location cells at +0, x then y, 16-bit each; its item
// table at +0x26C; its settings at +0x87B + t) and the map grid (54 x 54, index y * 54 + x).
// `ended`: flag 250 (the game does not obscure once the game has been finished). Returns how
// many squares changed.
int mark_important(const BYTE* section, BYTE* grid, bool ended);

// One square's value raised from the minor class to the major one, as the game would have set
// it for a major item; any other value unchanged. `obscure_majors`: setting 29.
uint8_t important_value(uint8_t value, bool obscure_majors, bool ended);

// A label (apitem.h: the popup's text, a glyph and up to two lines of `width` characters) as the
// map cursor's name: its first line only, what the item is and not whom it is for ("OS INSTALL
// DISK", "PROGRESSION ITEM"), the font's characters only, cut at kMapNameMax.
constexpr size_t kMapNameMax = 17;  // the cursor's name text: 17 glyphs, cut (0xA61C0)
std::string map_name(const std::string& label, int width);

// Whether the cursor's 17 name glyphs (their frames) spell a stand-in's game name, SCRAP
// ENERGY or SCRAP CHUNK, then blanks: what the map's handler writes for a square of item 47 or
// 48 shown or collected (`0xA61C0(text, record name, 0, 17)`).
bool cursor_shows_stand_in(const int32_t (&frames)[kMapNameMax]);

// The map cursor's name for labeled stand-in squares (docs/re/memory.md, "Map markers"). While
// on, both handler slots of the map menu's first child (+0x168, +0x178) that hold the game's
// handler point at the helper's wrapper, which calls the game's and then, where it wrote SCRAP
// ENERGY/CHUNK, writes the label's map name. Game thread only. Off, and before an unload, the
// game's handler goes back in a map still open; an unload refuses while an open map holds the
// wrapper. `hooks.map` null: nothing wrapped.
class MapCursorNames {
public:
    void watch(const GameAddresses& a, bool on);
    // The game's handler for `child`, if this wrapped it.
    void* original(void* child) const { return child == child_ ? original_ : nullptr; }
    void after(const GameAddresses& a, const std::map<int, std::string>& labels,
               const std::map<int, int>& widths);
    void restore(const GameAddresses& a);
    bool holds_hook() const { return child_ != nullptr; }

private:
    // The wrapped child is still the open map menu's first child, a slot holding the wrapper
    // (never true with no wrapper).
    bool still_wrapped(const GameAddresses& a) const;
    BYTE* child_ = nullptr;   // the map menu's first child, its handler the helper's
    BYTE* menu_ = nullptr;    // the map's menu
    void* original_ = nullptr;
};

}  // namespace zap
