// The pause screen's disk row (the OS Install Disks goal), in place of KEYS. The pause screen
// (built by 0x168BD0 each time it opens) has a KEYS panel under its entries: a 4-glyph KEYS label
// at (144, 208) and nine sprites (key slots 0-3, key icons 4-7 at frames 384-387, Zexion's icon
// 8). The game builds it only once flag 39 is set or a key is owned (the `jne` at 0x168C7D)
// (docs/re/memory.md, "The pause screen's KEYS row").
//
// While a count is set, that `jne` becomes `jmp` and, on a pause screen whose shape checks out,
// the row shows a disc and the count ("2 OF ?", or "2 OF 15" once told) across the label's 4
// glyphs and the 4 key slots; the other sprites are hidden. The disc is the helper's own 24 x 24
// image (kDiscRows, painted at (kDiscX, kDiscY) in the sprite texture while the row is on) when
// it is painted, else the game's small Data Disc (frame 829). A row already turned is left alone.
// The branch is put back when off and before an unload, only where it still reads `jmp`.
//
// The helper's disc is one frame-list entry the helper owns: a copy of frame 829's (0x38 bytes)
// with the image's texture rectangle, in a page allocated once and never freed. The pause
// sprites keep their template (+0x30), so the game never frees their list (0x745C0), and a
// sprite still pointing at the entry after an unload points at live memory. Whenever the image is
// not painted (the row off, before an unload, a texture it could not paint into), the entry shows
// frame 829's rectangle instead, so an open pause screen shows the game's disc, enlarged.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "addresses.h"

namespace zap {

constexpr int kRowCells = 8;  // the characters the row can show: "41 OF 41"

// The helper's disc: 24 x 24, white, at (kDiscX, kDiscY) in the 2048 x 2048 sprite texture, a
// place transparent in the game's art that no sprite definition uses (docs/re/memory.md, "The
// pause screen's KEYS row").
constexpr int kDiscX = 128, kDiscY = 1008, kDiscSize = 24;
extern const char kDiscRows[kDiscSize][kDiscSize + 1];

// The row's text, centered on its 8 cells: "2 OF ?" before the player has been told how many
// disks are needed (`required` 0), "2 OF 15" after.
std::string pause_row_text(int count, int required);

class PauseDiskRow {
public:
    // Game thread: the text to show ("" turns it off, the branch then put back).
    void set(std::string text) { text_ = std::move(text); }

    // Game thread: a save of a disk seed is loaded and `text` is its count from the seed's file
    // ("" for none): shown until `set` gives the client's, so a pause screen opened at once has
    // the row with no flash of the game's KEYS.
    void prepare(std::string text) { prepared_ = std::move(text); }

    // Whether the row is on (a text set or prepared): the helper's disc is then painted.
    bool on() const { return !text_.empty() || !prepared_.empty(); }

    // Game thread, each frame: the branch while on (or put back while off), and the row turned
    // on a pause screen that has not been. `disc`: the helper's disc is painted now.
    void watch(const GameAddresses& addresses, bool disc);

    // Game thread: the branch put back, and the disc's entry showing frame 829 (before an unload,
    // before the image is cleared).
    void restore(const GameAddresses& addresses);

    bool patched() const { return patched_; }
    const BYTE* disc_entry() const { return entry_; }  // tests

private:
    bool patch(const GameAddresses& addresses, uint8_t from, uint8_t to);
    void turn(const GameAddresses& addresses, BYTE* menu, const std::string& text, bool disc);
    // Sprite `s` pointed at the helper's disc entry (made from its frame 829 the first time).
    bool show_disc(BYTE* s);
    // The entry's texture rectangle: the helper's image, or frame 829's.
    void entry_shows(bool disc);

    std::string text_;
    std::string prepared_;
    bool patched_ = false;
    BYTE* entry_ = nullptr;  // in a page never freed (above)
    float game_rect_[4] = {};  // frame 829's texture rectangle, as copied
};

}  // namespace zap
