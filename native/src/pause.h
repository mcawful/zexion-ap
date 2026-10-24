// The pause screen's disk row (the OS Install Disks goal), in place of KEYS.
//
// The pause screen (built by 0x168BD0 each time it opens) has a second panel under its entries:
// a KEYS label (4 glyphs at x 144 to 168, y 208) and nine sprites: four key slots (0 to 3), the
// four key icons (4 to 7, the small icons' frames 384 to 387) in a diamond round (160, 188), and
// Zexion's icon (8, shown once Zexion is held). The game builds that panel only once the
// scanner's flag 39 is set or a key is owned (the `jne` at 0x168C7D), so in a game without keys
// there is none before the scanner (docs/re/memory.md, "The pause screen's KEYS row").
//
// While the disk count is shown, the helper makes that `jne` a `jmp` (the panel always built),
// and once the pause screen is on top, turns the row into the disks: the small Data Disc icon
// (frame 829, the one the game's own pause screen shows for a held disc) in the middle, the key
// slots and icons hidden, and the count ("2 OF ?", or "2 OF 15" once the scanner has said how
// many) on the label's line: the label's 4 glyphs carry the middle four characters and the four
// key slots, given the font, two on each side. Everything is written only after the screen's
// shape is checked; the branch is put back when the count is off and before an unload, and only
// where it still reads as written. The pause screen is built afresh each time it opens, so a row
// already turned (its icon reading 829) is left as it is.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace zap {

struct PauseAddresses {
    BYTE* current_menu = nullptr;      // holds the top menu
    void* equipment_fn = nullptr;      // EQUIPMENT's callback (the pause screen's entry)
    void* equipment_hook = nullptr;    // or the helper's, while it shows the disk on that screen
    void* widget_handler = nullptr;    // the generic widget handler
    BYTE* sprite_templates = nullptr;  // holds the pointer to the sprite definitions' templates
    void* sprite_sheet_fn = nullptr;   // (object, template): gives it a sprite definition's frames
    BYTE* keys_branch = nullptr;       // the `jne` (75 2E) that skips the KEYS panel
};

constexpr int kRowCells = 8;  // the characters the row can show: "41 OF 41"

// The row's text, centered on its 8 cells: "2 OF ?" before the player has been told how many
// disks are needed (`required` 0), "2 OF 15" after.
std::string pause_row_text(int count, int required);

class PauseDiskRow {
public:
    // Game thread: the text to show ("" turns it off, the branch then put back).
    void set(std::string text) { text_ = std::move(text); }

    // Game thread, each frame: the branch while on (or put back while off), and the row turned
    // on a pause screen that has not been.
    void watch(const PauseAddresses& addresses);

    // Game thread: the branch put back (before an unload).
    void restore(const PauseAddresses& addresses);

    bool patched() const { return patched_; }

private:
    bool patch(const PauseAddresses& addresses, uint8_t from, uint8_t to);
    void turn(const PauseAddresses& addresses, BYTE* menu);

    std::string text_;
    bool patched_ = false;
};

}  // namespace zap
