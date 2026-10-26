// The pause screen's disk row (the OS Install Disks goal), in place of KEYS. The pause screen
// (built by 0x168BD0 each time it opens) has a KEYS panel under its entries: a 4-glyph KEYS label
// at (144, 208) and nine sprites (key slots 0-3, key icons 4-7 at frames 384-387, Zexion's icon
// 8). The game builds it only once flag 39 is set or a key is owned (the `jne` at 0x168C7D)
// (docs/re/memory.md, "The pause screen's KEYS row").
//
// While a count is set, that `jne` becomes `jmp` and, on a pause screen whose shape checks out,
// the row shows the Data Disc icon (frame 829) and the count ("2 OF ?", or "2 OF 15" once told)
// across the label's 4 glyphs and the 4 key slots; the other sprites are hidden. A row already
// showing 829 is left alone. The branch is put back when off and before an unload, only where it
// still reads `jmp`.
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
