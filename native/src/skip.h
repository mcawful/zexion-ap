// The escape skip, offered in every game while a client is connected (Game::active), and in a
// save dressed with no client (Game::offline_skip_, the offline look: offline.h).
//
// Area event 3665 runs at the grab: "if flag 250 (set only by the ending), go to 3668", which
// offers SKIP ESCAPE AND FINAL BATTLE? unless Abridged Finale (flag 289). A first playthrough
// would have to finish the escape with items the logic never requires, so the helper points the
// else branch at 3668 too, in the loaded table only (docs/re/memory.md, "The escape skip"). Put
// back, if it still reads as patched, when the client goes away, the offline look ends, or
// before an unload; a reloaded table is patched again by the watch.
#pragma once

#include <windows.h>

#include <cstdint>

namespace zap {

constexpr int kSkipEvent = 3665;
constexpr int kSkipOfferEvent = 3668;

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
