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
// The offline files (offline.h), read on the helper's thread at start and after each
// disconnect, and the seeds put since (`offline_put`): the next frame takes both (take_offline).
SRWLOCK g_offline_lock = SRWLOCK_INIT;
std::optional<zap::OfflineFiles> g_offline_files;  // read, not yet taken
std::vector<zap::OfflineSeed> g_put_seeds;
// For `hello`: the reads taken by a frame, the seeds the last one found, and its error.
std::atomic<int> g_offline_reads{0};
std::atomic<int> g_offline_found{-1};
std::string g_offline_error;  // guarded by g_offline_lock

// The helper's own scan of the game's code for its addresses (findaddr.h). Made on the helper's
// thread after the hook and before the first read of the offline files and the pipe, so every
// reader (the game's thread taking those files, a `config` request, `hello`) comes after it, by
// the offline lock or the pipe. `why` is "" once it ran, else why it did not (the hook-test build,
// a build the helper does not know).
struct SelfScan {
    std::string why = "not run";
    zap::find::Found found;
};
SelfScan g_scan;
// Game thread, from the scan on: `hello`'s scan and addresses fields (native/README.md, "Finding
// its own addresses").
std::string g_scan_note = "-";
std::string g_addresses = "-";  // "scan", "cache" or "client": where the configuration came from

// Helper's thread: finds the game's addresses by byte pattern (the client's, findaddr.h) in a
// known build. It never runs in DllMain: the thread starts after the loader lock is released.
// Everything that allocates is inside the try, and the catch allocates nothing (an assignment of
// a short literal to a string, `clear`), so a failed allocation cannot escape: it would skip the
// files and the pipe, leaving the helper hooked, unreachable and unable to unload.
void run_self_scan() {
    try {
        g_scan = {};
        g_scan.why = zap::find::find_in_image(GetModuleHandleW(nullptr), g_scan.found);
        if (!g_scan.why.empty()) {
            g_scan_note = g_scan.why;
        } else if (!g_scan.found.failed.empty()) {
            g_scan_note = "failed " + g_scan.found.failed.front();  // taken or refused: below
        } else {
            g_scan_note = "ok";
        }
    } catch (...) {
        g_scan.found.rvas.clear();
        g_scan.found.failed.clear();
        g_scan.why = "scan_failed";
        g_scan_note = "scan_failed";
    }
}

// The helper's own folder for the seeds, and the game's save folder, where each seed's copy goes
// (offline.h). The test build is a host program's, never the game's, and takes every folder it
// writes through restrict_to_test_folder: inside the temporary folder, and never the real ones.
// Its save folder is only what a test names in ZAP_TEST_SAVE_DIR (not ZEXION_SAVE_DIR, which the
// repo's tools define as the real one), its own folder only what LOCALAPPDATA names if that is
// such a folder: else none, and then nothing is read or written. The production build uses none
// of this outside the game: with no known build (a program that is not Zexion) it has no save
// folder, and it takes no seed (handle).
std::wstring offline_dir() {
#ifdef ZAP_HOOKTEST
    return zap::restrict_to_test_folder(zap::offline_folder());
#else
    return zap::offline_folder();
#endif
}

std::wstring save_dir() {
#ifdef ZAP_HOOKTEST
    wchar_t folder[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ZAP_TEST_SAVE_DIR", folder, MAX_PATH);
    return n != 0 && n < MAX_PATH ? zap::restrict_to_test_folder(std::wstring(folder, n))
                                  : std::wstring();
#else
    return g_build == nullptr ? std::wstring() : zap::save_folder();
#endif
}

// The seeds of the game's randomizer saves, as its memory holds the slots (Game::saved_seeds),
// published from the frame call and read by the pipe thread and the copy thread: a seed's copy goes
// into the save folder only while a slot here uses it, and no save file of the game is opened (the
// game opens one to write with no share mode, so a read open of ours could fail its save).
zap::SavedSeeds g_saved;

// The slots as the pipe thread's calls see them: null before the first list is published (none
// known: no copy is written), else the list in `held`.
const std::vector<zap::OfflineSeed>* saved_list(std::vector<zap::OfflineSeed>& held) {
    if (g_saved.generation() == 0) return nullptr;
    held = g_saved.get();
    return &held;
}

// Set by any request that is no probe (a client's: `config`, `offline_put`, `apply`, ...), taken
// when the connection goes: a client that set nothing, `helper.py status` or a hello, leaves
// the files as they are (no copy is written for it).
std::atomic<bool> g_client_set{false};

bool is_probe(const std::string& request) {
    return request == "hello" || request == "ping" ||
           (request.size() > 7 && request.compare(request.size() - 7, 7, "_status") == 0);
}

// An exclusive hold of the offline lock, released however the scope ends.
class OfflineLock {
public:
    OfflineLock() { AcquireSRWLockExclusive(&g_offline_lock); }
    ~OfflineLock() { ReleaseSRWLockExclusive(&g_offline_lock); }
    OfflineLock(const OfflineLock&) = delete;
    OfflineLock& operator=(const OfflineLock&) = delete;
};

// `write` false only makes the seeds one, writing nothing (a client that set nothing went).
void read_offline_with(bool write) {
    std::vector<zap::OfflineSeed> held;
    zap::OfflineFiles files = zap::read_offline_files(offline_dir(), g_build, save_dir(), write,
                                                      saved_list(held));
    // Copied before the lock, and the lock released however this ends: a throw under it would
    // leave the next `hello` waiting for it for good.
    std::string error = files.error;
    OfflineLock lock;
    g_offline_error = std::move(error);
    g_offline_found.store(static_cast<int>(files.seeds.size()));
    g_offline_files = std::move(files);
}

void read_offline() { read_offline_with(true); }

// Counts a call from the game into one of the helper's functions outside the frame call (the
// trampoline counts those inside): helper_thread waits for none before it unloads.
class InCallback {
public:
    InCallback() { g_in_callback.fetch_add(1); }
    ~InCallback() { g_in_callback.fetch_sub(1); }
    InCallback(const InCallback&) = delete;
    InCallback& operator=(const InCallback&) = delete;
};

// Runs the helper's part of a call from the game, or its thread's start-up reads (the files),
// and nothing may unwind: into the game, or past the pipe's start.
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

// Game thread: with no client yet, the helper configured (inactive: only what runs with no client
// uses it) from its own scan's addresses if it found every one the game needs; else, from the
// cached addresses for this build, if they check out against the game (client/offline.py wrote
// them). Nothing from a scan that missed a required address: those are the cache's or the
// client's.
void configure_without_client(const zap::OfflineFiles& files) {
    HMODULE exe = GetModuleHandleW(nullptr);
    if (g_scan.why.empty()) {
        std::string why = g_game.configure(exe, g_scan.found.rvas, false);
        if (why.empty()) {
            g_addresses = "scan";
            // Only optional keys can be missing from an accepted scan (kConfigKeys).
            const std::string lacking = zap::find::failed_names(g_scan.found);
            g_scan_note = lacking.empty() ? "ok" : "ok without " + lacking;
            return;
        }
        g_scan_note = "refused " + zap::find::failure_of(g_scan.found, why);
    }
    if (files.config.empty()) return;
    std::string why = zap::check_offline_cache(exe, files);
    if (why.empty()) why = g_game.configure(exe, files.config, false);
    if (why.empty()) {
        g_addresses = "cache";
        return;
    }
    // The game's thread never waits for the lock (the pipe thread holds it only to swap the files
    // in): an error it cannot record now is dropped.
    if (TryAcquireSRWLockExclusive(&g_offline_lock)) {
        g_offline_error = "cache: " + why;
        ReleaseSRWLockExclusive(&g_offline_lock);
    }
}

// Game thread: the files read last.
void take_files(zap::OfflineFiles files) {
    g_offline_reads.fetch_add(1);
    if (!g_game.configured()) configure_without_client(files);
    // With no client, in place of the last ones; with one, added to them (other_save's look kept).
    if (g_game.active()) {
        g_game.merge_offline(std::move(files.seeds));
    } else {
        g_game.set_offline(std::move(files.seeds));
    }
}

// Game thread: the files read last and the seeds put since, if new.
void take_offline() {
    if (!TryAcquireSRWLockExclusive(&g_offline_lock)) return;
    std::optional<zap::OfflineFiles> files = std::exchange(g_offline_files, std::nullopt);
    std::vector<zap::OfflineSeed> put = std::exchange(g_put_seeds, {});
    ReleaseSRWLockExclusive(&g_offline_lock);
    if (files) take_files(std::move(*files));
    // After the files, which may have been read before these were put. The client puts its own
    // seed alone: the one its look applies in (Game::seed_look_loaded).
    if (!put.empty()) {
        g_game.note_client_seed(put.back());
        g_game.merge_offline(std::move(put));
    }
}

// Game thread: the slots' seeds published for the other threads, on the first frame and every
// 16th (cheap: 30 slots' markers and sections). A change wakes the copy thread by its generation.
void snapshot_saves() {
    static uint32_t frame_count = 0;
    if ((frame_count++ & 15u) != 0) return;
    g_saved.publish(g_game.saved_seeds());
}

// The copy thread: when the game's slots change (a save made, a new game, a deleted save) it
// reads the seed files and writes the copy of each seed a slot now uses into the save folder:
// the case of a seed a client put before the game's first save, which the put could not copy,
// whatever the client does after (it may be connected till the game closes). Never the frame
// call. Polls the slots' generation every 250 ms and stops when asked (helper_thread joins it
// before the DLL can go).
std::atomic<bool> g_copy_stop{false};
HANDLE g_copy_thread = nullptr;
HANDLE g_copy_wake = nullptr;  // set to stop it at once (else it looks every 250 ms)

DWORD WINAPI copy_thread(void*) {
    uint64_t done = 0;
    while (!g_copy_stop.load()) {
        WaitForSingleObject(g_copy_wake, 250);
        if (g_copy_stop.load()) break;
        const uint64_t generation = g_saved.generation();
        if (generation == done) continue;
        done = generation;
        try {
            std::vector<zap::OfflineSeed> saved = g_saved.get();
            zap::OfflineFiles files =
                zap::read_offline_files(offline_dir(), g_build, save_dir(), true, &saved);
            if (!files.error.empty()) {
                OfflineLock lock;
                g_offline_error = files.error;
            }
        } catch (...) {
            // Nothing may end this thread but being asked to.
        }
    }
    return 0;
}

// Starts the copy thread: the helper's start, again after a refused unload stopped it, and at each
// request (handle), which finds one that went since a stop timed out.
void start_copy_thread() {
    if (g_copy_thread != nullptr) {
        // One that is still running is the one to keep. One that is gone (a stop that timed out and
        // was not taken back, or one that ended on its own) leaves its handles: closed and cleared,
        // so a new thread starts in its place, not none ever again.
        if (WaitForSingleObject(g_copy_thread, 0) != WAIT_OBJECT_0) return;
        CloseHandle(g_copy_thread);
        CloseHandle(g_copy_wake);
        g_copy_thread = nullptr;
        g_copy_wake = nullptr;
    }
    g_copy_stop.store(false);
    g_copy_wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_copy_wake == nullptr) return;
    g_copy_thread = CreateThread(nullptr, 0, copy_thread, nullptr, 0, nullptr);
    if (g_copy_thread == nullptr) {
        CloseHandle(g_copy_wake);
        g_copy_wake = nullptr;
    }
}

// Stops the copy thread and waits for it, at most `milliseconds`: true if it is gone (or there was
// none). It may be inside a file call that does not return (an antivirus filter, a cloud
// placeholder on the save folder), and the DLL must not be freed under it: on false it is asked
// to go on as before (it was not stopped), and the caller does not unload. With `resume` false (the
// helper's last wait, which is not followed by any unload) the stop stays asked for: the thread
// ends when its call returns, and the DLL, which stays loaded, is never freed under it.
bool stop_copy_thread(DWORD milliseconds, bool resume = true) {
    if (g_copy_thread == nullptr) return true;
    g_copy_stop.store(true);
    SetEvent(g_copy_wake);
    if (WaitForSingleObject(g_copy_thread, milliseconds) != WAIT_OBJECT_0) {
        if (resume) {
            g_copy_stop.store(false);
            ResetEvent(g_copy_wake);
        }
        // It may have seen the stop just before: then it is gone, and the caller may go on.
        if (WaitForSingleObject(g_copy_thread, 0) != WAIT_OBJECT_0) return false;
    }
    CloseHandle(g_copy_thread);
    CloseHandle(g_copy_wake);
    g_copy_thread = nullptr;
    g_copy_wake = nullptr;
    return true;
}

// How long an unload waits for the copy thread. The reply comes after this and, once it has
// stopped, the wait for a frame in undo_for_unload (1 s), and the client waits for it 3 s in
// `Helper.unload` (APPLY_SECONDS) but only 2 s in `_unload_refusal` and tools/helper.py
// (REPLY_SECONDS): 0.5 s and 1 s stay under 2 s, so a refusal or a "bye" is never taken for a lost
// reply. A stuck thread is refused at once after this wait (no frame wait). An idle thread stops
// at once (its event).
constexpr DWORD kCopyStopMs = 500;

// Pipe thread: a seed file a client has just put, for the next frame to know (take_offline).
void note_put_seed(zap::OfflineSeed seed) {
    AcquireSRWLockExclusive(&g_offline_lock);
    // One of the same name still waiting (no frame since: the game in the background) is
    // replaced, so the queue holds one a seed however often a client puts it. The newest is
    // always last (take_offline: the client's seed is the last put).
    std::erase_if(g_put_seeds, [&](const zap::OfflineSeed& s) { return s.name == seed.name; });
    g_put_seeds.push_back(std::move(seed));
    ReleaseSRWLockExclusive(&g_offline_lock);
}

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
            take_offline();
            guarded([] { snapshot_saves(); });  // a throw here skips no other frame work
            g_mailbox.serve();
            if (TryAcquireSRWLockExclusive(&g_menu_lock)) {
                // No watch once an unload has begun: what the client asked for stays set, and a
                // refused unload's next frame applies it again. Guarded: a throw that kept the
                // lock would make the next unload wait on it for good.
                if (!g_unloading) {
                    guarded([] {
                        g_game.watch_newgame();
                        g_game.watch_all(&archipelago_chosen);
                    });
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

// A hello field kept to one word: spaces (a seed file's name, a reason) as underscores.
std::string one_word(std::string text) {
    std::replace(text.begin(), text.end(), ' ', '_');
    return text;
}

std::string hello() {
    // The fields read on the game's thread; these defaults stand if no frame comes.
    std::string game = " configured=0 feats=off", look = " offline=- active=0 title=0 lang=?";
    std::string found = " scan=- addresses=-";
    if (g_slot != nullptr) {
        std::string ignored;
        g_mailbox.call(
            [&] {
                game = std::string(" configured=") + (g_game.configured() ? "1" : "0") +
                       " feats=" + one_word(g_game.feats_state());
                look = " offline=" + one_word(g_game.offline_state()) +
                       " active=" + (g_game.active() ? "1" : "0") +
                       " title=" + one_word(g_game.title_state()) +
                       " lang=" + one_word(g_game.language());
                found = " scan=" + one_word(g_scan_note) + " addresses=" + g_addresses;
                return std::string();
            },
            kFrameWaitMs, ignored);
    }
    AcquireSRWLockShared(&g_offline_lock);
    std::string error = g_offline_error;
    ReleaseSRWLockShared(&g_offline_lock);
    return std::string("zexion-ap ") + kVersion + " build=" +
           (g_build ? g_build : "unknown") +
           " hooked=" + (g_slot ? "1" : "0") + game +
           " folder=" + (g_from_game_folder ? "1" : "0") + look +
           " reads=" + std::to_string(g_offline_reads.load()) + "/" +
           std::to_string(g_offline_found.load()) + found +
           (error.empty() ? "" : " read_error=" + one_word(error));
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

// The requests that run on the game's thread (native/README.md, "The pipe"). A `game` one is
// matched by its whole name; a `run` one by its first word, then a space or a line break, and it
// parses on the pipe thread first, so a bad one gets its error without waiting for a frame.
struct Request {
    const char* name;
    bool needs_config;  // `game` refused while nothing configured the helper
    std::string (*game)();
    std::string (*run)(const std::string& request) = nullptr;
};

std::string discs(const std::string& request) {
    // The OS Install Disks goal's count and the scanner's line (scan.h; the format: requests.h).
    int count = 0, required = 0;
    bool told = false;
    std::string line;
    std::string error = zap::parse_discs(request, count, required, line, told);
    return on_game(
        [&] {
            bool on = !line.empty();
            g_game.set_discs(count, required, std::move(line), told);
            return std::string(on ? "discs on" : "discs off");
        },
        true, error);
}

std::string mission_prompt(const std::string& request) {
    // Offer RETURN HOME WITH THE DISKS? when the game is next idle in play, lapsing while the
    // player is in play outside home's cell (mission.h; the format: requests.h).
    int column = -1, row = -1;
    std::string error = zap::parse_mission(request, column, row);
    return on_game(
        [&] {
            return std::string(g_game.request_mission(column, row) ? "queued"
                                                                   : "refused not configured");
        },
        true, error);
}

std::string ap_labels(const std::string& request) {
    std::map<int, zap::Label> labels;
    std::string error = zap::parse_labels(request, labels);
    return on_game(
        [&] {
            size_t n = labels.size();
            g_game.set_labels(std::move(labels));
            return "labeled " + std::to_string(n);
        },
        true, error);
}

std::string ap_newgame(const std::string& request) {
    zap::NewGamePlan plan;
    std::string error = zap::parse_newgame(request, plan);
    return on_game(
        [&] {
            return std::string(g_game.arm_newgame(std::move(plan)) ? "armed"
                                                                   : "refused not configured");
        },
        true, error);
}

std::string config(const std::string& request) {
    std::map<std::string, uint64_t> rvas;
    std::string error = zap::parse_config(request, rvas);
    HMODULE exe = GetModuleHandleW(nullptr);
    return on_game(
        [&] {
            std::string e = g_game.configure(exe, rvas);
            if (!e.empty()) return "error " + e;
            g_addresses = "client";
            // The client's addresses take over as they are; a key where they differ from the
            // helper's own is a bug in one scan, said so it is seen (client/native.py logs it).
            std::string differs =
                g_scan.why.empty() ? zap::find::differing_keys(rvas, g_scan.found.rvas) : "";
            return differs.empty() ? std::string("ok") : "ok differs " + differs;
        },
        false, error);
}

std::string apply(const std::string& request) {
    zap::Batch batch;
    std::string error = zap::parse_batch(request, batch);
    // Game::apply refuses unconfigured; a batch no frame took never applies.
    return on_game([&] { return g_game.apply(batch); }, false, error);
}

const Request kRequests[] = {
    {"shield", false, [] { return g_game.spawn_shield(GetTickCount64()); }},
    {"die", false, [] { return g_game.kill(GetTickCount64()); }},
    {"assists lock", true, [] { g_game.set_assists_locked(true); return std::string("locked"); }},
    {"assists free", true, [] { g_game.set_assists_locked(false); return std::string("free"); }},
    {"other_save", true, [] { return g_game.other_save(); }},
    {"client_back", true, [] { return std::string(g_game.client_back() ? "shown" : "none"); }},
    {"intel on", true, [] { g_game.set_intel(true); return std::string("intel on"); }},
    {"intel off", true, [] { g_game.set_intel(false); return std::string("intel off"); }},
    {"mission_prompt", true, nullptr, &mission_prompt},
    {"mission_status", true, [] { return g_game.mission_status(); }},
    // How the last black after YES ended (mission.h, black): for live checks.
    {"mission_black", true, [] { return g_game.mission_black(); }},
    // The received scraps' drops (game_calls.h, drop_status): for live checks.
    {"drop_status", false, [] { return g_game.drop_status(); }},
    // The traps set off (game_calls.h, trap_status): for live checks.
    {"trap_status", false, [] { return g_game.trap_status(); }},
    {"discs_status", false, [] { return g_game.equipment_status(); }},
    {"discs", false, nullptr, &discs},
    {"ap_labels", false, nullptr, &ap_labels},
    {"ap_newgame", false, nullptr, &ap_newgame},
    {"disarm", false, [] { g_game.disarm_newgame(); return std::string("disarmed"); }},
    {"newgame_status", false, [] { return g_game.newgame_status(); }},
    {"config", false, nullptr, &config},
    {"apply", false, nullptr, &apply},
};

bool matches(const Request& r, const std::string& request) {
    if (r.run == nullptr) return request == r.name;
    const size_t n = std::strlen(r.name);
    return request.rfind(r.name, 0) == 0 &&
           (request.size() == n || request[n] == ' ' || request[n] == '\n');
}

std::string handle(const std::string& request, bool& stop) {
    // Each request (the client pings every few seconds): a copy thread that is not there is
    // started, which is the case after a stop that timed out and then ended on its own. One that
    // is running is left alone (start_copy_thread).
    if (g_slot != nullptr) start_copy_thread();
    if (request == "hello") return hello();
    if (request == "ping") return "pong frames=" + std::to_string(g_frames.load());
    if (!is_probe(request)) g_client_set.store(true);
    if (request.rfind("offline_put ", 0) == 0) {
        // Only in the game (a known build): a program that merely loads this DLL, a test host
        // say, writes none of the real folders.
#if ZAP_CHECK_BUILD
        if (g_build == nullptr) return "error not the game";
#endif
        // Written by the helper: a packaged client's AppData writes are redirected (offline.h).
        // The seed's copy in the game's save folder too (offline.h): Steam Cloud carries it.
        zap::OfflineSeed seed;
        std::string save_error;
        std::vector<zap::OfflineSeed> held;
        std::string reply = zap::put_offline_file(offline_dir(), g_build, request, &seed,
                                                  save_dir(), &save_error, saved_list(held));
        if (!save_error.empty()) {
            OfflineLock lock;
            g_offline_error = "save folder copy: " + save_error;
        }
        if (!seed.name.empty()) note_put_seed(std::move(seed));
        return reply;
    }
    if (request == "unload") {
        // The copy thread goes first, with a bounded wait: stuck in a file call it would keep the
        // DLL from being freed, so the unload is refused and the helper works on.
        if (!stop_copy_thread(kCopyStopMs)) return "error the copy thread is busy; not unloading";
        std::string why = undo_for_unload();
        if (!why.empty()) {
            start_copy_thread();  // refused: the helper stays, and keeps writing its copies
            return "error " + why + "; not unloading";
        }
        stop = true;
        return "bye";
    }
    for (const Request& r : kRequests) {
        if (!matches(r, request)) continue;
        if (g_slot == nullptr) return "error not hooked, so nothing runs on the game's thread";
        return r.run != nullptr ? r.run(request) : on_game(r.game, r.needs_config);
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
        if (g_slot != nullptr) guarded(run_self_scan);
        // Guarded as the scan is: the pipe comes up whatever these throw (no files read then).
        if (g_slot != nullptr) guarded(read_offline);
        if (g_slot != nullptr) start_copy_thread();
        if (!zap::serve_pipe(zap::pipe_name(GetCurrentProcessId()), handle, [] {
                g_client_gone.store(true);
                // The client may have written them; the copies in the save folder only if it
                // set something (a probe, `helper.py status`, writes none).
                const bool write = g_client_set.exchange(false);
                if (g_slot != nullptr) guarded([write] { read_offline_with(write); });
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
        if (!stop_copy_thread(kCopyStopMs, false)) return 0;
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
