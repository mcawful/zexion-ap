// The save entries of the file select, COPY FILE and DELETE FILE (docs/re/memory.md, "The file
// select"), rewritten in two ways:
// - The disk count in place of the keys. For each listed slot, the keys' six glyphs show a
//   squashed DISK and the count in two digits (5 px lower, on the items bracket), then a blank
//   row; with no disk, all blank.
// - The AP logo on AP saves: a 16 x 16 image over four font glyphs (kApLogoGlyphs) in the blank
//   cells right of FILE NN (rows 0 and 1, columns 16 and 17), raised 4 px to center on that line.
//   Only over blank cells drawn in palette 26 at style 0x15 (white), under which a glyph shows
//   its own colors.
// While either is on, each entry's handler is the helper's wrapper: it calls the game's, then
// rewrites the entry after the fill (event 0xF), so no frame shows the game's; a frame-call
// watch catches fills that bypass the handler. Written only over the game's glyphs or the
// helper's own; when off and before an unload, the game's frames, the moved glyphs' rows and the
// handlers go back, in menus still open.
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "addresses.h"
#include "apitem.h"

namespace zap {

// The squashed DISK: "DI" and "SK", drawn as the game's KEYS (0x81 "KE", 0x82 "YS"), into font
// glyphs empty in the game's art (found by their alpha in Sprites.png, as 0xFB), painted while
// the file select's count is on (apitem.h FontGlyph).
constexpr uint8_t kDiskHeaderGlyphs[2] = {0xFC, 0xFD};
extern const char kDiskHeaderRows[2][8][9];

// The AP logo: four font glyphs empty in the game's art, two by two in the font (0x8C and 0x8D
// over 0x9C and 0x9D), so that one 16 x 16 image at kApLogoX, kApLogoY covers them; painted
// while a listed slot is an AP save. The world's AP item logo, in its colors (apitem.h
// glyph_rgb), painted whiter through a new game's fade to white (Game::watch_files).
constexpr uint8_t kApLogoGlyphs[4] = {0x8C, 0x8D, 0x9C, 0x9D};
constexpr int kApLogoX = font_x(kApLogoGlyphs[0]), kApLogoY = font_y(kApLogoGlyphs[0]);
static_assert(kApLogoGlyphs[1] == kApLogoGlyphs[0] + 1 &&
                  kApLogoGlyphs[2] == kApLogoGlyphs[0] + 16 &&
                  kApLogoGlyphs[3] == kApLogoGlyphs[0] + 17 && kApLogoGlyphs[0] % 16 < 15,
              "the logo's glyphs two by two in the font");
extern const char kApLogoRows[16][17];

// A new game's transition (newgame_fn queued, a 40-frame timer counting down), during which the
// screen fades to white: how far toward white the logo is painted (FontGlyph::set_white) with
// `left` frames to go, 0 when no new game is queued.
constexpr int32_t kNewGameFadeFrames = 40;
constexpr int new_game_white(bool queued, int32_t left) {
    return queued && left > 0 && left <= kNewGameFadeFrames
               ? 255 * (kNewGameFadeFrames - left + 1) / kNewGameFadeFrames
               : 0;
}

// The count under DISK: two digits ("03"), 0 to 99.
std::string file_disk_digits(int value);

class FileEntries {
public:
    // Game thread: what to show, a count by slot (none turns the count off): the client's for the
    // slots holding its seed, and each known seed's save its own (offline.h).
    void set_counts(std::map<int, int> counts) { counts_ = std::move(counts); }
    // Game thread: the slots holding AP saves (a seed the helper knows), which get the logo.
    void set_ap_slots(std::set<int> slots) { ap_slots_ = std::move(slots); }
    bool on() const { return disks_on() || marks_on(); }
    bool disks_on() const { return !counts_.empty(); }
    bool marks_on() const { return !ap_slots_.empty(); }

    // Game thread, each frame: wraps the entries' handlers on the open file screens (while
    // on, else puts them back), and rewrites entries still showing the game's glyphs.
    void watch(const GameAddresses& addresses);

    // Game thread, from the helper's entry handler: the game's handler for `widget` (null if
    // not one this wrapped), to call first.
    void* original(void* widget) const;
    // ...then, after event 0xF (the fill), the entry rewritten.
    void on_event(const GameAddresses& addresses, void* widget, intptr_t event);

    // Game thread: every wrapped handler put back, in menus still open (before an unload).
    void restore(const GameAddresses& addresses);
    bool holds_hook() const { return !wrapped_.empty(); }

    // Each listed slot's count, and the AP saves' slots.
    const std::map<int, int>& counts() const { return counts_; }
    const std::set<int>& ap_slots() const { return ap_slots_; }

private:
    struct Wrapped {
        BYTE* menu = nullptr;
        void* original = nullptr;
        // The keys' block as the game last filled it, and as the helper then wrote it (while
        // `keys_written`, until the game fills the entry again).
        int32_t keys_game[6] = {};
        int32_t keys_shown[6] = {};
        bool keys_written = false;
        // The logo's cells as the game last filled them, while `marked` (the logo written).
        int32_t logo_game[4] = {};
        bool marked = false;
    };
    void update(BYTE* entry, Wrapped& w);
    void forget_fills(BYTE* (&glyphs)[145], Wrapped& w);
    bool rewrite(BYTE* entry, BYTE* (&glyphs)[145], Wrapped& w);
    void put_back(BYTE* (&glyphs)[145], Wrapped& w);
    bool mark(BYTE* entry, BYTE* (&glyphs)[145], Wrapped& w);
    void unmark(BYTE* (&glyphs)[145], Wrapped& w);
    std::map<BYTE*, Wrapped> wrapped_;  // entry -> its menu and the game's handler
    std::map<int, int> counts_;  // slot -> the count it shows
    std::set<int> ap_slots_;  // the slots that get the logo
};

}  // namespace zap
