// What the helper's shared sources take from the operating system's headers. On Windows that is
// <windows.h> itself; built for the game's Linux build (native/linux/, native/README.md, "The
// Linux helper") it is the few Windows type names the shared sources use, as fixed-size types.
#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdint>

using BYTE = uint8_t;
using WORD = uint16_t;
using DWORD = uint32_t;
#define WINAPI  // Windows' calling convention for its API: one convention on x86-64 Linux
#endif
