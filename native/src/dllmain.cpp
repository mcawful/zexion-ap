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
#include <set>
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

constexpr char kVersion[] = "0.18.5";
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
// Whether the scanner's message is still the helper's (scan.h): unload waits for it, since a
// later helper would not take it for the game's.
std::atomic<bool> g_scan_written{false};
// Likewise the pause screen's branch (pause.h), left as `jmp` the game would build its KEYS row.
std::atomic<bool> g_branch_patched{false};
SRWLOCK g_menu_lock = SRWLOCK_INIT;
bool g_unloading = false;  // guarded by g_menu_lock
std::atomic<int> g_in_callback{0};
// Set by the pipe thread when its client goes away (the AP client exited or crashed): the next
// frame unlocks ASSIST, before it serves any request, so a reconnected client's lock comes
// after it. A lock is only for while the client says its seed's save is loaded.
std::atomic<bool> g_unlock_assists{false};

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

// The game-mode panel's handler while ARCHIPELAGO is offered (menu.h): the game's own first,
// then ARCHIPELAGO's hover border fitted in the same frame. The game's widget code calls it,
// as ARCHIPELAGO's callback, on its thread outside the frame call, with the widget and the
// event (the game's handler reads no more); unload takes it off the panel with the callback.
void panel_event(void* widget, intptr_t event, intptr_t r8, intptr_t r9) {
    g_in_callback.fetch_add(1);
    using Handler = void (*)(void*, intptr_t, intptr_t, intptr_t);
    auto handler = reinterpret_cast<Handler>(g_game.panel_handler());
    if (handler != nullptr) handler(widget, event, r8, r9);
    try {
        g_game.fit_archipelago_border();
    } catch (...) {
        // Nothing may unwind into the game.
    }
    g_in_callback.fetch_sub(1);
}

// EQUIPMENT's callback while the disk count is shown (equip.h): the game's own builds the
// screen, then the count is added in the same call, before the screen's first frame. Like
// ARCHIPELAGO's, the game's widget code calls it on its thread outside the frame call; unload
// takes it off the pause menu first and waits for a call still inside it.
void equipment_chosen(void* widget, int event) {
    g_in_callback.fetch_add(1);
    auto game_fn = reinterpret_cast<void (*)(void*, int)>(g_game.equipment_fn());
    if (game_fn != nullptr) game_fn(widget, event);
    try {
        g_game.equipment_built();
    } catch (...) {
        // Nothing may unwind into the game.
    }
    g_in_callback.fetch_sub(1);
}

// The Energy Tank panel's handler while it shows the disk (equip.h): the game's own first, then
// the disk's name and description when the disk is hovered or selected. Called by the game's
// widget code on its thread outside the frame call; unload takes it off the panel first.
void equip_panel_event(void* widget, intptr_t event, intptr_t r8, intptr_t r9) {
    g_in_callback.fetch_add(1);
    using Handler = void (*)(void*, intptr_t, intptr_t, intptr_t);
    auto handler = reinterpret_cast<Handler>(g_game.equip_panel_fn());
    if (handler != nullptr) handler(widget, event, r8, r9);
    try {
        g_game.equipment_event(widget, event);
    } catch (...) {
        // Nothing may unwind into the game.
    }
    g_in_callback.fetch_sub(1);
}

// RETURN HOME WITH THE DISKS? (mission.h). The opener: the game runs it as its transition routine, on its
// thread outside the frame call; it builds the game's prompt and relabels it. YES: the game's
// NO (closing the prompt), then the ending once idle. Unload takes both off first.
void mission_open() {
    g_in_callback.fetch_add(1);
    try {
        g_game.mission_open();
    } catch (...) {
        // Nothing may unwind into the game.
    }
    g_in_callback.fetch_sub(1);
}

// The save entries' handler while the file select shows the disk count (files.h): the game's
// own first (it fills the entry on event 0xF), then the entry rewritten. Called by the game's
// widget code on its thread outside the frame call; unload puts the game's back first.
void file_entry_event(void* widget, intptr_t event, intptr_t r8, intptr_t r9) {
    g_in_callback.fetch_add(1);
    using Handler = void (*)(void*, intptr_t, intptr_t, intptr_t);
    auto handler = reinterpret_cast<Handler>(g_game.file_entry_original(widget));
    if (handler != nullptr) handler(widget, event, r8, r9);
    try {
        g_game.file_event(widget, event);
    } catch (...) {
        // Nothing may unwind into the game.
    }
    g_in_callback.fetch_sub(1);
}

void mission_yes(void* widget, int event) {
    g_in_callback.fetch_add(1);
    void* no = nullptr;
    try {
        no = g_game.mission_yes();
    } catch (...) {
    }
    if (no != nullptr) reinterpret_cast<void (*)(void*, int)>(no)(widget, event);
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
            g_game.note_play(GetTickCount64());
            if (g_unlock_assists.exchange(false)) {
                // The client went away: what it asked for that changes the game goes too.
                g_game.set_assists_locked(false);
                g_game.set_discs(0, 0, "");
                g_game.set_disk_slots(0, 0, {});
            }
            g_mailbox.serve();
            g_game.watch_newgame();
            if (TryAcquireSRWLockExclusive(&g_menu_lock)) {
                // Once an unload has begun, neither the choice nor the skip is put back in.
                if (!g_unloading) {
                    g_game.watch_menu(&archipelago_chosen);
                    g_game.watch_assists();
                    g_game.watch_skip();
                    g_game.watch_discs();
                    g_game.watch_equipment();
                    g_game.watch_mission();
                    g_game.watch_files();
                    g_game.watch_ap_items();
                }
                g_menu_installed.store(g_game.menu_installed());
                g_items_restyled.store(g_game.ap_items_restyled());
                g_scan_written.store(g_game.scan_written());
                g_branch_patched.store(g_game.keys_branch_patched());
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
    if (request == "shield") {
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        std::string result;
        if (!g_mailbox.call([] { return g_game.spawn_shield(GetTickCount64()); }, kFrameWaitMs,
                            result)) {
            return "refused no frame";
        }
        return result;
    }
    if (request == "die") {
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        std::string result;
        if (!g_mailbox.call([] { return g_game.kill(GetTickCount64()); }, kFrameWaitMs,
                            result)) {
            return "refused no frame";
        }
        return result;
    }
    if (request == "assists lock" || request == "assists free") {
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        bool lock = request == "assists lock";
        std::string result;
        if (!g_mailbox.call(
                [lock] {
                    if (!g_game.configured()) return std::string("refused not configured");
                    g_game.set_assists_locked(lock);
                    return std::string(lock ? "locked" : "free");
                },
                kFrameWaitMs, result)) {
            return "refused no frame";
        }
        return result;
    }
    if (request == "mission_prompt" || request == "mission_status") {
        // "mission_prompt": offer RETURN HOME WITH THE DISKS? when the game is next idle in play.
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        bool ask = request == "mission_prompt";
        std::string result;
        if (!g_mailbox.call(
                [ask] {
                    if (!g_game.configured()) return std::string("refused not configured");
                    if (ask) g_game.request_mission();
                    return ask ? std::string("queued") : g_game.mission_status();
                },
                kFrameWaitMs, result)) {
            return "refused no frame";
        }
        return result;
    }
    if (request == "disk_slots" || request.rfind("disk_slots ", 0) == 0) {
        // "disk_slots": off. "disk_slots COUNT REQUIRED SLOTS": the file select's disk count
        // for the slots holding the seed (files.h).
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        int count = 0, required = 0;
        std::set<int> slots;
        std::string error = zap::parse_disk_slots(request, count, required, slots);
        if (!error.empty()) return "error " + error;
        std::string result;
        if (!g_mailbox.call(
                [&] {
                    if (!g_game.configured()) return std::string("refused not configured");
                    bool on = !slots.empty();
                    g_game.set_disk_slots(count, required, std::move(slots));
                    return std::string(on ? "disk_slots on" : "disk_slots off");
                },
                kFrameWaitMs, result)) {
            return "refused no frame";
        }
        return result;
    }
    if (request == "discs_status") {
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        std::string result;
        if (!g_mailbox.call([] { return g_game.equipment_status(); }, kFrameWaitMs, result)) {
            return "refused no frame";
        }
        return result;
    }
    if (request == "discs" || request.rfind("discs ", 0) == 0) {
        // "discs": off. "discs COUNT REQUIRED HEX": the OS Install Disks goal's count and the
        // scanner's line, its characters in hex (scan.h).
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        int count = 0, required = 0;
        bool told = false;
        std::string line;
        std::string error = zap::parse_discs(request, count, required, line, told);
        if (!error.empty()) return "error " + error;
        std::string result;
        if (!g_mailbox.call(
                [&] {
                    if (!g_game.configured()) return std::string("refused not configured");
                    bool on = !line.empty();
                    g_game.set_discs(count, required, std::move(line), told);
                    return std::string(on ? "discs on" : "discs off");
                },
                kFrameWaitMs, result)) {
            return "refused no frame";
        }
        return result;
    }
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
                    g_game.set_panel_hook(reinterpret_cast<void*>(&panel_event));
                    g_game.set_equipment_hook(reinterpret_cast<void*>(&equipment_chosen));
                    g_game.set_equipment_panel_hook(reinterpret_cast<void*>(&equip_panel_event));
                    g_game.set_mission_hooks(reinterpret_cast<void*>(&mission_open),
                                             reinterpret_cast<void*>(&mission_yes));
                    g_game.set_file_entry_hook(reinterpret_cast<void*>(&file_entry_event));
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
    bool was_locked = false;  // ASSIST's lock, armed again should the unload be refused
    // The disk count and scanner line, likewise given back on a refusal.
    int disc_count = 0, discs_required = 0;
    bool discs_told = true;
    std::string scan_line;
    // And the file select's count.
    int file_count = 0, file_required = 0;
    std::set<int> file_slots;
    if (g_slot != nullptr) {
        std::string ignored;
        g_mailbox.call(
            [&] {
                was_locked = g_game.assists_locked();
                disc_count = g_game.disc_count();
                discs_required = g_game.discs_required();
                discs_told = g_game.discs_told();
                scan_line = g_game.scan_line();
                file_count = g_game.files().count();
                file_required = g_game.files().required();
                file_slots = g_game.files().slots();
                g_game.disarm_newgame();
                g_game.restore_skip();
                g_game.set_discs(0, 0, "");
                g_game.restore_discs();
                g_game.restore_mission();
                g_game.restore_files();
                g_game.restore_ap_items();
                g_game.restore_menu(&archipelago_chosen);
                g_game.set_assists_locked(false);
                g_game.restore_assists();
                g_menu_installed.store(g_game.menu_installed());
                g_items_restyled.store(g_game.ap_items_restyled());
                g_scan_written.store(g_game.scan_written());
                g_branch_patched.store(g_game.keys_branch_patched());
                return std::string();
            },
            kFrameWaitMs, ignored);
    }
    auto refuse = [&](const std::string& why) {
        set_unloading(false);
        if (was_locked || !scan_line.empty() || !file_slots.empty()) {
            std::string ignored;
            g_mailbox.call(
                [&] {
                    if (was_locked) g_game.set_assists_locked(true);
                    if (!scan_line.empty()) {
                        g_game.set_discs(disc_count, discs_required, scan_line, discs_told);
                    }
                    if (!file_slots.empty()) {
                        g_game.set_disk_slots(file_count, file_required, file_slots);
                    }
                    return std::string();
                },
                kFrameWaitMs, ignored);
        }
        return why;
    };
    if (set_unloading(true)) return refuse("a menu still holds the helper's callback (no frame came)");
    if (g_items_restyled.load()) return refuse("the stand-in item is still restyled (no frame came)");
    if (g_scan_written.load()) return refuse("the scanner's message is still the helper's (no frame came)");
    if (g_branch_patched.load()) return refuse("the pause screen's branch is still the helper's (no frame came)");
    if (!unhook()) return refuse(kHookReplaced);
    return "";
}

DWORD WINAPI helper_thread(void*) {
    try {
        hook();
        if (!zap::serve_pipe(zap::pipe_name(GetCurrentProcessId()), handle,
                             [] { g_unlock_assists.store(true); })) {
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
