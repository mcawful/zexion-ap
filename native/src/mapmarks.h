// The map in AP saves: item 48's squares drawn as important, and the cursor's name for stand-in
// squares (docs/re/memory.md, "Map markers"; native/README.md, "The map"). The game picks a
// square's class by item ID where it writes it (0x7E980 and eleven other places), so item 48
// shows as minor; mark_important raises it as the game would. The grid is saved, so nothing is
// put back.
#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "addresses.h"

namespace zap {

// The loaded randomizer section (game_layout.h, rando) and the map grid (54 x 54, index
// y * 54 + x); `ended`: flag 250 (no obscuring once finished). Returns the squares changed.
int mark_important(const BYTE* section, BYTE* grid, bool ended);

// The randomizer's three Obscure settings, from the loaded section: Obscure Majors (t29),
// Obscure Minors (t38) and Obscure Class (t41). All off with no section.
struct Obscure {
    bool majors = false, minors = false, by_class = false;
};
Obscure obscure_settings(const BYTE* section);

// One square's value raised from the minor class to the major one, as the game would have set
// it for a major item under `obscure`; any other value unchanged.
uint8_t important_value(uint8_t value, Obscure obscure, bool ended);

// The item update's write of the square under an item on screen in a randomizer game (0x19E5A5
// on B1, each frame the item is in view), which would undo mark_important's raise of item 48.
constexpr size_t kSeenMarkerSize = 50;

// That write with ID 48 a major too, built from the game's bytes `game` (its store copied as is):
// false if they are not the write as traced on B1. It reads only the registers the game's does,
// leaves them as the game's does, and calls nothing.
bool seen_marker_patch(const BYTE (&game)[kSeenMarkerSize], BYTE (&out)[kSeenMarkerSize]);

// The write patched while the labels' seed's save is loaded. Game thread only. The patch uses
// nothing of the helper's, so one left behind cannot crash the game; but a plain randomizer
// game's Scrap Chunks would show as majors, so an unload refuses while it is not put back.
class SeenMarker {
public:
    // Patched at `site` while `on` (null: nothing), put back while not.
    void watch(BYTE* site, bool on);
    void restore();
    bool patched() const { return site_ != nullptr; }
    // Why it could not be patched the last time it tried, or "".
    const std::string& error() const { return error_; }

private:
    BYTE* site_ = nullptr;
    BYTE original_[kSeenMarkerSize] = {};
    BYTE written_[kSeenMarkerSize] = {};
    std::string error_;
};

// A label (apitem.h: a glyph and up to two lines of `width` characters) as the map cursor's
// name: its first line only (what the item is), the font's characters only, cut at kMapNameMax.
constexpr size_t kMapNameMax = 17;  // the cursor's name text: 17 glyphs, cut (0xA61C0)
std::string map_name(const std::string& label, int width);

// Whether the cursor's 17 name glyphs (their frames) spell SCRAP ENERGY or SCRAP CHUNK, then
// blanks: what the map's handler writes for a square of item 47 or 48 shown or collected.
bool cursor_shows_stand_in(const int32_t (&frames)[kMapNameMax]);

// The map cursor's name for labeled stand-in squares. While on, the map menu's first child's
// handler slots point at the helper's wrapper, which calls the game's and then, where it wrote
// SCRAP ENERGY/CHUNK, writes the label's map name. Game thread only. Off, and before an unload,
// the game's handler goes back in a map still open; an unload refuses while an open map holds
// the wrapper. `hooks.map` null: nothing wrapped.
class MapCursorNames {
public:
    void watch(const GameAddresses& a, bool on);
    // The game's handler for `child`, if this wrapped it.
    void* original(void* child) const { return child == child_ ? original_ : nullptr; }
    // In a translation the cursor's name is `hd_name(cell)` if given and not empty, else the
    // label's first line (translation.h hd_label) without its icon.
    void after(const GameAddresses& a, const std::map<int, std::string>& labels,
               const std::map<int, int>& widths,
               const std::function<std::wstring(int)>& hd_name = {});
    void restore(const GameAddresses& a);
    bool holds_hook() const { return child_ != nullptr; }
    // Whether the map is the top menu, its first child holding the game's handler or the wrapper.
    static bool open(const GameAddresses& a);

private:
    // The wrapped child is still the open map's first child, a slot holding the wrapper.
    bool still_wrapped(const GameAddresses& a) const;
    BYTE* child_ = nullptr;   // the map menu's first child, its handler the helper's
    BYTE* menu_ = nullptr;    // the map's menu
    void* original_ = nullptr;
};

}  // namespace zap
