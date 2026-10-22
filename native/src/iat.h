// Import-table hooks: the game calls SDL and FMOD through its import table, so replacing one
// slot there redirects every call the game makes to that function, with no byte patterns.
#pragma once

#include <windows.h>

namespace zap {

// The import-table slot through which `module` calls `function` from `dll` (matched without
// regard to case), or nullptr if the module does not import it by name.
void** find_import_slot(HMODULE module, const char* dll, const char* function);

// Atomically replaces the pointer in `slot` with `replacement` if it still holds `expected`.
// Returns true if it did.
bool swap_slot(void** slot, void* expected, void* replacement);

// A few bytes of code in memory that is never freed. Each call to `entry` counts itself in
// `*in_flight`, calls `*target` with the same register arguments (at most four, none on the
// stack) and returns its result. Pointing an import slot here instead of at the DLL makes
// unloading safe: put the slot back, set `*target` to the original function, and wait for
// `*in_flight` to reach 0; after that no thread can be in, or about to enter, the DLL's code.
// The memory is deliberately leaked, a page or two per load.
struct Trampoline {
    void* entry = nullptr;
    volatile LONG* in_flight = nullptr;
    void* volatile* target = nullptr;
};
bool make_trampoline(void* target, Trampoline& out);

}  // namespace zap
