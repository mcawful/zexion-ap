// zexion_ap.dll: one thread hooks the game's SDL_GL_SwapWindow import through a trampoline
// (iat.h), in a known build only (build.h), then serves the pipe (pipe.h), so frame() runs on the
// game's thread between frames (native/README.md, "Lifecycle"; docs/re/memory.md, "The per-frame
// call: the game's SDL_GL_SwapWindow import"). The hook-test build (zexion_ap_hooktest.dll) hooks
// the host's GetTickCount instead, in any but a known build.

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "build.h"
#include "findaddr.h"
#include "game_calls.h"
#include "game_requests.h"
#include "host_offline.h"
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

// The folders of the seed files (host_offline.h). The hook-test build is a host program's, never
// the game's, and takes every folder it writes through restrict_to_test_folder: inside the
// temporary folder, never the real ones. Its save folder is only what a test names in
// ZAP_TEST_SAVE_DIR (not ZEXION_SAVE_DIR, which the repo's tools define as the real one), its own
// folder only what LOCALAPPDATA names if that is such a folder: else none, and then nothing is
// read or written. The release has none of that: with no known build it has no save folder.
#ifdef ZAP_HOOKTEST
std::wstring host_offline_folder() { return zap::restrict_to_test_folder(zap::offline_folder()); }

std::wstring host_save_folder(const char*) {
    wchar_t folder[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ZAP_TEST_SAVE_DIR", folder, MAX_PATH);
    return n != 0 && n < MAX_PATH ? zap::restrict_to_test_folder(std::wstring(folder, n))
                                  : std::wstring();
}
#else
std::wstring host_offline_folder() { return zap::offline_folder(); }

std::wstring host_save_folder(const char* build) {
    return build == nullptr ? std::wstring() : zap::save_folder();
}
#endif

constexpr char kVersion[] = "1.2.0";
constexpr DWORD kFrameWaitMs = 1000;  // how long a request waits for a frame to take it

// Passes on the original's rax: the test target returns a value, SDL_GL_SwapWindow none.
using Passthrough = uintptr_t (*)(void* argument);

HMODULE g_self = nullptr;
const char* g_build = nullptr;
void** g_slot = nullptr;  // set while hooked
void* g_original = nullptr;
zap::Trampoline g_trampoline;
std::atomic<uint64_t> g_frames{0};
zap::Mailbox g_mailbox;
// Game thread only: frame(), the mailbox work, and the functions the game calls below.
zap::Game g_game;
std::atomic_flag g_in_frame_work = ATOMIC_FLAG_INIT;
// What an unload must wait for (Game::unload_blocker), or null: unload refuses while set. The
// frame offers ARCHIPELAGO only under g_menu_lock with g_unloading clear, and only tries the
// lock, so it never waits on the pipe thread.
std::atomic<const char*> g_blocker{nullptr};
SRWLOCK g_menu_lock = SRWLOCK_INIT;
bool g_unloading = false;  // guarded by g_menu_lock
std::atomic<int> g_in_callback{0};
// Set by the pipe thread on a disconnect; the next frame puts everything back before any request.
std::atomic<bool> g_client_gone{false};
// Loaded from the game's own folder (by the proxy, or a client with no build): `hello` reports it.
bool g_from_game_folder = false;
// The seed files, the helper's own scan and the thread that copies the seeds (host_offline.h).
zap::HostOffline g_host{g_game};

// Set by any request that sets something a client's going must put back (requests that
// zap::sets_client_state names: `config`, `offline_put`, ...), taken when the connection goes: a
// client that set nothing, `helper.py status` or a hello, leaves the game and the files as they
// are (no put back, no copy written for it).
std::atomic<bool> g_client_set{false};

// Counts a call from the game into one of the helper's functions outside the frame call (the
// trampoline counts those inside): helper_thread waits for none before it unloads.
class InCallback {
public:
    InCallback() { g_in_callback.fetch_add(1); }
    ~InCallback() { g_in_callback.fetch_sub(1); }
    InCallback(const InCallback&) = delete;
    InCallback& operator=(const InCallback&) = delete;
};

// Runs the helper's part of a call from the game, or its thread's start-up work (the scan, the
// files), and nothing may unwind: into the game, or past the pipe's start.
template <typename Work>
void guarded(Work&& work) {
    try {
        work();
    } catch (...) {
    }
}

// Game thread: what an unload must wait for, published for the pipe thread.
void publish_blocker() { g_blocker.store(g_game.unload_blocker()); }

// ARCHIPELAGO's callback, on the game's thread but outside the trampoline's count, so InCallback
// counts it, and unload first takes it off the menu (via the mailbox) and refuses while it may
// remain. The other game-called functions below follow the same rule.
void archipelago_chosen(void*, int) {
    InCallback in;
    guarded([] { g_game.start_archipelago(&archipelago_chosen); });
    publish_blocker();
}

// A handler in place of the game's: the game's own (`Original`, null for none), then the
// helper's part (`After`), with the arguments the game's widget code passes.
using Handler = void (*)(void*, intptr_t, intptr_t, intptr_t);
template <void* (*Original)(void* widget), void (*After)(void* widget, intptr_t event)>
void handler_event(void* widget, intptr_t event, intptr_t r8, intptr_t r9) {
    InCallback in;
    auto handler = reinterpret_cast<Handler>(Original(widget));
    if (handler != nullptr) handler(widget, event, r8, r9);
    guarded([&] { After(widget, event); });
}

// The game-mode panel while ARCHIPELAGO is offered (menu.h): its hover border fitted.
void* panel_original(void*) { return g_game.panel_handler(); }
void panel_after(void*, intptr_t) { g_game.fit_archipelago_border(); }

// The Energy Tank panel while it shows the disk (equip.h): the disk's name and description.
void* equip_panel_original(void*) { return g_game.equip_panel_fn(); }
void equip_panel_after(void* widget, intptr_t event) { g_game.equipment_event(widget, event); }

// The file select's save entries (files.h): each rewritten after the game fills it (event 0xF).
void* file_entry_original(void* widget) { return g_game.file_entry_original(widget); }
void file_entry_after(void* widget, intptr_t event) { g_game.file_event(widget, event); }

// The map's per-frame handler (mapmarks.h): the cursor's name rewritten after the game's.
void* map_original(void* child) { return g_game.map_original(child); }
void map_after(void*, intptr_t) { g_game.map_after(); }

// EQUIPMENT's callback with the disk goal on (equip.h): the game's screen, then the disk widget.
void equipment_chosen(void* widget, int event) {
    InCallback in;
    auto game_fn = reinterpret_cast<void (*)(void*, int)>(g_game.equipment_fn());
    if (game_fn != nullptr) game_fn(widget, event);
    guarded([] { g_game.equipment_built(); });
}

// The pause menu's way back from OPTION MENU (pause.h): the game's, then the disk row turned.
void options_back(void* menu) {
    InCallback in;
    auto game_fn = reinterpret_cast<void (*)(void*)>(g_game.options_back_fn());
    if (game_fn != nullptr) game_fn(menu);
    guarded([&] { g_game.options_returned(menu); });
}

// RETURN HOME WITH THE DISKS? (mission.h): the opener and the ending run as the game's transition
// routine; YES runs the game's NO (closing the prompt), then the helper's part.
void mission_open() {
    InCallback in;
    guarded([] { g_game.mission_open(); });
}

void mission_yes(void* widget, int event) {
    InCallback in;
    void* no = nullptr;
    guarded([&] { no = g_game.mission_yes(); });
    if (no == nullptr) return;
    reinterpret_cast<void (*)(void*, int)>(no)(widget, event);
    guarded([] { g_game.mission_after_no(); });
}

void mission_end() {
    InCallback in;
    guarded([] { g_game.mission_end(); });
}

// The helper's wrappers for the game's menus, given to Game once, before the hook.
void set_hooks() {
    zap::Hooks hooks;
    hooks.panel = reinterpret_cast<void*>(&handler_event<&panel_original, &panel_after>);
    hooks.equipment = reinterpret_cast<void*>(&equipment_chosen);
    hooks.equipment_panel =
        reinterpret_cast<void*>(&handler_event<&equip_panel_original, &equip_panel_after>);
    hooks.mission_open = reinterpret_cast<void*>(&mission_open);
    hooks.mission_yes = reinterpret_cast<void*>(&mission_yes);
    hooks.mission_end = reinterpret_cast<void*>(&mission_end);
    hooks.file_entry =
        reinterpret_cast<void*>(&handler_event<&file_entry_original, &file_entry_after>);
    hooks.map = reinterpret_cast<void*>(&handler_event<&map_original, &map_after>);
    hooks.options_back = reinterpret_cast<void*>(&options_back);
    g_game.set_hooks(hooks);
}

// How long an unload waits for the copy thread. The reply comes after this and, once it has
// stopped, the wait for a frame in undo_for_unload (1 s), and the client waits for it 3 s in
// `Helper.unload` (APPLY_SECONDS) but only 2 s in `_unload_refusal` and tools/helper.py
// (REPLY_SECONDS): 0.5 s and 1 s stay under 2 s, so a refusal or a "bye" is never taken for a lost
// reply. A stuck thread is refused at once after this wait (no frame wait). An idle thread stops
// at once (its event).
constexpr DWORD kCopyStopMs = 500;

// Pipe thread: sets or clears g_unloading; returns what an unload must wait for, read under the
// lock (so not before a frame that offered ARCHIPELAGO has published it), or null.
const char* set_unloading(bool unloading) {
    AcquireSRWLockExclusive(&g_menu_lock);
    g_unloading = unloading;
    const char* blocker = g_blocker.load();
    ReleaseSRWLockExclusive(&g_menu_lock);
    return blocker;
}

uintptr_t frame(void* argument) {
    g_frames.fetch_add(1);
    // The game calls this from its one main thread. Should two threads ever call it at once
    // (the hook-test target is called from several), only one does the work.
    if (!g_in_frame_work.test_and_set(std::memory_order_acquire)) {
        try {
            g_game.note_play(GetTickCount64());
            if (g_client_gone.exchange(false)) {
                // Only the offline look dresses a save until a client configures again.
                g_game.client_gone(&archipelago_chosen);
            }
            g_host.take();
            guarded([] { g_host.snapshot_saves(); });  // a throw here skips no other frame work
            g_mailbox.serve();
            if (TryAcquireSRWLockExclusive(&g_menu_lock)) {
                // No watch once an unload has begun: what the client asked for stays set, and a
                // refused unload's next frame applies it again. Guarded: a throw that kept the
                // lock would make the next unload wait on it for good.
                if (!g_unloading) {
                    guarded([] { g_game.watch_newgame(); });
                    // Each watch guarded inside: one that throws keeps none of the others from
                    // running (Game::watch_all).
                    guarded([] { g_game.watch_all(&archipelago_chosen); });
                    // Traps patch the game's code as they start (trap.h): never while an unload
                    // runs (they wait, and are lost with the helper, as popups are). Guarded on
                    // its own: a received trap's popup is shown here, and the popups behind it
                    // wait for it, so a watch that throws must not keep it from running.
                    guarded([] { g_game.pump_traps(GetTickCount64()); });
                }
                publish_blocker();
                ReleaseSRWLockExclusive(&g_menu_lock);
            }
            g_game.pump_popups();
            g_game.pump_drops(GetTickCount64());
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
    g_host.set_build(g_build, {&host_offline_folder, &host_save_folder});
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

std::string hello() {
    // The fields read on the game's thread; these defaults stand if no frame comes.
    std::string game = " configured=0 feats=off", look = " offline=- active=0 title=0 lang=?";
    std::string scan = " scan=- addresses=-";
    if (g_slot != nullptr) {
        std::string ignored;
        g_mailbox.call(
            [&] {
                game = std::string(" configured=") + (g_game.configured() ? "1" : "0") +
                       " feats=" + zap::one_word(g_game.feats_state());
                look = " offline=" + zap::one_word(g_game.offline_state()) +
                       " active=" + (g_game.active() ? "1" : "0") +
                       " title=" + zap::one_word(g_game.title_state()) +
                       " lang=" + zap::one_word(g_game.language());
                scan = g_host.scan_fields();
                return std::string();
            },
            kFrameWaitMs, ignored);
    }
    return std::string("zexion-ap ") + kVersion + " build=" +
           (g_build ? g_build : "unknown") +
           " hooked=" + (g_slot ? "1" : "0") + game +
           " folder=" + (g_from_game_folder ? "1" : "0") + look +
           g_host.read_fields() + scan + g_host.error_field();
}

// Pipe thread: `work`'s reply, run at the game's next frame; "refused not configured" when
// `needs_config` and nothing configured the helper; "refused no frame" if no frame took it within
// kFrameWaitMs (it then never runs); "error <parse_error>" at once for a request not parsed.
std::string on_game(const std::function<std::string()>& work, bool needs_config,
                    const std::string& parse_error = "") {
    if (!parse_error.empty()) return "error " + parse_error;
    std::string result;
    bool ran = g_mailbox.call(
        [&] {
            if (needs_config && !g_game.configured()) return std::string("refused not configured");
            return work();
        },
        kFrameWaitMs, result);
    return ran ? result : "refused no frame";
}

// `config` (native/README.md, "The pipe"): the addresses the client resolved, as RVAs, parsed on
// the pipe thread first, so a bad one gets its error without waiting for a frame.
std::string config(const std::string& request) {
    std::map<std::string, uint64_t> rvas;
    std::string error = zap::parse_config(request, rvas);
    HMODULE exe = GetModuleHandleW(nullptr);
    return on_game(
        [&] {
            std::string e = g_game.configure(exe, rvas);
            if (!e.empty()) return "error " + e;
            const std::string differs = g_host.client_configured(rvas);
            return differs.empty() ? std::string("ok") : "ok differs " + differs;
        },
        false, error);
}

std::string handle(const std::string& request, bool& stop) {
    // Each request (the client pings every few seconds): a copy thread that is not there is
    // started, which is the case after a stop that timed out and then ended on its own. One that
    // is running is left alone (start_copy_thread).
    if (g_slot != nullptr) g_host.start_copy_thread();
    if (request == "hello") return hello();
    if (request == "ping") return "pong frames=" + std::to_string(g_frames.load());
    if (zap::sets_client_state(request)) g_client_set.store(true);
    if (request.rfind("offline_put ", 0) == 0) {
        // Only in the game (a known build): a program that merely loads this DLL, a test host
        // say, writes none of the real folders.
#if ZAP_CHECK_BUILD
        if (g_build == nullptr) return "error not the game";
#endif
        return g_host.put(request);
    }
    if (request == "unload") {
        // The copy thread goes first, with a bounded wait: stuck in a file call it would keep the
        // DLL from being freed, so the unload is refused and the helper works on.
        if (!g_host.stop_copy_thread(kCopyStopMs)) return "error the copy thread is busy; not unloading";
        std::string why = undo_for_unload();
        if (!why.empty()) {
            g_host.start_copy_thread();  // refused: the helper stays, and keeps writing its copies
            return "error " + why + "; not unloading";
        }
        stop = true;
        return "bye";
    }
    if (request.rfind("config", 0) == 0 &&
        (request.size() == 6 || request[6] == ' ' || request[6] == '\n')) {
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        return config(request);
    }
    if (std::optional<zap::PreparedRequest> prepared = zap::prepare_game_request(request)) {
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        return on_game([&] { return prepared->run(g_game); }, prepared->needs_config,
                       prepared->error);
    }
    return "error unknown request";
}

// Puts back everything the helper changed in the game, then unhooks: "" or why not, with the
// helper then still loaded and working.
std::string undo_for_unload() {
    // From here no frame offers ARCHIPELAGO again, nor runs a watch; then one frame takes it off
    // the menu.
    set_unloading(true);
    bool put_back = false;
    const char* refusal = nullptr;  // Game::unload_refusal's, made before anything is put back
    if (g_slot != nullptr) {
        std::string ignored;
        g_mailbox.call(
            [&] {
                // A menu open, or an AP save's ending on its way: nothing is put back.
                refusal = g_game.unload_refusal();
                if (refusal != nullptr) return std::string();
                // The game's memory only: what the client asked for is kept, for the watches to
                // apply again should the unload be refused (the client would not resend it).
                g_game.put_back_game(&archipelago_chosen);
                g_game.put_back_traps();  // none held: refused above while one is
                g_game.hold_translated_popups();  // until a watch runs again
                g_game.restore_feats();
                g_game.hide_title();
                publish_blocker();
                put_back = true;
                return std::string();
            },
            kFrameWaitMs, ignored);
    }
    // Refused: the next frame's watches set it all again.
    auto refuse = [&](const std::string& why) {
        set_unloading(false);
        return why;
    };
    if (refusal != nullptr) return refuse(refusal);
    if (const char* blocker = set_unloading(true)) {
        return refuse(std::string(blocker) + (put_back ? "" : " (no frame came)"));
    }
    // Steam's slot back even if no frame came: no frame call watches it from here (unloading is
    // set), and the stub it held stays, passing calls through (feats.h).
    g_game.release_steam();
    if (!unhook()) return refuse(kHookReplaced);
    return "";
}

bool in_game_folder() {
    wchar_t self[MAX_PATH], exe[MAX_PATH];
    DWORD a = GetModuleFileNameW(g_self, self, MAX_PATH);
    DWORD b = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (a == 0 || a >= MAX_PATH || b == 0 || b >= MAX_PATH) return false;
    std::wstring s(self, a), e(exe, b);
    s.resize(s.find_last_of(L"\\/") + 1);
    e.resize(e.find_last_of(L"\\/") + 1);
    return CompareStringOrdinal(s.c_str(), -1, e.c_str(), -1, TRUE) == CSTR_EQUAL;
}

DWORD WINAPI helper_thread(void*) {
    try {
        g_from_game_folder = in_game_folder();
        // Before the hook: no frame runs yet. And before the pipe: a `hello` reads title_state,
        // which answers "no version" until this has run (game_calls.h).
        g_game.set_title_version(kVersion);
        set_hooks();
        hook();
        // Then the helper's own scan (never in DllMain: this thread starts after the loader lock
        // is released), before the first read of the files and the pipe: those come after it.
        if (g_slot != nullptr) guarded([] { g_host.run_self_scan(); });
        // Guarded as the scan is: the pipe comes up whatever these throw (no files read then).
        if (g_slot != nullptr) guarded([] { g_host.read_files(true); });
        if (g_slot != nullptr) g_host.start_copy_thread();
        if (!zap::serve_pipe(zap::pipe_name(GetCurrentProcessId()), handle, [] {
                // Only a client that set something has anything to put back, and the copies in the
                // save folder are written only for one (a probe, `helper.py status`, does neither).
                // The files are read for any: the client may have written them.
                const bool write = g_client_set.exchange(false);
                if (write) g_client_gone.store(true);
                if (g_slot != nullptr) guarded([write] { g_host.read_files(write); });
            })) {
            // No pipe: nothing could ever unload us, so put the game back as unload does and
            // leave, retrying until a frame comes; if the hook was replaced, stay.
            for (;;) {
                std::string why = undo_for_unload();
                if (why.empty()) break;
                if (why == kHookReplaced) return 0;
                Sleep(1000);
            }
        }
        // Normally stopped by the unload request already. If it is stuck (a file call that does not
        // return) the DLL stays loaded, as when the hook was replaced: never freed under a thread.
        if (!g_host.stop_copy_thread(kCopyStopMs, false)) return 0;
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
