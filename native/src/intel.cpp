#include "intel.h"

#include <cstring>

#include "menu.h"  // safe_read, writable

namespace zap {

const char kIntelName[] = "INTEL CHIP";

namespace {

// The record's fields (docs/re/memory.md, "Other players' items") and the Area Map's values
// as B1's Objects file holds them (read from the file, 2026-10-25).
constexpr ptrdiff_t kRecordSize = 100;
constexpr ptrdiff_t kKind = 0x00, kFrame = 0x02, kTicks = 0x04, kFrames = 0x06, kIcon = 0x08;
constexpr ptrdiff_t kPalette = 0x09, kName = 0x0A, kInventory = 0x3C, kEvent = 0x40;
constexpr ptrdiff_t kRedirect = 0x54, kPickupClass = 0x58;
constexpr size_t kNameSize = 50;
constexpr int16_t kOwnKind = 34, kOwnFrame = 25, kOwnTicks = 6, kOwnFrames = 2;
constexpr uint8_t kOwnIcon = 0, kOwnPalette = 3;
constexpr int16_t kOwnInventory = 46, kOwnRedirect = 0, kOwnClass = 1;
constexpr int16_t kOwnEvent = 3551, kNoEvent = -1;  // 3551: the map reveal
constexpr char kOwnName[] = "AREA MAP";

int16_t word(const BYTE* r, ptrdiff_t o) {
    int16_t v;
    std::memcpy(&v, r + o, sizeof(v));
    return v;
}

// The record's fields the helper never changes, as the game loads them.
bool is_area_map(const BYTE* r) {
    return word(r, kKind) == kOwnKind && word(r, kFrame) == kOwnFrame &&
           word(r, kTicks) == kOwnTicks && word(r, kFrames) == kOwnFrames && r[kIcon] == kOwnIcon &&
           r[kPalette] == kOwnPalette && word(r, kInventory) == kOwnInventory &&
           word(r, kRedirect) == kOwnRedirect && word(r, kPickupClass) == kOwnClass;
}

bool named(const BYTE* r, const char* name) {
    return std::memcmp(r + kName, name, std::strlen(name) + 1) == 0;
}

bool is_own(const BYTE* r) { return named(r, kOwnName) && word(r, kEvent) == kOwnEvent; }
bool is_ours(const BYTE* r) { return named(r, kIntelName) && word(r, kEvent) == kNoEvent; }

// A copy of record 49, checked readable and writable, and where it is; null if not.
BYTE* area_map(BYTE* holder, BYTE (&copy)[kRecordSize]) {
    BYTE* records = nullptr;
    if (holder == nullptr || !safe_read(holder, &records, sizeof(records)) || records == nullptr) {
        return nullptr;
    }
    BYTE* r = records + kAreaMapItem * kRecordSize;
    if (!safe_read(r, copy, sizeof(copy)) || !writable(r, kRecordSize)) return nullptr;
    return r;
}

void set_name(BYTE* r, const char* name) {
    char field[kNameSize] = {};
    std::memcpy(field, name, std::strlen(name));
    std::memcpy(r + kName, field, sizeof(field));
}

void set_event(BYTE* r, int16_t event) { std::memcpy(r + kEvent, &event, sizeof(event)); }

}  // namespace

void IntelChip::watch(BYTE* holder) {
    if (!on_) {
        if (restyled_) restore(holder);
        return;
    }
    BYTE copy[kRecordSize];
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
    BYTE copy[kRecordSize];
    BYTE* r = area_map(holder, copy);
    // Back to the game's, whether this helper renamed it or an earlier one left it so.
    if (r != nullptr && is_area_map(copy) && is_ours(copy)) {
        set_name(r, kOwnName);
        set_event(r, kOwnEvent);
    }
    restyled_ = false;
}

}  // namespace zap
