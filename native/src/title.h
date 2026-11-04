// The title screen's mod lines: ARCHIPELAGO MOD <version> and ©2026 MCAWFUL, centered in the
// black band at the bottom, while the helper is loaded, and above them, while no client is
// connected, START THE AP CLIENT TO PLAY A SEED.
//
// The game's title routine (0x93680, from disassembly; docs/re/memory.md, "The title screen")
// makes the copyright line once the title animation ends: a text object from 0x92300
// (characters, length, 32; each glyph's frame is its character), placed by 0xA79E0 at (88, 120),
// added at the tail of the draw list [draw_layer] (head +0x18, tail +0x20; an object's next
// +0x10, previous +0x18, list +0), its glyphs put right after it by 0xA8620, each glyph shown
// (+0x120) and given the title's palette and style, glyph 0 the © frame (0xA9). Leaving the
// title clears the list, which frees them.
//
// Every 9th frame call (every one in English(HD)), the helper looks through the draw list for
// that line (© then "GALLANT LEAF"); when it is there and the helper's lines are not, it makes
// them the same way, in the same list, with the line's own palette, style and tag. Nothing is kept between looks:
// both are found again in the list each time, so the game freeing them is never a problem.
// Before an unload, the helper's lines are hidden (the game frees them with the title).
//
// In English(HD) the version's line and the hint are shown in the translation's font, in mixed
// case: each line's translated text built by the game's builder (translation.h, Rebuild::first)
// centered on the screen, its pixel glyphs hidden; the credit stays in pixels, as the game's
// own copyright line does. The game's text destructor (0x92010) frees a text object's
// translated glyphs with it. Those glyphs are drawn from the HD list, not the title's, and the
// file select (and Option, from it) opens over the title without clearing it, so they are
// shown only while no menu is open (menu_state 0) as well; the pixel ones follow the game's ©.
// The lines are made only while no menu is open, so none join the list over a menu.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "addresses.h"

namespace zap {

// After ARCHIPELAGO MOD and the version, the credit (its first character becomes the © glyph,
// as the game's own line's), and, while no client is connected, what to do to play.
extern const char kTitleCredit[];
extern const char kTitleHint[];

class TitleLines {
public:
    // The helper's version, for the first line (until set, no lines are made).
    void set_version(const std::string& version) {
        mod_line_ = "ARCHIPELAGO MOD " + version;
        mod_line_hd_ = "Archipelago Mod " + version;
    }

    // Game thread, every frame call (looks every 9th, every one with `hd`): adds the lines when
    // the copyright line is up without them, and shows them as the game shows its line's © (in
    // English(HD), the translated ones only with no menu open).
    // Returns "" or why the lines could not be made on this title. `client`: a client is connected (the hint is
    // hidden). `hd`: English(HD) shows (its two lines in the translation's font).
    std::string watch(const GameAddresses& a, bool client, bool hd = false);
    // Game thread: the lines hidden, if up (before an unload).
    void hide(const GameAddresses& a);
    // Whether the lines were in the list at the last look (for `hello`).
    bool shown() const { return shown_; }

private:
    std::string mod_line_, mod_line_hd_;
    int frames_ = 0;
    bool hd_ = false;  // English(HD) at the last call
    bool shown_ = false;
    std::string last_error_;
};

}  // namespace zap
