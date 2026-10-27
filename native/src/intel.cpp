#include "intel.h"

#include <cstring>

#include "game_mem.h"

namespace zap {

const char kIntelName[] = "INTEL CHIP";

namespace {

// The Area Map's record (game_layout.h for its fields) as B1's Objects file holds it (read from
// the file, 2026-10-25): the fields the helper never changes, then its name and event.
constexpr RecordShape kAreaMap = {.kind = 34, .frame = 25, .ticks = 6, .frames = 2, .icon = 0,
                                  .palette = 3, .inventory = 46, .redirect = 0, .pickup_class = 1};
constexpr char kOwnName[] = "AREA MAP";
constexpr int16_t kOwnEvent = 3551, kNoEvent = -1;  // 3551: the map reveal

bool is_own(const BYTE* r) {
    return record_named(r, kOwnName) && word(r, record::kEvent) == kOwnEvent;
}
bool is_ours(const BYTE* r) {
    return record_named(r, kIntelName) && word(r, record::kEvent) == kNoEvent;
}

}  // namespace

void IntelChip::watch(BYTE* holder) {
    if (!on_) {
        if (restyled_) restore(holder);
        return;
    }
    BYTE copy[record::kSize];
    BYTE* r = writable_record(holder, kAreaMapItem, copy);
    if (r == nullptr || !matches(copy, kAreaMap)) {
        restyled_ = false;  // not the record as traced: left alone
        return;
    }
    if (is_own(copy)) {
        set_record_name(r, kIntelName);
        put(r, record::kEvent, kNoEvent);
        restyled_ = true;
    } else {
        restyled_ = is_ours(copy);  // ours already, or another name: left alone
    }
}

void IntelChip::restore(BYTE* holder) {
    BYTE copy[record::kSize];
    BYTE* r = writable_record(holder, kAreaMapItem, copy);
    // Back to the game's, whether this helper renamed it or an earlier one left it so.
    if (r != nullptr && matches(copy, kAreaMap) && is_ours(copy)) {
        set_record_name(r, kOwnName);
        put(r, record::kEvent, kOwnEvent);
    }
    restyled_ = false;
}

}  // namespace zap
