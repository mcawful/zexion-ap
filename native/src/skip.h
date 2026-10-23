// The escape skip, offered in every game while the helper is loaded.
//
// After Zexion is grabbed, the game runs area event 3665: "if flag 250 is set, go to 3668",
// and 3668 offers SKIP ESCAPE AND FINAL BATTLE? unless flag 289 (Abridged Finale) is set.
// Flag 250 is set only by the ending, so a first playthrough never gets the offer and must
// finish the escape, which needs the Zenith Thrusters (docs/re/memory.md, "The escape skip").
// The helper points the record's other branch at 3668 too, in the loaded event table only
// (nothing is saved), so the offer comes whether or not flag 250 is set. Every field of the
// record is checked first, and the record is put back on unload if it still reads as patched.
// The game may load the table again; the watch then patches the new one.
#pragma once

#include <windows.h>

#include <cstdint>

namespace zap {

constexpr int kSkipEvent = 3665;
constexpr int kSkipOfferEvent = 3668;
constexpr ptrdiff_t kEventSize = 0x28;

class EscapeSkip {
public:
    // Game thread. `table_holder` holds the pointer to the loaded events, `count` their number
    // (16-bit). Patches event 3665 if it reads as the game's own; returns whether it is patched
    // now.
    bool apply(BYTE* table_holder, BYTE* count);

    // Game thread. Puts the record back if the table is the one patched and the record still
    // reads as patched; otherwise just forgets it.
    void restore(BYTE* table_holder);

    bool patched() const { return record_ != nullptr; }

private:
    BYTE* table_ = nullptr;
    BYTE* record_ = nullptr;
};

}  // namespace zap
