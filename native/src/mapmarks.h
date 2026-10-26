// Important-item markers on the map for the progression stand-in (item 48, Scrap Chunk).
//
// Each map square's marker is its value in the map grid (+0x3E469D), set when the square is
// revealed; the game draws an item as important (major) or basic (minor) by a fixed range of
// item IDs, 3 to 35, tested where it writes the value (0x7E980 and eleven other places), not by
// the item's record (docs/re/memory.md, "Map markers"). This world places its own OS Install
// Disks and other players' progression items as item 48, outside that range, so the map showed
// them as basic. For an Archipelago save, the helper raises each such square's value from the
// minor class to the major one, as the game would have set it: below 0x24, value % 12 is the
// class, 1 a minor shown and 2 a major shown, 7 a minor obscured and 9 a major obscured (the
// Obscure settings); a collected item (0x1B) and an unrevealed square are left alone. The grid
// is saved, so the markers stay; nothing is put back.
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>

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

// The map screen (docs/re/memory.md, "Map markers"): the top menu is the map's when its first
// panel's first child holds the map's per-frame handler (0x169E50, or the helper's stand-in for
// it) at +0x178; the cursor's square is computed from the cursor widget (the panel's fourth
// child, +0x1E6 and +0x1E4) at the map's scale (zoomed in when it reads 40.0), as the map's
// handler does (read live: X 35 Y 28 on the map's readout from the cursor's (350, 223) zoomed
// in, 2026-10-25).
struct MapCursor {
    bool open = false;
    int cell = -1;  // y * 54 + x
};
MapCursor map_cursor(BYTE* current_menu, void* map_handler);

// A label (apitem.h: the popup's text, a glyph and up to two lines) as the map cursor's name: the
// font's characters only, on one line, at most kMapNameMax ("OS INSTALL DISK", "SLIDE MOD FOR
// BOB"); a longer one keeps its "FOR ..." part ("FOR ALICE").
constexpr size_t kMapNameMax = 17;  // the cursor's name text: 17 glyphs, cut (0xA61C0)
std::string map_name(const std::string& label);

// Whether the cursor's 17 name glyphs (their frames) spell a stand-in's game name, SCRAP
// ENERGY or SCRAP CHUNK, then blanks: what the map's handler writes for a square of item 47 or
// 48 shown or collected (`0xA61C0(text, record name, 0, 17)`).
bool shows_stand_in(const int32_t (&frames)[kMapNameMax]);

struct MapAddresses {
    BYTE* current_menu = nullptr;  // holds the top menu
    BYTE* map_handler = nullptr;   // holds the map's per-frame handler (0x169E50)
    void* text_set_fn = nullptr;   // (text, string, first glyph, glyphs)
    void* hook = nullptr;          // the helper's stand-in for the map's handler
};

// The map cursor's name for the stand-ins' squares. The map's handler, which the game calls each
// frame through the map menu's first child (+0x168, the child its only argument; the builder
// puts it in +0x178 too, and the helper wraps both), writes the
// name of the record of the item under the cursor into the cursor's 17-glyph text. While on (an
// Archipelago save's labels known), the helper points that slot at its stand-in, which calls the
// game's handler and then, where it wrote SCRAP ENERGY or SCRAP CHUNK for a labeled square,
// writes the label's map name over it with the game's text routine, before the frame is drawn.
// The records are not touched. Off, and before an unload, the game's handler goes back in a map
// still open; the helper refuses to unload while an open map holds its stand-in.
class MapCursorNames {
public:
    void watch(const MapAddresses& a, bool on);
    // The game's handler for `child`, if this wrapped it.
    void* original(void* child) const { return child == child_ ? original_ : nullptr; }
    void after(const MapAddresses& a, const std::map<int, std::string>& labels);
    void restore(const MapAddresses& a);
    bool holds_hook() const { return child_ != nullptr; }

private:
    // The wrapped child is still the open map menu's first child, a slot holding the stand-in.
    bool still_wrapped(const MapAddresses& a) const;
    BYTE* child_ = nullptr;   // the map menu's first child, its handler the helper's
    BYTE* menu_ = nullptr;    // the map's menu
    void* original_ = nullptr;
};

}  // namespace zap
