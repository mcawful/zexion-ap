// What the helper's shared sources take from the operating system's headers. On Windows that is
// <windows.h> itself; built for the game's Linux build (native/linux/, native/README.md, "The
// Linux helper") it is the few Windows type names the shared sources use, as fixed-size types.
#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else

using BYTE = uint8_t;
using WORD = uint16_t;
using DWORD = uint32_t;
using LONG = int32_t;
#define WINAPI  // Windows' calling convention for its API: one convention on x86-64 Linux

// The game's image: on Windows its module handle; on Linux there is one, the main binary, and a
// handle only stands for it (game_mem.h's in_image_code).
using HMODULE = const void*;
#endif

namespace zap {
// Milliseconds since some fixed point (GetTickCount64 on Windows; CLOCK_MONOTONIC on Linux).
uint64_t ticks_ms();
// Fresh zeroed read-write memory of `size` bytes the helper keeps for good, or null.
void* alloc_pages(size_t size);
}  // namespace zap
