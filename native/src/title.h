// The title screen's mod lines: ARCHIPELAGO MOD <version> and ©2026 MCAWFUL, centered in the
// black band at the bottom, while the helper is loaded, and above them, while no client is
// connected, START THE AP CLIENT TO PLAY A SEED (the owner's mock-ups, 2026-10-25).
//
// The game's title routine (0x93680, from disassembly; docs/re/memory.md, "The title screen")
// makes the copyright line once the title animation ends: a text object from 0x92300
// (characters, length, 32; each glyph's frame is its character), placed by 0xA79E0 at (88, 120),
// added at the tail of the draw list [draw_layer] (head +0x18, tail +0x20; an object's next
// +0x10, previous +0x18, list +0), its glyphs put right after it by 0xA8620, each glyph shown
// (+0x120) and given the title's palette and style, glyph 0 the © frame (0xA9). Leaving the
// title clears the list, which frees them.
//
// Each few frames, the helper looks through the draw list for that line (© then
// "GALLANT LEAF"); when it is there and the helper's lines are not, it makes them the same way,
// in the same list, with the line's own palette, style and tag. Nothing is kept between looks:
// both are found again in the list each time, so the game freeing them is never a problem.
// Before an unload, the helper's lines are hidden (the game frees them with the title).
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace zap {

struct TitleAddresses {
    BYTE* draw_layer = nullptr;      // holds the draw list new screen objects join
    void* text_new_fn = nullptr;     // (characters, length, 32): a text object and its glyphs
    void* text_place_fn = nullptr;   // (text, x, y)
    void* text_link_fn = nullptr;    // (text): its glyphs into its list
};

// The version and the credit (the credit's first character becomes the © glyph, as the game's
// own line's).
std::string title_mod_line(const std::string& version);
extern const char kTitleCredit[];
// And, while no client is connected, what to do to play (the owner's pick, 2026-10-25).
extern const char kTitleHint[];
// Where they go: centered on the 320-pixel screen, 8 pixels a character, in the band below the
// planet, 12 pixels apart (the owner's mock-ups): the hint, then the version, then the credit.
constexpr int kTitleHintY = 190, kTitleModY = 202, kTitleCreditY = 214;
int title_line_x(size_t length);

class TitleLines {
public:
    // The helper's version, for the first line (until set, no lines are made).
    void set_version(const std::string& version) { mod_line_ = title_mod_line(version); }

    // Game thread, each frame: adds the lines when the copyright line is up without them, and
    // shows them as the game shows its line's ©.
    // Returns "" or why it could not (only when it tried).
    // `client`: a client is connected (the hint is hidden).
    std::string watch(const TitleAddresses& a, bool client);
    // Game thread: the lines hidden, if up (before an unload).
    void hide(const TitleAddresses& a);
    // Whether the lines were in the list at the last look (for `hello`).
    bool shown() const { return shown_; }

private:
    std::string mod_line_;
    int frames_ = 0;
    bool shown_ = false;
    std::string last_error_;
};

}  // namespace zap
