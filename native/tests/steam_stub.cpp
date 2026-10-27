// A stand-in steam_api64.dll for the feats tests, built into a folder of its own with them:
// SteamInternal_ContextInit as Steamworks' accessors use it (docs/re/memory.md, "Feats and
// achievements"), returning a context's interface pointer, here an interface that counts the
// calls to each vtable slot. The tests import it by name, as the game does, so the helper finds
// and hooks the import slot the same way.
#include <windows.h>

#include <utility>

namespace {

constexpr int kSlots = 64;
int g_calls[kSlots] = {};

struct Interface {
    void** vtable;
};

// Slot I's method: counts the call, and returns true as Steam's would.
template <int I>
bool method(Interface*, const char*) {
    ++g_calls[I];
    return true;
}

template <int... I>
void** table(std::integer_sequence<int, I...>) {
    static void* entries[kSlots] = {reinterpret_cast<void*>(&method<I>)...};
    return entries;
}

Interface g_interface{table(std::make_integer_sequence<int, kSlots>{})};

}  // namespace

// The context is {init, counter, interface}: the first call fills the interface (Steam's own
// calls the init function for it) and returns where it is.
extern "C" __declspec(dllexport) void* SteamInternal_ContextInit(void* context) {
    auto** c = static_cast<void**>(context);
    if (c[1] == nullptr) {
        c[2] = &g_interface;
        c[1] = reinterpret_cast<void*>(1);
    }
    return &c[2];
}

// The calls made to vtable slot `slot` (0x30 / 8 = 6, SetAchievement; 9, StoreStats).
extern "C" __declspec(dllexport) int stub_calls(int slot) {
    return slot >= 0 && slot < kSlots ? g_calls[slot] : -1;
}
