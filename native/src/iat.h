// Import-table hooks: replacing one slot redirects every call the game makes to that import,
// with no byte patterns.
#pragma once

#include <windows.h>

namespace zap {

// The import-table slot through which `module` calls `function` from `dll` (matched without
// regard to case), or nullptr if the module does not import it by name.
void** find_import_slot(HMODULE module, const char* dll, const char* function);

// Atomically replaces `slot`'s pointer with `replacement` if it still holds `expected`; true if so.
bool swap_slot(void** slot, void* expected, void* replacement);

// A few bytes of code in memory never freed (two pages per load). Each call to `entry` counts
// itself in `*in_flight`, calls `*target` with the same register arguments (at most four, none
// on the stack) and returns its result. Once the slot is put back, `*target` is the original
// and `*in_flight` is 0, no thread can be in, or about to enter, the DLL through it (the game's
// calls into helper functions are counted apart: InCallback, dllmain.cpp).
struct Trampoline {
    void* entry = nullptr;
    volatile LONG* in_flight = nullptr;
    void* volatile* target = nullptr;
};
bool make_trampoline(void* target, Trampoline& out);

// At code + at, the rel32 from an instruction ending at `next` to `to` (the caller checks it fits).
void put_rel32(BYTE* code, size_t at, const BYTE* next, const BYTE* to);

}  // namespace zap
