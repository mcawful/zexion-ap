// The disk count on the file select (the OS Install Disks goal), in place of the keys.
//
// The file select (and COPY FILE and DELETE FILE) is a menu whose first panel (handler
// 0xB6920) holds the three save entries: each a widget whose label is 145 glyphs, 5 rows of 29,
// its slot at +0x230 (1 to 30) and the save block it shows at +0x1F8 (docs/re/memory.md, "The
// file select"). The game fills an entry (0xDF840) through its handler on event 0xF (opening
// the screen, paging), and directly after a copy or delete. Six glyphs show the keys: 85 and 86
// (row 2, columns 27 and 28) a squashed KEYS, and 114, 115, 143, 144 the keys as circles; all
// blank with no key and the save's flag 39 clear.
//
// For the slots the client lists (those holding the seed), the helper fills the keys' block
// with the disks (the owner's layout): a squashed DISK in place of KEYS, in the game's style
// (two font glyphs the helper paints, kDiskHeaderGlyphs), the count in two digits on the row
// below ("03", moved 5 pixels down to center on the items bracket, and put back for any other
// entry and when off), and the last row blank, drawn as the keys are. With no disk the block
// is blank, as with no key.
// While on, each entry's handler is the helper's, which calls the game's and rewrites the entry
// after event 0xF, so a filled entry never shows a frame of keys; a frame-call watch catches
// the fills that bypass the handler. Every value is checked before a write; an entry whose key
// glyphs are not the game's (or the helper's) is left alone, and so is an entry whose handler
// it does not hold. When off and before an unload, in menus still open, it puts back the frames
// the game last filled each entry's keys' block with (while the entry still shows the
// helper's), the count's glyphs in their row, and the handlers.
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace zap {

struct FileAddresses {
    BYTE* current_menu = nullptr;  // holds the top menu
    void* file_panel_fn = nullptr;  // the entries' panel handler
    void* entry_fns[3] = {};        // the file select's, COPY FILE's and DELETE FILE's entry handlers
    void* hook = nullptr;           // the helper's stand-in for an entry's handler
};

// The squashed DISK: "DI" and "SK", drawn as the game's KEYS (0x81 "KE", 0x82 "YS"), into font
// glyphs empty in the game's art (found by their alpha in Sprites.png, as 0xFB), painted while
// the file select's count is on (apitem.h FontGlyph).
constexpr uint8_t kDiskHeaderGlyphs[2] = {0xFC, 0xFD};
extern const char kDiskHeaderRows[2][8][9];

// The count under DISK: two digits ("03"), 0 to 99.
std::string file_disk_digits(int value);

class FileDisks {
public:
    // Game thread: what to show (an empty slot set turns it off): one count for the slots the
    // client lists, or each slot's own (offline, saves of several seeds: offline.h).
    void set(int count, std::set<int> slots);
    void set_counts(std::map<int, int> counts) { counts_ = std::move(counts); }
    bool on() const { return !counts_.empty(); }

    // Game thread, each frame: wraps the entries' handlers on the open file screens (while
    // on, else puts them back), and rewrites entries still showing keys.
    void watch(const FileAddresses& addresses);

    // Game thread, from the stand-in handler: the game's handler for `widget` (null if not one
    // this wrapped), to call first.
    void* original(void* widget) const;
    // ...then, after event 0xF (the fill), the entry rewritten.
    void on_event(const FileAddresses& addresses, void* widget, intptr_t event);

    // Game thread: every wrapped handler put back, in menus still open (before an unload).
    void restore(const FileAddresses& addresses);
    bool holds_hook() const { return !wrapped_.empty(); }

    // For a refused unload, which turns it back on as it was.
    // Each listed slot's count (a refused unload sets them again).
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

// A `disk_slots` request: "disk_slots" (off) or "disk_slots COUNT REQUIRED SLOTS", the slots a
// comma list or "-". Returns "" or what is wrong.
std::string parse_disk_slots(const std::string& request, int& count, int& required,
                             std::set<int>& slots);

}  // namespace zap
