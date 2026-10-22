// zexion_ap.dll: the native helper the AP client loads into the running game.
//
// On load it starts one thread of its own. If the game is a build it knows, that thread points
// the game's import-table slot for SDL_GL_SwapWindow at a trampoline (iat.h), which calls the
// helper's frame function and then SDL. The game makes that call from its main loop
// (docs/re/memory.md, "The per-frame call"), so the frame function runs on the game's thread,
// between frames. The thread then serves the command pipe (pipe.h). "unload" points the slot
// and the trampoline back at SDL, waits until no call is inside the helper, and unloads the
// DLL, so a rebuilt helper can be loaded without restarting the game.
//
// Helper phase 1 only counts frames. Later phases run queued work in the frame function
// (grants, the game's popup), where it cannot race the game.
//
// The target is set at build time: the game's SDL_GL_SwapWindow, behind the build check, in
// zexion_ap.dll; this test program's own GetTickCount, with no build check, in the
// zexion_ap_hooktest.dll that native/tests/ load and unload under load.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "build.h"
#include "iat.h"
#include "pipe.h"

#ifdef ZAP_HOOKTEST
#define ZAP_TARGET_DLL "kernel32.dll"
#define ZAP_TARGET_DLL_W L"kernel32.dll"
#define ZAP_TARGET_FUNCTION "GetTickCount"
#define ZAP_CHECK_BUILD 0
#else
#define ZAP_TARGET_DLL "SDL2.dll"
#define ZAP_TARGET_DLL_W L"SDL2.dll"
#define ZAP_TARGET_FUNCTION "SDL_GL_SwapWindow"
#define ZAP_CHECK_BUILD 1
#endif

namespace {

constexpr char kVersion[] = "0.1.0";

// SDL_GL_SwapWindow(SDL_Window*) returns nothing; passing through whatever the original left in
// rax keeps the same function usable for the test target, which returns a value.
using Passthrough = uintptr_t (*)(void* argument);

HMODULE g_self = nullptr;
const char* g_build = nullptr;
void** g_slot = nullptr;  // set while hooked
void* g_original = nullptr;
zap::Trampoline g_trampoline;
std::atomic<uint64_t> g_frames{0};

uintptr_t frame(void* argument) {
    g_frames.fetch_add(1);
    return reinterpret_cast<Passthrough>(g_original)(argument);
}

void hook() {
    HMODULE exe = GetModuleHandleW(nullptr);
    g_build = zap::identify_build(exe);
    // The real helper hooks only a known build; the hook-test build never hooks the game.
    if (ZAP_CHECK_BUILD ? g_build == nullptr : g_build != nullptr) return;
    HMODULE target_dll = GetModuleHandleW(ZAP_TARGET_DLL_W);
    if (target_dll == nullptr) return;
    void* original = reinterpret_cast<void*>(GetProcAddress(target_dll, ZAP_TARGET_FUNCTION));
    void** slot = zap::find_import_slot(exe, ZAP_TARGET_DLL, ZAP_TARGET_FUNCTION);
    // Hook only a slot that still points at the real function, so nobody else's hook is lost.
    if (original == nullptr || slot == nullptr || *slot != original) return;
    g_original = original;
    if (!zap::make_trampoline(reinterpret_cast<void*>(&frame), g_trampoline)) return;
    if (zap::swap_slot(slot, original, g_trampoline.entry)) g_slot = slot;
}

// Puts the slot back. Returns false if something else has replaced it since; then the helper
// stays loaded, since its trampoline is still in the chain.
bool unhook() {
    if (g_slot == nullptr) return true;
    if (!zap::swap_slot(g_slot, g_trampoline.entry, g_original)) return false;
    g_slot = nullptr;
    // A call already in the trampoline either has counted itself (wait for it below) or has not
    // read the target yet (it now goes straight to the original).
    InterlockedExchangePointer(const_cast<void**>(g_trampoline.target), g_original);
    return true;
}

void wait_until_no_call_inside() {
    if (g_trampoline.in_flight == nullptr) return;
    while (InterlockedCompareExchange(g_trampoline.in_flight, 0, 0) != 0) Sleep(1);
}

std::string handle(const std::string& request, bool& stop) {
    if (request == "hello") {
        return std::string("zexion-ap ") + kVersion + " build=" + (g_build ? g_build : "unknown") +
               " hooked=" + (g_slot ? "1" : "0");
    }
    if (request == "ping") return "pong frames=" + std::to_string(g_frames.load());
    if (request == "unload") {
        if (!unhook()) return "error the frame hook was replaced by something else; not unloading";
        stop = true;
        return "bye";
    }
    return "error unknown request";
}

DWORD WINAPI helper_thread(void*) {
    try {
        hook();
        if (!zap::serve_pipe(zap::pipe_name(GetCurrentProcessId()), handle)) {
            // No pipe (the name was taken, no descriptor, or it broke): nothing could ever
            // unload us, so undo the hook now and leave.
            if (!unhook()) return 0;
        }
        wait_until_no_call_inside();
    } catch (...) {
        // An exception must not reach the game: stay loaded, hooked or not, and stop serving.
        return 0;
    }
    FreeLibraryAndExitThread(g_self, 0);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        HANDLE thread = CreateThread(nullptr, 0, helper_thread, nullptr, 0, nullptr);
        if (thread == nullptr) return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
