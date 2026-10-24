// The scanner's message, reworded for the OS Install Disks goal.
//
// The first time the player enters Vallahar X6 Y8 in a game without all four keys, an object
// there shows SCANNING..., copies the key markers to the map, and 180 frames later shows Text
// entry 12, ENERGY KEYS DETECTED! (docs/re/memory.md, "The scanner"). In a seed with the OS
// Install Disks goal there are no keys, so no markers either; the helper rewrites that entry,
// in the loaded Text table only (nothing is saved), to say how many disks win (the client's
// line, "COLLECT 15 OS INSTALL DISKS!"). The entry is checked first, and put back on request,
// or before an unload, if it still reads as written. The game may load the table again; the
// watch then writes the new one.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace zap {

constexpr int kScanTextEntry = 12;
constexpr ptrdiff_t kTextEntrySize = 32;  // each entry: its text, then zeros
constexpr char kScanTextOriginal[] = "ENERGY KEYS DETECTED!";

class ScanText {
public:
    // Game thread. `table_holder` holds the pointer to the loaded Text table. Writes `line`
    // (at most 31 characters) over entry 12 if it reads as the game's own or as a line this
    // helper wrote; returns whether it reads `line` now.
    bool apply(BYTE* table_holder, const std::string& line);

    // Game thread. Puts the game's text back if the table is the one written and the entry
    // still reads as written; otherwise just forgets it.
    void restore(BYTE* table_holder);

    bool written() const { return entry_ != nullptr; }

private:
    BYTE* table_ = nullptr;
    BYTE* entry_ = nullptr;
    std::string line_;  // what entry_ was given
};

// Whether `line` can stand in the entry: 1 to 31 characters of the game's 8 x 8 font.
bool scan_line_ok(const std::string& line);

}  // namespace zap
