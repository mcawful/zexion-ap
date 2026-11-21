// The ending's patch, which both builds make (feats_win.cpp, feats_linux.cpp): the stores and
// the jump around the join, as traced on B1 (docs/re/memory.md, "Feats and achievements").
#pragma once

#include <cstdint>
#include <cstring>

#include "game_mem.h"

namespace zap::feats_ending {

// The code around the ending's join, as traced on B1 (docs/re/memory.md, "Feats and
// achievements"): `call [rip+slot]` then `mov rdx, [rip+...]` at the call; `mov byte [rip+flag
// 251], 1` just before the join, `movsxd rdx, [rip+...]` at it, and `mov byte [rip+flag 250], 1`
// 0x11 bytes after it.
inline constexpr ptrdiff_t kMaxStretch = 0x1000;  // the join lies after the call, within the routine
inline constexpr BYTE kStoreByte[] = {0xC6, 0x05};  // mov byte [rip+rel32], imm8: 7 bytes
inline constexpr size_t kStoreSize = 7;
inline constexpr BYTE kJmp = 0xE9;

inline bool fits_rel32(const BYTE* from_next, const BYTE* to) {
    ptrdiff_t d = to - from_next;
    return d >= INT32_MIN && d <= INT32_MAX;
}

// Where `mov byte [rip+rel32], value` at `where` (7 bytes, read into `bytes`) stores, if it is one
// storing `value`; null otherwise.
inline const BYTE* store_target(const BYTE* where, const BYTE* bytes, BYTE value) {
    if (std::memcmp(bytes, kStoreByte, sizeof(kStoreByte)) != 0 || bytes[6] != value) {
        return nullptr;
    }
    return where + kStoreSize + field<int32_t>(bytes, 2);
}

}  // namespace zap::feats_ending
