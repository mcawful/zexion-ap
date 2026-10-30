// The scanner's message (Text entry 12, ENERGY KEYS DETECTED!) rewritten as the client's OS
// Install Disks line, in the loaded Text table only (docs/re/memory.md, "The scanner"). Written
// only over the game's text or the helper's own; put back, if it still reads as written, when
// off and before an unload; a reloaded table is written again by the watch.
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
    // (must pass scan_line_ok) over entry 12 if it reads as the game's or the helper's last
    // line; returns whether it reads `line` now.
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
