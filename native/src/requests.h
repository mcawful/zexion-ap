// The pipe's requests that carry data, parsed (dllmain.cpp runs them; native/README.md lists
// them). Every parser returns "" or what is wrong, and checks every value it keeps.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "text.h"  // Label

namespace zap {

struct OfflineSeed;  // offline.h

struct Cas {
    uint8_t item;  // inventory byte 0..49
    uint8_t expected;
    uint8_t value;
};

struct Popup {
    uint8_t item;
    std::string suffix;  // already filtered to the font (see parse_batch)
    bool bare = false;   // a notice: the text alone, no icon or name (an Area Map's hint)
    bool silent = false;  // no pickup sound (the helper's offline notices, game_offline.cpp)
    // A client's (from `apply`), not one of the helper's own notices: held for its save.
    bool counted = false;
    // The save a client's popup is for (the randomizer save loaded at `apply`): it waits while
    // another save is loaded. None: shown in any save.
    std::shared_ptr<const OfflineSeed> origin;
};

struct Batch {
    std::vector<Cas> cas;
    bool refresh = false;
    std::vector<Popup> popups;
};

// Bytes at an offset into the loaded randomizer section (kSectionSize bytes at rando_blob).
struct SectionBytes {
    uint16_t offset;
    std::vector<uint8_t> bytes;
};

// One room of the randomizer menu's start table: its map cell and flag (1 where the room holds
// object 25).
struct StartRoom {
    int16_t x;
    int16_t y;
    uint8_t flag;
};

// What an AP new game needs: the section must already hold `expect` (the seed's mode), and
// then gets `write` (the AP item table, start, starting items). `menu` (offsets into the
// randomizer menu's state) sets the menu up as the seed's mode for the ARCHIPELAGO choice;
// without it the choice is not offered and only a RANDOMIZER game started by hand gets the
// plan.
struct NewGamePlan {
    std::vector<SectionBytes> expect;
    std::vector<SectionBytes> write;
    std::vector<SectionBytes> menu;
    // The menu's start table as it builds it when it opens; for a mode with Double Teleport,
    // whose placement reads it. Filled in before the placement if the menu has not built it.
    std::vector<StartRoom> start_table;
};

constexpr size_t kStartTableMax = 64;  // the rooms the menu's builder allows

constexpr size_t kSectionSize = 0x908;
constexpr size_t kMenuSize = 0x114;  // the randomizer menu's state, as the game saves it

// "ap_newgame" followed by lines "expect OFFSET HEX", "write OFFSET HEX" (in the section) and
// "menu OFFSET HEX" (in the menu's state); OFFSET decimal or 0x hex, HEX an even number of hex
// digits; every range must lie inside its block. Also "start_table HEX": 5 bytes per room
// (x and y, 16-bit, and the flag), at most 64 rooms, cells on the 54 x 54 map, flags 0 or 1.
std::string parse_newgame(const std::string& request, NewGamePlan& out);

// "ap_labels" followed by lines "label X Y HEX": the name, as hex bytes, for the stand-in
// item at map cell (x, y), both 0 to 53. Names keep only what the game's font draws
// (letters made upper-case, digits, space and . , ' ! ? - : ( ) / &), and of each of its two
// lines only the first line it wraps to: the item's at 23 characters (after the logo glyph),
// whom it is for at 24.
std::string parse_labels(const std::string& request, std::map<int, Label>& out);

// Hex digits (either case), two to a byte, appended to `out`: false for none, an odd count or
// anything else.
bool parse_hex(const std::string& text, std::vector<uint8_t>& out);
// A comma list of numbers ("4,12"), each 1 to `max_digits` digits and from `lo` to `hi`,
// appended to `out` in order: false for anything else.
bool parse_number_list(const std::string& text, size_t max_digits, int lo, int hi,
                       std::vector<int>& out);

// "config name=rva name=rva ...", RVAs in hex (0x...) or decimal.
std::string parse_config(const std::string& request, std::map<std::string, uint64_t>& out);

// "apply" followed by lines "cas ITEM EXPECTED VALUE", "refresh", "popup ITEM SUFFIX" and
// "notice ITEM TEXT". A popup's suffix keeps only letters (made upper-case), digits, space and
// . , ' ! ? -, at most 40 characters; a notice's text keeps the same and :, at most 200, and is
// shown alone, with no icon or name (notice_text).
std::string parse_batch(const std::string& request, Batch& out);

// A `discs` request: "discs" (off: count 0, required 0, line "") or "discs COUNT REQUIRED HEX
// [TOLD]", the scanner's line in hex (scan.h), TOLD 1 (the default) or 0.
std::string parse_discs(const std::string& request, int& count, int& required, std::string& line,
                        bool& told);

// A `disk_slots` request: "disk_slots" (off: no slot) or "disk_slots COUNT REQUIRED SLOTS",
// SLOTS a comma list of slots 1 to 30, or "-": `counts` gets COUNT for each slot. REQUIRED
// (1 to 255) is checked, as the client sends it, but not kept: the file select shows the count
// alone (files.h).
std::string parse_disk_slots(const std::string& request, std::map<int, int>& counts);

}  // namespace zap
