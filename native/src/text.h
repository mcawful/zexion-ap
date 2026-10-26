// The helper's popups as the game lays them out: its message routine lays a text out a given
// number of characters a line and cuts wherever a line ends, with no line-break code, and sizes
// a one-line box to its text (docs/re/memory.md, "Other players' items", "Game functions the
// helper calls"). Pure text, apart from fit_notice_box.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace zap {

// The game's popup line width, 24 characters: `mov edx, 24` in its pickup code
// (docs/re/memory.md, "Other players' items"), and the width the helper lays its own popups and
// the stand-ins' names out in.
constexpr int kGamePopupWidth = 24;
// Where the helper's popups go and how long they stay, as the game's pickup popup.
constexpr int kPopupX = 0xA0, kPopupY = 0x58, kPopupFrames = 0xB4;
// A message's text past the 255 characters (and the NUL) of its slot overruns the next slot:
// the game copies it with no check (docs/re/memory.md, "Game functions the helper calls").
constexpr size_t kMessageMax = 255;
// The popup message's box: its centre's y (a double) and its half-height (docs/re/memory.md,
// "Game functions the helper calls").
constexpr ptrdiff_t kMessageY = 0x48, kMessageHalfHeight = 0x1FC;

// A label for another player's item: the stand-in's name as laid out, and the popup's line
// width that name is laid out for (its widest line).
struct Label {
    std::string name;
    int width = 0;
    // For another Zexion player's item: the item's record, whose icon and name the first line
    // is made of once the records are loaded (until then the name says AP ITEM), and the
    // second line.
    int item = -1;
    std::string who;
    // "@N NAME": item N's icon, but this name for it (this player's own OS Install Disk, which
    // the game's record calls DATA DISC).
    std::string item_name;
};

// A label's popup layout: `first` (which starts with its glyph) and `who`, each centered in
// the longer, which is the width.
Label layout_label(const std::string& first, const std::string& who);

// Words of `text` in lines of at most `width` characters; a word longer than a line is cut.
std::vector<std::string> wrap_words(const std::string& text, size_t width);
// `lines` as one popup text: with more than one line, each centered in per_line characters and
// padded to the end of its line; one line as it is.
std::string center_lines(const std::vector<std::string>& lines, int per_line);
// A received item's popup text: `head` (the icon and the game's name), then `suffix` ("FROM
// ALICE") from the next line, its words wrapped at per_line characters, every line centered in
// the widest line, whose width goes to `*width` (the popup's line width: the game sizes a box
// to it).
std::string popup_text(const std::string& head, const std::string& suffix, int per_line,
                       int* width = nullptr);
// A notice's popup text: `text` alone, left-aligned, its words wrapped whole at per_line
// characters (a word longer than a line split with a hyphen at the line's end), the box as
// wide as the widest line (`*width`), kept within the game's 256-byte message slot.
std::vector<std::string> wrap_hyphenated(const std::string& text, size_t width);
std::string notice_text(const std::string& text, int per_line, int* width = nullptr);
// Game thread, right after the game made a notice's message from `text`: fits the box to the
// notice's real lines when it ends in its blank line (notice_text), else leaves it alone.
void fit_notice_box(BYTE* message, const std::string& text, int per_line);

}  // namespace zap
