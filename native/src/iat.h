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

}  // namespace zap
