// What the native tests share: check() and its failure count, unaligned field access, and the
// stand-in items' records as the game loads them. The tests write offsets as raw hex, so they
// cross-check src/game_layout.h rather than repeat it.
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

inline int g_failures = 0;

inline void check(bool ok, const char* what, const std::string& detail = "") {
    if (ok) return;
    if (detail.empty()) {
        std::printf("FAIL: %s\n", what);
    } else {
        std::printf("FAIL: %s %s\n", what, detail.c_str());
    }
    ++g_failures;
}

// A T in place at p + offset.
template <typename T>
T& at(BYTE* p, ptrdiff_t offset) {
    return *reinterpret_cast<T*>(p + offset);
}

// Unaligned reads and writes.
template <typename T>
T get(const BYTE* p, ptrdiff_t offset) {
    T v;
    std::memcpy(&v, p + offset, sizeof(v));
    return v;
}

template <typename T>
void put(BYTE* p, ptrdiff_t offset, T value) {
    std::memcpy(p + offset, &value, sizeof(value));
}

// A stand-in item's record (Scrap Energy, 47: frame 134, event 3549; Scrap Chunk, 48: frame
// 142, event 3550) as B1 loads it: kind 34, 6 ticks a frame, 2 frames, palette 8, inventory 46,
// pickup class 1 (docs/re/memory.md, "Other players' items").
inline void own_stand_in(BYTE* r, int16_t frame, const char* name, int16_t event) {
    std::memset(r, 0, 100);
    put<int16_t>(r, 0x00, 34);
    put<int16_t>(r, 0x02, frame);
    put<int16_t>(r, 0x04, 6);
    put<int16_t>(r, 0x06, 2);
    r[0x09] = 8;
    std::memcpy(r + 0x0A, name, std::strlen(name) + 1);
    put<int16_t>(r, 0x3C, 46);
    put<int16_t>(r, 0x3E, 45);
    put<int16_t>(r, 0x40, event);
    put<int16_t>(r, 0x58, 1);
    put<int16_t>(r, 0x5A, 13);
    put<int16_t>(r, 0x60, 244);
}
