// The map in AP saves: item 48's squares drawn as important, and the cursor's name for
// stand-in squares.
//
// A square's marker is its map-grid value (+0x3E469D on B1), set when revealed; the game picks
// major/minor by item ID 3..35 where it writes it (0x7E980 and eleven other places), not by
// record, so item 48 shows as minor. mark_important raises those squares' class to major as the
// game would. The grid is saved, so nothing is put back (docs/re/memory.md, "Map markers").
// While an item is on screen the game writes its square again every frame (the write
// SeenMarker patches), which would undo the raise.
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>

#include "addresses.h"

namespace zap {

// The loaded randomizer section (game_layout.h, rando) and the map grid (54 x 54, index
// y * 54 + x).
// `ended`: flag 250 (the game does not obscure once the game has been finished). Returns how
// many squares changed.
int mark_important(const BYTE* section, BYTE* grid, bool ended);

// The randomizer's three Obscure settings, from the loaded section: Obscure Majors (t29),
// Obscure Minors (t38) and Obscure Class (t41). All off with no section.
struct Obscure {
    bool majors = false, minors = false, by_class = false;
};
Obscure obscure_settings(const BYTE* section);

// One square's value raised from the minor class to the major one, as the game would have set
// it for a major item under `obscure`; any other value unchanged. In a randomizer game the
// game's reveal (0x7EECE) obscures a major under Obscure Majors or Obscure Class, and a minor
// under Obscure Minors (Class sends a minor down the major's path), both only with flag 250
// clear and a reveal mode other than 3. So an obscured minor becomes an obscured major unless
// majors are shown, and a shown minor a shown major, except under Majors on with Minors and
// Class off: there a square a hint, a map room or Hints Revealed revealed (base 0xC) becomes an
// obscured major and one seen directly (base 0x18) a shown one (docs/re/memory.md, "Map
// markers").
uint8_t important_value(uint8_t value, Obscure obscure, bool ended);

// The item update's write of the square under an item on screen in a randomizer game (0x19E5A5
// to 0x19E5D7 on B1, in 0x19E2E0, each frame the item is in view): a revealed square (base 0xC
// or 0x18, not collected) gets base + 2 for IDs 3 to 35 and base + 1 for any other, so item 48
// went back to a minor every frame against mark_important's raise, and its minimap dot was seen
// to flash (assumed to be this: docs/re/memory.md, "Map markers").
constexpr size_t kSeenMarkerSize = 50;

// That write with ID 48 a major too, built from the game's bytes `game` (its store copied as it
// is): false if they are not the write as traced on B1. It still reads only the registers the
// game's does, leaves them as the game's does, and calls nothing.
bool seen_marker_patch(const BYTE (&game)[kSeenMarkerSize], BYTE (&out)[kSeenMarkerSize]);

// The write patched while the labels' seed's save is loaded, so a stand-in on screen gets a
// major's shown class as the game's own majors do (0x1A, 0x0E). Game thread only. The patch
// calls and reads nothing of the helper's, so one left behind cannot crash the game; but a plain
// randomizer game's Scrap Chunks would then show as majors on screen, so an unload refuses while
// it could not be put back.
class SeenMarker {
public:
    // Patched at `site` while `on` (null: nothing), put back while not.
    void watch(BYTE* site, bool on);
    void restore();
    bool patched() const { return site_ != nullptr; }
    // Why it could not be patched the last time it tried, or "".
    const std::string& error() const { return error_; }

private:
    bool reads_as_written() const;
    BYTE* site_ = nullptr;
    BYTE original_[kSeenMarkerSize] = {};
    BYTE written_[kSeenMarkerSize] = {};
    std::string error_;
};

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
