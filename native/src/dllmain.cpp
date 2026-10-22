// zexion_ap.dll: the native helper the AP client loads into the running game.
//
// On load it starts one thread of its own. If the game is a build it knows, that thread replaces
// the game's import-table slot for SDL_GL_SwapWindow, which the game calls once per frame on its
// own thread, so the helper gets a call between frames. It then serves the command pipe
// (pipe.h). "unload" puts the original slot back, waits until no frame is inside the hook, and
// unloads the DLL, so a rebuilt helper can be loaded without restarting the game.
//
// Phase 1 only counts frames. Later phases run queued work in the frame call (grants, the
// game's popup), where it cannot race the game.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "build.h"
#include "iat.h"
#include "pipe.h"

namespace {

constexpr char kVersion[] = "0.1.0";

using SwapWindow = void (*)(void* window);  // SDL2: void SDL_GL_SwapWindow(SDL_Window*)

HMODULE g_self = nullptr;
const char* g_build = nullptr;
void** g_slot = nullptr;  // set while hooked
SwapWindow g_original = nullptr;
std::atomic<uint64_t> g_frames{0};
std::atomic<int> g_in_hook{0};

void swap_window_hook(void* window) {
    g_in_hook.fetch_add(1);
    g_frames.fetch_add(1);
    g_original(window);
    g_in_hook.fetch_sub(1);
}

void hook() {
    HMODULE exe = GetModuleHandleW(nullptr);
    g_build = zap::identify_build(exe);
    if (g_build == nullptr) return;
    HMODULE sdl = GetModuleHandleW(L"SDL2.dll");
    if (sdl == nullptr) return;
    auto* original = reinterpret_cast<void*>(GetProcAddress(sdl, "SDL_GL_SwapWindow"));
    void** slot = zap::find_import_slot(exe, "SDL2.dll", "SDL_GL_SwapWindow");
    // Hook only a slot that still points at SDL itself, so nobody else's hook is overwritten.
    if (original == nullptr || slot == nullptr || *slot != original) return;
    g_original = reinterpret_cast<SwapWindow>(original);
    if (zap::swap_slot(slot, original, reinterpret_cast<void*>(&swap_window_hook))) {
        g_slot = slot;
    }
}

bool unhook() {
    if (g_slot == nullptr) return true;
    if (!zap::swap_slot(g_slot, reinterpret_cast<void*>(&swap_window_hook),
                        reinterpret_cast<void*>(g_original))) {
        return false;  // something else replaced the slot after us; leaving it is safer
    }
    g_slot = nullptr;
    return true;
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
    hook();
    zap::serve_pipe(zap::pipe_name(GetCurrentProcessId()), handle);
    if (g_slot != nullptr) {
        // The pipe could not be created: stay loaded and hooked, since nothing can unload us.
        return 0;
    }
    // A frame that entered the hook before the slot was restored may still be in it.
    while (g_in_hook.load() != 0) Sleep(1);
    Sleep(100);  // and let it return past our last instructions
    FreeLibraryAndExitThread(g_self, 0);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, helper_thread, nullptr, 0, nullptr);
        if (thread == nullptr) return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
