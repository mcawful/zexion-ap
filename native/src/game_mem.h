// Reading and writing the game's memory: fault-safe reads, the write check, and the walks every
// module shares (the open menus, a sprite's frame). Layouts are in game_layout.h.
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game_layout.h"

namespace zap {

// Copies `size` bytes from `from` into `to`, or returns false if they are not committed,
// readable memory (or reading them faults).
bool safe_read(const void* from, void* to, size_t size);

// Whether `size` bytes at `at` are committed memory the game may write (no guard page).
bool writable(const void* at, size_t size);

// A T at base + offset, fault-safe; false for a null base or memory that cannot be read.
template <typename T>
bool read(const BYTE* base, ptrdiff_t offset, T& out) {
    return base != nullptr && safe_read(base + offset, &out, sizeof(T));
}

// Writes a T at base + offset, unaligned; the caller has checked the place.
template <typename T>
void put(BYTE* base, ptrdiff_t offset, T value) {
    std::memcpy(base + offset, &value, sizeof(value));
}

// A T at p + offset, unaligned and unchecked: for a copy, or memory known good.
template <typename T>
T field(const BYTE* p, ptrdiff_t offset) {
    T v;
    std::memcpy(&v, p + offset, sizeof(v));
    return v;
}
inline int16_t word(const BYTE* p, ptrdiff_t offset) { return field<int16_t>(p, offset); }

// A T in place at p + offset, unchecked: for memory the caller owns or has checked.
template <typename T>
T& at(BYTE* p, ptrdiff_t offset) {
    return *reinterpret_cast<T*>(p + offset);
}

// Whether `wanted` is still on the list of open menus, from [current_menu] down (menu::kBelow).
// The walk stops after kMaxMenus, far more than the game ever stacks.
constexpr int kMaxMenus = 64;
bool is_open(const BYTE* current_menu, const BYTE* wanted);

// A sprite's frame (sprite::kFrameIndex), or -1 if it cannot be read.
int32_t frame_of(const BYTE* sprite);
// Shows frame `frame` of a sprite's frame list (its index and its frame pointer), if the list
// has that many; false, with nothing written, otherwise.
bool set_frame(BYTE* sprite, int32_t frame);

}  // namespace zap
