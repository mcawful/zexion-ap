#include "skip.h"

#include <cstring>

#include "game_mem.h"

namespace zap {

namespace {

constexpr uint8_t kFlagTest = 14;  // event type: if the flag at +2 is set go to +4, else to +6
constexpr int16_t kNone = -1;
// A flag test's arguments (game_layout.h, event::).
constexpr ptrdiff_t kTestFlag = event::kArg0, kThen = event::kArg1, kElse = event::kArg2,
                    kNext = event::kArg3;

// The record as the game loads it (else = none), or as patched (else = the offer).
bool expected(const BYTE* r, int16_t else_branch) {
    return r[event::kType] == kFlagTest && r[event::kTypeHigh] == 0 &&
           word(r, kTestFlag) == flag::kEnding && word(r, kThen) == kSkipOfferEvent &&
           word(r, kElse) == else_branch && word(r, kNext) == kNone &&
           word(r, event::kSelf) == kSkipEvent;
}

}  // namespace

bool EscapeSkip::apply(BYTE* table_holder, BYTE* count) {
    BYTE* table = pointer_at(table_holder);
    int16_t events = 0;
    if (table == nullptr || !read(count, 0, events) || events <= kSkipOfferEvent) {
        return patched();  // not loaded (yet): keep what is known
    }
    BYTE* record = table + kSkipEvent * event::kSize;
    BYTE copy[event::kSize];
    if (!safe_read(record, copy, sizeof(copy))) return false;
    if (expected(copy, kSkipOfferEvent)) {
        table_ = table;
        record_ = record;
        return true;  // already patched
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
    BYTE copy[event::kSize];
    if (pointer_at(table_holder) == table_ && safe_read(record_, copy, sizeof(copy)) &&
        expected(copy, kSkipOfferEvent) &&
        writable(record_ + kElse, sizeof(int16_t))) {
        int16_t none = kNone;
        std::memcpy(record_ + kElse, &none, sizeof(none));
    }
    table_ = nullptr;
    record_ = nullptr;
}

}  // namespace zap
