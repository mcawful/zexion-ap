// The Area Map as an Intel Chip (the world's Area Map Hints option, on by default).
//
// With hints on, the world's Area Maps are Intel Chips, which the game holds as the Area Map
// (item 49). Its record (docs/re/memory.md, "Other players' items", for the records' fields)
// names it AREA MAP and runs area event 3551 at pickup, which reveals the map around it. While
// on (the client's `intel on`: this seed's save loaded), the helper names the record INTEL CHIP
// and sets its area event to none (-1), so the game's pickup popup says INTEL CHIP (seen), as
// would anything else that shows the record's name (the map cursor, from disassembly: not seen),
// and a pickup reveals nothing; the client's hint follows it.
// Only a record that reads as the game loads it (or as the helper leaves it) is changed, and it
// is put back when off and before an unload.
#pragma once

#include <windows.h>

#include <cstdint>

namespace zap {

constexpr int kAreaMapItem = 49;
extern const char kIntelName[];  // "INTEL CHIP"

class IntelChip {
public:
    void set(bool on) { on_ = on; }
    bool on() const { return on_; }

    // Game thread, each frame: renames the record while on, puts it back while off.
    // `records_holder` holds the pointer to the item records.
    void watch(BYTE* records_holder);
    // Game thread: the record back to the game's (off, before an unload).
    void restore(BYTE* records_holder);
    // Whether the record was the helper's when last looked at (an unload waits for a frame).
    bool restyled() const { return restyled_; }

private:
    bool on_ = false;
    bool restyled_ = false;
};

}  // namespace zap
