// The Area Map (item 49) as an Intel Chip, for seeds with Area Map Hints: while on (`intel on`),
// its record is named INTEL CHIP and its pickup event (3551, the map reveal) set to -1, so the
// pickup popup says INTEL CHIP and reveals nothing (docs/re/memory.md, "Intel Chips"). Only a
// record reading as the game's or the helper's is written; put back when off and before an
// unload.
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
