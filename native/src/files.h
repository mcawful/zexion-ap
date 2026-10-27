// The disk count on the file select, COPY FILE and DELETE FILE, in place of the keys
// (docs/re/memory.md, "The file select"). For each listed slot, the keys' six glyphs show a
// squashed DISK and the count in two digits (5 px lower, on the items bracket), then a blank
// row; with no disk, all blank. While on, each entry's handler is the helper's wrapper: it calls
// the game's, then rewrites the entry after the fill (event 0xF), so no frame shows keys; a
// frame-call watch catches fills that bypass the handler. Written only over the game's key
// glyphs or the helper's own; when off and before an unload, the game's frames, the count's row
// and the handlers go back, in menus still open.
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>

#include "addresses.h"

namespace zap {

// The squashed DISK: "DI" and "SK", drawn as the game's KEYS (0x81 "KE", 0x82 "YS"), into font
// glyphs empty in the game's art (found by their alpha in Sprites.png, as 0xFB), painted while
// the file select's count is on (apitem.h FontGlyph).
constexpr uint8_t kDiskHeaderGlyphs[2] = {0xFC, 0xFD};
extern const char kDiskHeaderRows[2][8][9];

// The count under DISK: two digits ("03"), 0 to 99.
std::string file_disk_digits(int value);

class FileDisks {
public:
    // Game thread: what to show, a count by slot (none turns it off): the client's for the slots
    // holding its seed, and each known seed's save its own (offline.h).
    void set_counts(std::map<int, int> counts) { counts_ = std::move(counts); }
    bool on() const { return !counts_.empty(); }

    // Game thread, each frame: wraps the entries' handlers on the open file screens (while
    // on, else puts them back), and rewrites entries still showing keys.
    void watch(const GameAddresses& addresses);

    // Game thread, from the helper's entry handler: the game's handler for `widget` (null if
    // not one this wrapped), to call first.
    void* original(void* widget) const;
    // ...then, after event 0xF (the fill), the entry rewritten.
    void on_event(const GameAddresses& addresses, void* widget, intptr_t event);

    // Game thread: every wrapped handler put back, in menus still open (before an unload).
    void restore(const GameAddresses& addresses);
    bool holds_hook() const { return !wrapped_.empty(); }

    // Each listed slot's count.
    const std::map<int, int>& counts() const { return counts_; }

private:
    struct Wrapped {
        BYTE* menu = nullptr;
        void* original = nullptr;
        // The keys' block as the game last filled it, and as the helper then wrote it (while
        // `written`, until the game fills the entry again).
        int32_t game[6] = {};
        int32_t shown[6] = {};
        bool written = false;
    };
    void update(BYTE* entry, Wrapped& w);
    bool rewrite(BYTE* entry, BYTE* (&glyphs)[145], Wrapped& w);
    void put_back(BYTE* (&glyphs)[145], Wrapped& w);
    std::map<BYTE*, Wrapped> wrapped_;  // entry -> its menu and the game's handler
    std::map<int, int> counts_;  // slot -> the count it shows
};

}  // namespace zap
