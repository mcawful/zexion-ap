#include "skip.h"

#include <cstring>

#include "menu.h"  // safe_read, writable

namespace zap {

namespace {

constexpr uint8_t kFlagTest = 14;  // event type: if the flag is set go to +4, else to +6
constexpr int16_t kFlag = 250;
constexpr int16_t kNone = -1;
constexpr ptrdiff_t kType = 0, kTypeHigh = 1, kTestFlag = 2, kThen = 4, kElse = 6, kNext = 8;
constexpr ptrdiff_t kSelf = 0x24;

int16_t word(const BYTE* record, ptrdiff_t at) {
    int16_t v;
    std::memcpy(&v, record + at, sizeof(v));
    return v;
}

// The record as the game loads it (else = none), or as patched (else = the offer).
bool expected(const BYTE* r, int16_t else_branch) {
    return r[kType] == kFlagTest && r[kTypeHigh] == 0 && word(r, kTestFlag) == kFlag &&
           word(r, kThen) == kSkipOfferEvent && word(r, kElse) == else_branch &&
           word(r, kNext) == kNone && word(r, kSelf) == kSkipEvent;
}

}  // namespace

bool EscapeSkip::apply(BYTE* table_holder, BYTE* count) {
    BYTE* table = nullptr;
    int16_t events = 0;
    if (!safe_read(table_holder, &table, sizeof(table)) || table == nullptr ||
        !safe_read(count, &events, sizeof(events)) || events <= kSkipOfferEvent) {
        return patched();  // not loaded (yet): keep what is known
    }
    BYTE* record = table + kSkipEvent * kEventSize;
    BYTE copy[kEventSize];
    if (!safe_read(record, copy, sizeof(copy))) return false;
    if (expected(copy, kSkipOfferEvent)) {
        table_ = table;
        record_ = record;
        return true;  // already patched (by this helper before, or this frame)
    }
    if (!expected(copy, kNone) || !writable(record + kElse, sizeof(int16_t))) {
        table_ = nullptr;
        record_ = nullptr;  // not the record traced on B1: leave the game alone
        return false;
    }
    int16_t offer = kSkipOfferEvent;
    std::memcpy(record + kElse, &offer, sizeof(offer));
    table_ = table;
    record_ = record;
    return true;
}

void EscapeSkip::restore(BYTE* table_holder) {
    if (record_ == nullptr) return;
    BYTE* table = nullptr;
    BYTE copy[kEventSize];
    if (safe_read(table_holder, &table, sizeof(table)) && table == table_ &&
        safe_read(record_, copy, sizeof(copy)) && expected(copy, kSkipOfferEvent) &&
        writable(record_ + kElse, sizeof(int16_t))) {
        int16_t none = kNone;
        std::memcpy(record_ + kElse, &none, sizeof(none));
    }
    table_ = nullptr;
    record_ = nullptr;
}

}  // namespace zap
