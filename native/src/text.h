// The helper's popups as the game's message routine lays them out: a set number of characters a
// line, cut where a line ends, with no line-break code (docs/re/memory.md, "Other players' items"
// and "Game functions the helper calls"). Pure text, apart from fit_notice_box.
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
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
// A message's frames left (32-bit), counting down by one a frame.
constexpr ptrdiff_t kMessageFramesLeft = 0x1EC;
// The window over a message's text (`0xA8210`; docs/re/memory.md, "Game functions the helper
// calls"): the glyph count and line length it reads, its half-width and half-height, and the
// steps the message's update adds to those each frame the box opens and takes off each frame it
// closes (32-bit each); the box's bottom (a double), 0 until the box first opens.
constexpr ptrdiff_t kMessageGlyphs = 0x200, kMessageLineLength = 0x204;
constexpr ptrdiff_t kTextWindowX = 0x214, kTextWindowY = 0x218;
constexpr ptrdiff_t kTextWindowStepX = 0x1F4, kTextWindowStepY = 0x1F8;
constexpr ptrdiff_t kMessageBoxBottom = 0x160;
// From this many rows of glyphs on, a row the window cuts wrong comes back whole while the box
// opens and closes (with three, row 1 is cut two pixels short for two frames).
constexpr int kMiscutRows = 4;

// The helper's text is kept in the case it was sent or written in, mixed case for its own;
// capitals only where they are drawn: the 8 x 8 font, and every translation but English(HD)
// (translation.h, hd_case). Letters alone change: a record's icon byte (0x6E and 0x77 among
// them) never goes through these, it is put before the text they make.
constexpr char capital(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c; }
std::string in_capitals(std::string text);
// One of the helper's fixed texts in capitals, made at compile time: Capitals kName{kText}.
template <size_t N>
struct Capitals {
    char text[N] = {};
    constexpr explicit Capitals(const char (&s)[N]) {
        for (size_t i = 0; i < N; ++i) text[i] = capital(s[i]);
    }
    constexpr operator const char*() const { return text; }
    constexpr operator std::string_view() const { return {text, N - 1}; }
    static constexpr size_t size() { return N - 1; }
};

// The logo glyph, which starts a stand-in's name (the record's own icon stays 0, so the popup
// shows the name as written and it can be centered), and the name where no label is.
constexpr uint8_t kApItemGlyph = 0xEE;
constexpr char kApItemName[] = "AP Item";

// A label for another player's item at a map cell (requests.h, parse_labels): its parts in the
// case the client sent them, and the stand-in's name laid out from them (lay_out).
struct Label {
    // The first line: its glyph and its text (kApItemName where "": first_text). For another
    // Zexion player's item N (`item`, -1 if none) the logo and kApItemName until records load,
    // then its record's icon and name (Game::resolve_item_labels).
    uint8_t glyph = kApItemGlyph;
    std::string first;
    std::string who;  // the second line, whom it is for ("" for none)
    int item = -1;
    // "@N NAME": item N's icon, but this name for it (this player's own OS Install Disk, which
    // the game's record calls DATA DISC).
    std::string item_name;
    bool pending() const { return item >= 0 && first.empty(); }
    std::string first_text() const;
    // The stand-in's name in the 8 x 8 font, set by lay_out: the first line (glyph first) and
    // `who` in capitals, each centered in the longer, cut to the record's 49 bytes; and the
    // widest line, the pickup popup's width.
    std::string name;
    int width = 0;
    void lay_out();
};

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
// Game thread, right after the game made a message (and fit_notice_box): when its text is
// kMiscutRows rows or more, has the game show the text only while the box is fully open, all of
// it, so the box opens and closes empty; else, or with the message not as the game made it,
// leaves it alone.
void show_text_when_open(BYTE* message);

}  // namespace zap
