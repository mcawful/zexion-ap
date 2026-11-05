// The pause screen's disk row (the OS Install Disks goal), in place of KEYS. The game builds the
// KEYS row only once flag 39 is set or a key is owned (keys_branch, a `jne`); while a count is
// set, that `jne` becomes `jmp`, and on a pause screen whose shape checks out the row shows a
// disc and the count across KEYS's 4 glyphs and the 4 key slots, the other sprites hidden. The
// disc is the helper's image (kDiscRows) when painted, else the game's frame 829, through one
// frame-list entry in a page never freed: a sprite still on it after an unload points at live
// memory. While on, the pause menu's way back from OPTION MENU (options_back_fn) is the helper's
// wrapper, which turns the row again in the same call. Both are put back when off and before an
// unload, only where they still read as the helper wrote them (docs/re/memory.md, "The pause
// screen's KEYS row"; native/README.md, "The OS Install Disks goal").
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "addresses.h"
#include "translation.h"  // BuiltMark

namespace zap {

constexpr int kRowCells = 8;  // the characters the row can show: "41 OF 41"

// The helper's disc: 24 x 24, white, at a free place in the sprite texture (docs/re/memory.md,
// "The pause screen's KEYS row").
constexpr int kDiscX = 128, kDiscY = 1008, kDiscSize = 24;
extern const char kDiscRows[kDiscSize][kDiscSize + 1];

// The row's text, centered on its 8 cells: "2 of ?" before the player has been told how many
// disks are needed (`required` 0), "2 of 15" after; in capitals in the cells, and in a
// translation trimmed, as hd_case shows it.
std::string pause_row_text(int count, int required);

class PauseDiskRow {
public:
    // Game thread: the text to show ("" turns it off, the branch then put back).
    void set(std::string text) { text_ = std::move(text); }

    // Game thread: a save of a disk seed is loaded, `text` its count from the seed's file ("" for
    // none): shown until `set` gives the client's, so the game's KEYS never flashes.
    void prepare(std::string text) { prepared_ = std::move(text); }

    // Whether the row is on (a text set or prepared): the helper's disc is then painted.
    bool on() const { return !text_.empty() || !prepared_.empty(); }

    // Game thread, each frame: the branch while on (or put back while off), and the row turned
    // on a pause screen that has not been. `disc`: the helper's disc is painted now.
    void watch(const GameAddresses& addresses, bool disc);

    // Game thread: the branch and the pause menu's way back put back, and the disc's entry
    // showing frame 829 (before an unload, before the image is cleared).
    void restore(const GameAddresses& addresses);

    // Game thread, from the wrapper right after the game's way back from OPTION MENU has run on
    // `menu` (before the frame is drawn): the row turned on it, if it is open and on.
    void returned(const GameAddresses& addresses, BYTE* menu, bool disc);

    bool patched() const { return patched_; }
    // Whether an open menu may still hold the wrapper (unload must put it back first).
    bool holds_hook() const { return hooked_ != nullptr; }
    const BYTE* disc_entry() const { return entry_; }  // tests

private:
    const std::string& shown() const { return text_.empty() ? prepared_ : text_; }
    void turn(const GameAddresses& addresses, BYTE* menu, const std::string& text, bool disc);
    void hook_return(const GameAddresses& addresses);
    void unhook_return(const GameAddresses& addresses);
    // Sprite `s` pointed at the helper's disc entry (made from its frame 829 the first time).
    bool show_disc(BYTE* s);
    // The entry's texture rectangle: the helper's image, or frame 829's.
    void entry_shows(bool disc);

    std::string text_;
    std::string prepared_;
    bool patched_ = false;
    BYTE* entry_ = nullptr;  // in a page never freed (above)
    float game_rect_[4] = {};  // frame 829's texture rectangle, as copied
    BuiltLabel hd_;  // in a translation: the label whose translated text is the helper's count
    bool slots_hidden_ = false;  // the key slots' characters hidden for a translation
    BYTE* hooked_ = nullptr;     // the pause menu whose way back is the wrapper
};

}  // namespace zap
