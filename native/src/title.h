// The title screen's mod lines: ARCHIPELAGO MOD <version> and ©2026 MCAWFUL, centered under the
// game's copyright line, and above them, while no client is connected, START THE AP CLIENT TO
// PLAY A SEED. They are made as the game makes that line, in its draw list (draw_layer), which
// frees them with the title; nothing is kept between looks, both are found again each time.
// Before an unload they are hidden. In English(HD) the version's line and the hint are rebuilt
// in the translation's font and shown only with no menu open (docs/re/memory.md, "The title
// screen"; native/README.md, "The title screen's lines").
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
    void set_version(const std::string& version) { mod_line_ = "Archipelago Mod " + version; }

    // Game thread, every frame call (looks every 9th, every one with `hd`, English(HD) shown):
    // adds the lines when the copyright line is up without them, and shows them as the game shows
    // its line's ©. `client`: a client is connected (the hint hidden). Returns "" or why the lines
    // could not be made on this title.
    std::string watch(const GameAddresses& a, bool client, bool hd = false);
    // Game thread: the lines hidden, if up (before an unload).
    void hide(const GameAddresses& a);
    // Whether the lines were in the list at the last look (for `hello`).
    bool shown() const { return shown_; }

private:
    std::string mod_line_;
    int frames_ = 0;
    bool hd_ = false;  // English(HD) at the last call
    bool shown_ = false;
    std::string last_error_;
};

}  // namespace zap
