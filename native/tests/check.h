// What the native tests share: check() and its failure count, report() at the end of main,
// unaligned field access, code bytes, hex, the game's sprite-sheet function and the stand-in
// items' records as the game loads them. The tests write offsets as raw hex, so they
// cross-check src/game_layout.h rather than repeat it.
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

// main's result: says so if every check passed (`name`: the tests'), and the exit code.
inline int report(const char* name) {
    if (g_failures == 0) std::printf("%s: all passed\n", name);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
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

// Byte `i` of code the helper patches, read and written through volatile (the compiler would
// otherwise take the array's initial bytes as constants), the page made writable as the
// helper makes it.
inline unsigned char code_byte(const unsigned char* code, int i) {
    return static_cast<const volatile unsigned char*>(code)[i];
}
inline void set_code_byte(unsigned char* code, int i, unsigned char value) {
    DWORD old = 0;
    VirtualProtect(code + i, 1, PAGE_EXECUTE_READWRITE, &old);
    static_cast<volatile unsigned char*>(code)[i] = value;
    VirtualProtect(code + i, 1, old, &old);
}

// Bytes as hex digits (upper-case; the helper's parsers take either case).
inline std::string hex(const void* bytes, size_t n) {
    static const char* d = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        const auto b = static_cast<const uint8_t*>(bytes)[i];
        out += d[b >> 4];
        out += d[b & 15];
    }
    return out;
}
inline std::string hex(const std::string& text) { return hex(text.data(), text.size()); }

// The game's sprite-sheet function (0x74780; object, template): the template's frames.
inline void sprite_sheet(void* o, void* t) {
    put(static_cast<BYTE*>(o), 0x1BC, get<int32_t>(static_cast<BYTE*>(t), 0x1BC));
    put(static_cast<BYTE*>(o), 0x1A8, get<BYTE*>(static_cast<BYTE*>(t), 0x1A8));
}

// A stand-in item's record (Scrap Energy, 47: frame 134, event 3549; Scrap Chunk, 48: frame
// 142, event 3550) as B1 loads it: kind 34, 6 ticks a frame, 2 frames, palette 8, inventory 46,
// +0x3E 45, pickup class 1, sound 13, Database 244 (docs/re/memory.md, "Other players' items").
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
// Both stand-ins' records in a table of 51 as B1 loads them.
inline void own_stand_ins(BYTE* records) {
    own_stand_in(records + 47 * 100, 134, "SCRAP ENERGY", 3549);
    own_stand_in(records + 48 * 100, 142, "SCRAP CHUNK", 3550);
}
