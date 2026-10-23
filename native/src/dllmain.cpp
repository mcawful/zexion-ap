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
// Each frame call counts itself, serves the mailbox (requests from the pipe thread that touch
// the game, mailbox.h), watches for a new game to write the AP seed into and offers ARCHIPELAGO
// on the game-mode panel (helper phase 3), and shows queued popups (helper phase 2,
// game_calls.h), where none of it can race the game.
//
// The target is set at build time: the game's SDL_GL_SwapWindow, behind the build check, in
// zexion_ap.dll; this test program's own GetTickCount, with no build check, in the
// zexion_ap_hooktest.dll that native/tests/ load and unload under load.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "build.h"
#include "game_calls.h"
#include "iat.h"
#include "mailbox.h"
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

constexpr char kVersion[] = "0.9.0";
constexpr DWORD kFrameWaitMs = 1000;  // how long a request waits for a frame to take it

// SDL_GL_SwapWindow(SDL_Window*) returns nothing; passing through whatever the original left in
// rax keeps the same function usable for the test target, which returns a value.
using Passthrough = uintptr_t (*)(void* argument);

HMODULE g_self = nullptr;
const char* g_build = nullptr;
void** g_slot = nullptr;  // set while hooked
void* g_original = nullptr;
zap::Trampoline g_trampoline;
std::atomic<uint64_t> g_frames{0};
zap::Mailbox g_mailbox;
zap::Game g_game;  // used on the game's thread only: frame() and archipelago_chosen()
std::atomic_flag g_in_frame_work = ATOMIC_FLAG_INIT;
// Whether the game's menu holds a pointer to archipelago_chosen, for the pipe thread's unload.
// The frame call offers ARCHIPELAGO only while holding g_menu_lock with g_unloading clear; it
// only tries the lock, so it never waits on the pipe thread, which holds it only briefly.
std::atomic<bool> g_menu_installed{false};
// Whether the stand-in item's record is restyled (apitem.h), which unload must undo first.
std::atomic<bool> g_items_restyled{false};
SRWLOCK g_menu_lock = SRWLOCK_INIT;
bool g_unloading = false;  // guarded by g_menu_lock
std::atomic<int> g_in_callback{0};

// ARCHIPELAGO's callback. The game's own widget code calls it, on the game's thread but outside
// the frame call (so outside the trampoline's count). Unload therefore first takes it off the
// menu on the game's thread, through the mailbox, refuses to unload while it may still be
// there, and waits for a call still inside it.
void archipelago_chosen(void*, int) {
    g_in_callback.fetch_add(1);
    try {
        g_game.start_archipelago(&archipelago_chosen);
    } catch (...) {
        // Nothing may unwind into the game.
    }
    g_menu_installed.store(g_game.menu_installed());
    g_in_callback.fetch_sub(1);
}

// Pipe thread: sets or clears g_unloading; returns whether ARCHIPELAGO is on the menu.
bool set_unloading(bool unloading) {
    AcquireSRWLockExclusive(&g_menu_lock);
    g_unloading = unloading;
    bool installed = g_menu_installed.load();
    ReleaseSRWLockExclusive(&g_menu_lock);
    return installed;
}

uintptr_t frame(void* argument) {
    g_frames.fetch_add(1);
    // The game calls this from its one main thread. Should two threads ever call it at once
    // (the hook-test target is called from several), only one does the work.
    if (!g_in_frame_work.test_and_set(std::memory_order_acquire)) {
        try {
            g_mailbox.serve();
            g_game.watch_newgame();
            if (TryAcquireSRWLockExclusive(&g_menu_lock)) {
                // Once an unload has begun, neither the choice nor the skip is put back in.
                if (!g_unloading) {
                    g_game.watch_menu(&archipelago_chosen);
                    g_game.watch_skip();
                    g_game.watch_ap_items();
                }
                g_menu_installed.store(g_game.menu_installed());
                g_items_restyled.store(g_game.ap_items_restyled());
                ReleaseSRWLockExclusive(&g_menu_lock);
            }
            g_game.pump_popups();
        } catch (...) {
            // Nothing may unwind into the game.
        }
        g_in_frame_work.clear(std::memory_order_release);
    }
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

constexpr char kHookReplaced[] = "the frame hook was replaced by something else";
std::string undo_for_unload();

std::string handle(const std::string& request, bool& stop) {
    if (request == "hello") {
        std::string state;  // configured, then whether the escape skip is offered
        if (g_slot != nullptr) {
            g_mailbox.call(
                [] {
                    return std::string(g_game.configured() ? "1" : "0") +
                           (g_game.skip_offered() ? "1" : "0");
                },
                kFrameWaitMs, state);
        }
        bool configured = state.size() == 2 && state[0] == '1';
        bool skip = state.size() == 2 && state[1] == '1';
        return std::string("zexion-ap ") + kVersion + " build=" + (g_build ? g_build : "unknown") +
               " hooked=" + (g_slot ? "1" : "0") + " configured=" + (configured ? "1" : "0") +
               " skip=" + (skip ? "1" : "0");
    }
    if (request == "ping") return "pong frames=" + std::to_string(g_frames.load());
    if (request.rfind("ap_labels", 0) == 0) {
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        std::map<int, zap::Label> labels;
        std::string error = zap::parse_labels(request, labels);
        if (!error.empty()) return "error " + error;
        std::string result;
        if (!g_mailbox.call(
                [&] {
                    if (!g_game.configured()) return std::string("refused not configured");
                    size_t n = labels.size();
                    g_game.set_labels(std::move(labels));
                    return "labeled " + std::to_string(n);
                },
                kFrameWaitMs, result)) {
            return "refused no frame";
        }
        return result;
    }
    if (request.rfind("ap_newgame", 0) == 0 || request == "disarm" ||
        request == "newgame_status") {
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        std::string result;
        if (request == "disarm") {
            if (!g_mailbox.call([] { g_game.disarm_newgame(); return std::string("disarmed"); },
                                kFrameWaitMs, result)) {
                return "refused no frame";
            }
            return result;
        }
        if (request == "newgame_status") {
            if (!g_mailbox.call([] { return g_game.newgame_status(); }, kFrameWaitMs, result)) {
                return "refused no frame";
            }
            return result;
        }
        zap::NewGamePlan plan;
        std::string error = zap::parse_newgame(request, plan);
        if (!error.empty()) return "error " + error;
        if (!g_mailbox.call(
                [&] {
                    if (!g_game.configured()) return std::string("refused not configured");
                    g_game.arm_newgame(std::move(plan));
                    return std::string("armed");
                },
                kFrameWaitMs, result)) {
            return "refused no frame";
        }
        return result;
    }
    if (request.rfind("config", 0) == 0 || request.rfind("apply", 0) == 0) {
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        std::string result;
        if (request.rfind("config", 0) == 0) {
            std::map<std::string, uint64_t> rvas;
            std::string error = zap::parse_config(request, rvas);
            if (!error.empty()) return "error " + error;
            HMODULE exe = GetModuleHandleW(nullptr);
            if (!g_mailbox.call([&] {
                    std::string e = g_game.configure(exe, rvas);
                    return e.empty() ? std::string("ok") : "error " + e;
                }, kFrameWaitMs, result)) {
                return "refused no frame";
            }
            return result;
        }
        zap::Batch batch;
        std::string error = zap::parse_batch(request, batch);
        if (!error.empty()) return "error " + error;
        if (!g_mailbox.call([&] { return g_game.apply(batch); }, kFrameWaitMs, result)) {
            return "refused no frame";  // withdrawn: it will never be applied
        }
        return result;
    }
    if (request == "unload") {
        std::string why = undo_for_unload();
        if (!why.empty()) return "error " + why + "; not unloading";
        stop = true;
        return "bye";
    }
    return "error unknown request";
}

// Puts back everything the helper changed in the game, then unhooks: "" or why not, with the
// helper then still loaded and working.
std::string undo_for_unload() {
    // From here no frame offers ARCHIPELAGO again; then one frame takes it off the menu.
    set_unloading(true);
    if (g_slot != nullptr) {
        std::string ignored;
        g_mailbox.call(
            [] {
                g_game.disarm_newgame();
                g_game.restore_skip();
                g_game.restore_ap_items();
                g_game.restore_menu(&archipelago_chosen);
                g_menu_installed.store(g_game.menu_installed());
                g_items_restyled.store(g_game.ap_items_restyled());
                return std::string();
            },
            kFrameWaitMs, ignored);
    }
    if (set_unloading(true)) {
        set_unloading(false);
        return "ARCHIPELAGO is still on the game's menu (no frame came)";
    }
    if (g_items_restyled.load()) {
        set_unloading(false);
        return "the stand-in item is still restyled (no frame came)";
    }
    if (!unhook()) {
        set_unloading(false);
        return kHookReplaced;
    }
    return "";
}

DWORD WINAPI helper_thread(void*) {
    try {
        hook();
        if (!zap::serve_pipe(zap::pipe_name(GetCurrentProcessId()), handle)) {
            // No pipe (the name was taken, no descriptor, or it broke): nothing could ever
            // unload us, so put the game back as unload does and leave, retrying until a frame
            // comes to do it; if the hook was replaced, stay.
            for (;;) {
                std::string why = undo_for_unload();
                if (why.empty()) break;
                if (why == kHookReplaced) return 0;
                Sleep(1000);
            }
        }
        wait_until_no_call_inside();
        while (g_in_callback.load() != 0) Sleep(1);
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
