// The helper's popups as the game's message routine lays them out: a set number of characters a
// line, cut where a line ends, with no line-break code (docs/re/memory.md, "Other players' items"
// and "Game functions the helper calls"). Pure text, apart from fit_notice_box.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace zap {

// What the game's 8 x 8 font draws besides capital letters and digits (the popups' sets are
// its first 7 and 8: parse_batch).
constexpr char kFontPunctuation[] = " .,'!?-:()/&";

// The pickup popup's line width (docs/re/memory.md, "Other players' items"), which the helper's
// popups and the stand-ins' names use too.
constexpr int kGamePopupWidth = 24;
// Where the helper's popups go and how long they stay, as the game's pickup popup.
constexpr int kPopupX = 0xA0, kPopupY = 0x58, kPopupFrames = 0xB4;
// A message slot's characters before its NUL: the game copies a longer text over the next slot.
constexpr size_t kMessageMax = 255;
// The popup message's box: its center's y (a double) and its half-height (32-bit).
constexpr ptrdiff_t kMessageY = 0x48, kMessageHalfHeight = 0x1FC;

// A label for another player's item: the stand-in's name as laid out, and its widest line.
struct Label {
    std::string name;
    int width = 0;
    // `item`: another Zexion player's item N (-1 if none); its record's icon and name make the
    // first line once records load (AP ITEM until then). `who`: the second line.
    int item = -1;
    std::string who;
    // "@N NAME": item N's icon, but this name for it (this player's own OS Install Disk, which
    // the game's record calls DATA DISC).
    std::string item_name;
    // The first line (without its glyph; an @N item's NAME) and `who` in the case they were sent
    // in, for English(HD)'s mixed case (Game::hd_label_text); empty where none was sent.
    std::string own_first, own_who;
};

// A label's layout: `first` (starting with its glyph) and `who`, each centered in the longer.
Label layout_label(const std::string& first, const std::string& who);

// Words of `text` in lines of at most `width` characters; a word longer than a line is cut.
std::vector<std::string> wrap_words(const std::string& text, size_t width);
// `lines` as one popup text: with more than one line, each centered in per_line characters,
// all but the last padded to the line's end; one line as it is.
std::string center_lines(const std::vector<std::string>& lines, int per_line);
// A received item's popup text: `head` (the icon and the game's name), then `suffix` ("FROM
// ALICE") wrapped at per_line from the next line, every line centered in the widest, whose width
// goes to `*width` (the popup's line width, which the game sizes the box to).
std::string popup_text(const std::string& head, const std::string& suffix, int per_line,
                       int* width = nullptr);
// `text` in lines of at most `width`, words whole; a longer word split, a hyphen ending each line.
std::vector<std::string> wrap_hyphenated(const std::string& text, size_t width);
// A notice: `text` alone, left-aligned (wrap_hyphenated), the box as wide as its widest line
// (`*width`), within the 256-byte message slot.
std::string notice_text(const std::string& text, int per_line, int* width = nullptr);
// Game thread, right after the game made a notice's message from `text`: fits the box to the
// notice's real lines when it ends in its blank line (notice_text), else leaves it alone.
void fit_notice_box(BYTE* message, const std::string& text, int per_line);

}  // namespace zap
