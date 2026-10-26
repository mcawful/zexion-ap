#include "intel.h"

#include <cstring>

#include "game_mem.h"

namespace zap {

const char kIntelName[] = "INTEL CHIP";

namespace {

// The Area Map's record (game_layout.h for its fields) as B1's Objects file holds it (read from
// the file, 2026-10-25).
constexpr int16_t kOwnKind = 34, kOwnFrame = 25, kOwnTicks = 6, kOwnFrames = 2;
constexpr uint8_t kOwnIcon = 0, kOwnPalette = 3;
constexpr int16_t kOwnInventory = 46, kOwnRedirect = 0, kOwnClass = 1;
constexpr int16_t kOwnEvent = 3551, kNoEvent = -1;  // 3551: the map reveal
constexpr char kOwnName[] = "AREA MAP";

// The record's fields the helper never changes, as the game loads them.
bool is_area_map(const BYTE* r) {
    return word(r, record::kKind) == kOwnKind && word(r, record::kFrame) == kOwnFrame &&
           word(r, record::kTicks) == kOwnTicks && word(r, record::kFrames) == kOwnFrames &&
           r[record::kIcon] == kOwnIcon &&
           r[record::kPalette] == kOwnPalette && word(r, record::kInventory) == kOwnInventory &&
           word(r, record::kRedirect) == kOwnRedirect && word(r, record::kPickupClass) == kOwnClass;
}

bool named(const BYTE* r, const char* name) {
    return std::memcmp(r + record::kName, name, std::strlen(name) + 1) == 0;
}

bool is_own(const BYTE* r) { return named(r, kOwnName) && word(r, record::kEvent) == kOwnEvent; }
bool is_ours(const BYTE* r) { return named(r, kIntelName) && word(r, record::kEvent) == kNoEvent; }

// A copy of record 49, checked readable and writable, and where it is; null if not.
BYTE* area_map(BYTE* holder, BYTE (&copy)[record::kSize]) {
    BYTE* records = nullptr;
    if (holder == nullptr || !safe_read(holder, &records, sizeof(records)) || records == nullptr) {
        return nullptr;
    }
    BYTE* r = records + kAreaMapItem * record::kSize;
    if (!safe_read(r, copy, sizeof(copy)) || !writable(r, record::kSize)) return nullptr;
    return r;
}

void set_name(BYTE* r, const char* name) {
    char field[record::kNameSize] = {};
    std::memcpy(field, name, std::strlen(name));
    std::memcpy(r + record::kName, field, sizeof(field));
}

void set_event(BYTE* r, int16_t event) { std::memcpy(r + record::kEvent, &event, sizeof(event)); }

}  // namespace

void IntelChip::watch(BYTE* holder) {
    if (!on_) {
        if (restyled_) restore(holder);
        return;
    }
    BYTE copy[record::kSize];
    BYTE* r = area_map(holder, copy);
    if (r == nullptr || !is_area_map(copy)) {
        restyled_ = false;  // not the record as traced: left alone
        return;
    }
    if (is_own(copy)) {
        set_name(r, kIntelName);
        set_event(r, kNoEvent);
        restyled_ = true;
    } else {
        restyled_ = is_ours(copy);  // ours already, or another name: left alone
    }
}

void IntelChip::restore(BYTE* holder) {
    BYTE copy[record::kSize];
    BYTE* r = area_map(holder, copy);
    // Back to the game's, whether this helper renamed it or an earlier one left it so.
    if (r != nullptr && is_area_map(copy) && is_ours(copy)) {
        set_name(r, kOwnName);
        set_event(r, kOwnEvent);
    }
    restyled_ = false;
}

}  // namespace zap
