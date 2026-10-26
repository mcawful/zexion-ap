// zexion_ap.dll, loaded by the client or by the proxy (native/proxy/). On load, one thread hooks
// the game's SDL_GL_SwapWindow import slot through a trampoline (iat.h), in a known build only
// (build.h), and then serves the pipe (pipe.h). The frame function so runs on the game's thread,
// between frames (docs/re/memory.md, "The per-frame call"). "unload" restores the slot, waits
// for calls to drain, and frees the DLL.
//
// Hook target, set at build time: SDL_GL_SwapWindow in a known build (zexion_ap.dll), or the
// host's own GetTickCount in any but a known build (zexion_ap_hooktest.dll, for the native tests).

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
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

constexpr char kVersion[] = "0.30.0";
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
// Used on the game's thread only: by frame() and the mailbox work it runs, and by the helper's
// functions the game calls (archipelago_chosen and the others below).
zap::Game g_game;
std::atomic_flag g_in_frame_work = ATOMIC_FLAG_INIT;
// What an unload must wait for (Game::unload_blocker: a game structure that may still hold a
// helper function, a record restyled, the scanner's text or the pause screen's branch still
// the helper's), or null: unload refuses while set. The frame offers ARCHIPELAGO only under
// g_menu_lock with g_unloading clear, and only tries the lock, so it never waits on the pipe
// thread.
std::atomic<const char*> g_blocker{nullptr};
SRWLOCK g_menu_lock = SRWLOCK_INIT;
bool g_unloading = false;  // guarded by g_menu_lock
std::atomic<int> g_in_callback{0};
// Set by the pipe thread when its client disconnects; the next frame puts everything back before
// it serves any request, so a reconnecting client's requests come after.
std::atomic<bool> g_client_gone{false};
// What clients had set, from a refused unload (undo_for_unload): the next frame sets it again
// before anything else, so before any request after the refusal. The pipe thread publishes it,
// the frame takes it; neither waits.
std::atomic<zap::ClientState*> g_rearm{nullptr};
// Whether the helper was loaded from the game's own folder (by the proxy, native/proxy/, or by
// a client with no build of its own) rather than from elsewhere: `hello` reports it.
bool g_from_game_folder = false;
// The offline files (offline.h), read on the helper's thread at start and after each
// disconnect, handed to the next frame under g_offline_lock.
SRWLOCK g_offline_lock = SRWLOCK_INIT;
bool g_offline_ready = false;
zap::OfflineFiles g_offline_files;
// For `hello`: the reads taken by a frame, the seeds the last one found, and its error.
std::atomic<int> g_offline_reads{0};
std::atomic<int> g_offline_found{-1};
std::string g_offline_error;  // guarded by g_offline_lock

void read_offline() {
    zap::OfflineFiles files = zap::read_offline_files(zap::offline_folder(), g_build);
    AcquireSRWLockExclusive(&g_offline_lock);
    g_offline_error = files.error;
    g_offline_found.store(static_cast<int>(files.seeds.size()));
    g_offline_files = std::move(files);
    g_offline_ready = true;
    ReleaseSRWLockExclusive(&g_offline_lock);
}

// Counts a call from the game into one of the helper's functions outside the frame call (the
// trampoline counts those inside): helper_thread waits for none before it unloads.
class InCallback {
public:
    InCallback() { g_in_callback.fetch_add(1); }
    ~InCallback() { g_in_callback.fetch_sub(1); }
    InCallback(const InCallback&) = delete;
    InCallback& operator=(const InCallback&) = delete;
};

// Runs the helper's part of a call from the game: nothing may unwind into the game.
template <typename Work>
void guarded(Work&& work) {
    try {
        work();
    } catch (...) {
        // Dropped: the game's own call goes on as if the helper had done nothing.
    }
}

// Game thread: what an unload must wait for, published for the pipe thread.
void publish_blocker() { g_blocker.store(g_game.unload_blocker()); }

// ARCHIPELAGO's callback. The game calls it on its thread but outside the trampoline's count, so
// InCallback counts it, and unload first takes it off the menu (via the mailbox) and refuses
// while it may remain. The other game-called functions below follow the same rule.
void archipelago_chosen(void*, int) {
    InCallback in;
    guarded([] { g_game.start_archipelago(&archipelago_chosen); });
    publish_blocker();
}

// A handler the helper puts in place of the game's: the game's own for the widget (`Original`,
// null for none) first, then the helper's part (`After`), with the arguments the game's widget
// code passes (its handlers read no more than these).
using Handler = void (*)(void*, intptr_t, intptr_t, intptr_t);
template <void* (*Original)(void* widget), void (*After)(void* widget, intptr_t event)>
void handler_event(void* widget, intptr_t event, intptr_t r8, intptr_t r9) {
    InCallback in;
    auto handler = reinterpret_cast<Handler>(Original(widget));
    if (handler != nullptr) handler(widget, event, r8, r9);
    guarded([&] { After(widget, event); });
}

// The game-mode panel's handler while ARCHIPELAGO is offered (menu.h): ARCHIPELAGO's hover
// border fitted in the frame the game lays it out.
void* panel_original(void*) { return g_game.panel_handler(); }
void panel_after(void*, intptr_t) { g_game.fit_archipelago_border(); }

// The Energy Tank panel's handler while it shows the disk (equip.h): the disk's name and
// description when the disk is hovered or selected.
void* equip_panel_original(void*) { return g_game.equip_panel_fn(); }
void equip_panel_after(void* widget, intptr_t event) { g_game.equipment_event(widget, event); }

// The save entries' handler while the file select shows the disk count (files.h): the entry
// rewritten after the game fills it (event 0xF).
void* file_entry_original(void* widget) { return g_game.file_entry_original(widget); }
void file_entry_after(void* widget, intptr_t event) { g_game.file_event(widget, event); }

// The map's per-frame handler while the helper names the stand-ins' squares (mapmarks.h), called
// with the map menu's first child: the cursor's name rewritten after the game writes it.
void* map_original(void* child) { return g_game.map_original(child); }
void map_after(void*, intptr_t) { g_game.map_after(); }

// EQUIPMENT's callback while the disk goal is on (equip.h): the game builds the screen, then the
// disk widget is added in the same call.
void equipment_chosen(void* widget, int event) {
    InCallback in;
    auto game_fn = reinterpret_cast<void (*)(void*, int)>(g_game.equipment_fn());
    if (game_fn != nullptr) game_fn(widget, event);
    guarded([] { g_game.equipment_built(); });
}

// RETURN HOME WITH THE DISKS? (mission.h). The opener: the game runs it as its transition
// routine; it builds the game's prompt and relabels it. YES: the game's NO (closing the prompt),
// then the ending once idle.
void mission_open() {
    InCallback in;
    guarded([] { g_game.mission_open(); });
}

void mission_yes(void* widget, int event) {
    InCallback in;
    void* no = nullptr;
    guarded([&] { no = g_game.mission_yes(); });
    if (no != nullptr) reinterpret_cast<void (*)(void*, int)>(no)(widget, event);
}

// Game thread: the helper's wrappers for the game's menus, given to Game before it is configured.
void set_hooks() {
    zap::Hooks hooks;
    hooks.panel = reinterpret_cast<void*>(&handler_event<&panel_original, &panel_after>);
    hooks.equipment = reinterpret_cast<void*>(&equipment_chosen);
    hooks.equipment_panel =
        reinterpret_cast<void*>(&handler_event<&equip_panel_original, &equip_panel_after>);
    hooks.mission_open = reinterpret_cast<void*>(&mission_open);
    hooks.mission_yes = reinterpret_cast<void*>(&mission_yes);
    hooks.file_entry =
        reinterpret_cast<void*>(&handler_event<&file_entry_original, &file_entry_after>);
    hooks.map = reinterpret_cast<void*>(&handler_event<&map_original, &map_after>);
    g_game.set_hooks(hooks);
}

// Game thread: the files read last, if new. With no client yet, the cached addresses for this
// build configure the helper (inactive: only the offline look uses them).
void take_offline() {
    if (!TryAcquireSRWLockExclusive(&g_offline_lock)) return;
    bool ready = g_offline_ready;
    zap::OfflineFiles files;
    if (ready) {
        files = std::move(g_offline_files);
        g_offline_ready = false;
    }
    ReleaseSRWLockExclusive(&g_offline_lock);
    if (!ready) return;
    g_offline_reads.fetch_add(1);
    if (!g_game.configured() && !files.config.empty()) {
        std::string why = zap::check_offline_cache(GetModuleHandleW(nullptr), files);
        if (why.empty()) {
            set_hooks();
            why = g_game.configure(GetModuleHandleW(nullptr), files.config, false);
        }
        // The game's thread never waits for the lock: an error it cannot record now is dropped
        // (the pipe thread holds the lock only to swap the files in).
        if (!why.empty() && TryAcquireSRWLockExclusive(&g_offline_lock)) {
            g_offline_error = "cache: " + why;
            ReleaseSRWLockExclusive(&g_offline_lock);
        }
    }
    if (!g_game.active()) g_game.set_offline(std::move(files.seeds));
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
            if (zap::ClientState* state = g_rearm.exchange(nullptr)) {
                std::unique_ptr<zap::ClientState> owned(state);
                g_game.rearm(std::move(*owned));
            }
            if (g_client_gone.exchange(false)) {
                // The client went away: put back what it set; only the offline look may dress a
                // save until a client configures again. Its popups still show.
                g_game.client_gone(&archipelago_chosen);
            }
            take_offline();
            g_mailbox.serve();
            g_game.watch_newgame();
            if (TryAcquireSRWLockExclusive(&g_menu_lock)) {
                // Once an unload has begun, no watch runs (nothing is put back in).
                if (!g_unloading) g_game.watch_all(&archipelago_chosen);
                publish_blocker();
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

// `hello`'s fields from the game's thread: configured, the escape skip offered, a client there,
// the offline seeds (offline.h) and the title screen's lines (title.h).
struct HelloState {
    bool configured = false;
    bool skip = false;
    bool active = false;
    std::string offline = "-";
    std::string title = "0";
};

// A hello field kept to one word: spaces (a seed file's name, a reason) as underscores.
std::string one_word(std::string text) {
    for (char& c : text) c = c == ' ' ? '_' : c;
    return text;
}

std::string hello() {
    // Read on the game's thread; the defaults stand if no frame comes.
    HelloState state;
    if (g_slot != nullptr) {
        std::string ignored;
        g_mailbox.call(
            [&state] {
                state.configured = g_game.configured();
                state.skip = g_game.skip_offered();
                state.active = g_game.active();
                state.offline = g_game.offline_state();
                state.title = g_game.title_state();
                return std::string();
            },
            kFrameWaitMs, ignored);
    }
    AcquireSRWLockShared(&g_offline_lock);
    std::string error = g_offline_error;
    ReleaseSRWLockShared(&g_offline_lock);
    return std::string("zexion-ap ") + kVersion + " build=" + (g_build ? g_build : "unknown") +
           " hooked=" + (g_slot ? "1" : "0") + " configured=" + (state.configured ? "1" : "0") +
           " skip=" + (state.skip ? "1" : "0") + " folder=" + (g_from_game_folder ? "1" : "0") +
           " offline=" + one_word(state.offline) + " active=" + (state.active ? "1" : "0") +
           " title=" + one_word(state.title) + " reads=" + std::to_string(g_offline_reads.load()) +
           "/" + std::to_string(g_offline_found.load()) +
           (error.empty() ? "" : " read_error=" + one_word(error));
}

// Pipe thread: `work` run on the game's thread at its next frame, and its reply; "refused not
// configured" instead, when `needs_config` and nothing configured the helper; "refused no frame"
// if no frame took it within kFrameWaitMs (it then never runs).
std::string on_game(const std::function<std::string()>& work, bool needs_config) {
    std::string result;
    bool ran = g_mailbox.call(
        [&] {
            if (needs_config && !g_game.configured()) return std::string("refused not configured");
            return work();
        },
        kFrameWaitMs, result);
    return ran ? result : "refused no frame";
}

// The requests that run on the game's thread (native/README.md lists them and their replies),
// each matched by its whole name, or by its first word: `prefix` (the name then anything) or
// `word` (the name alone, or the name, a space and anything). Most are `game`, run on the
// game's thread (on_game) as they are; the others `run`, which parse the request on the pipe's
// thread first, so a bad one gets its error without waiting for a frame.
enum class Match { kName, kPrefix, kWord };
struct Request {
    const char* name;
    Match match;
    bool needs_config;  // `game` refused while nothing configured the helper
    std::string (*game)();
    std::string (*run)(const std::string& request) = nullptr;
};

std::string disk_slots(const std::string& request) {
    // "disk_slots": off. "disk_slots COUNT REQUIRED SLOTS": the file select's disk count for the
    // slots holding the seed (files.h); REQUIRED is checked but not shown (requests.h).
    std::map<int, int> counts;
    std::string error = zap::parse_disk_slots(request, counts);
    if (!error.empty()) return "error " + error;
    return on_game(
        [&] {
            bool on = !counts.empty();
            g_game.set_disk_slots(std::move(counts));
            return std::string(on ? "disk_slots on" : "disk_slots off");
        },
        true);
}

std::string discs(const std::string& request) {
    // "discs": off. "discs COUNT REQUIRED HEX TOLD": the OS Install Disks goal's count and the
    // scanner's line, its characters in hex (scan.h).
    int count = 0, required = 0;
    bool told = false;
    std::string line;
    std::string error = zap::parse_discs(request, count, required, line, told);
    if (!error.empty()) return "error " + error;
    return on_game(
        [&] {
            bool on = !line.empty();
            g_game.set_discs(count, required, std::move(line), told);
            return std::string(on ? "discs on" : "discs off");
        },
        true);
}

std::string ap_labels(const std::string& request) {
    std::map<int, zap::Label> labels;
    std::string error = zap::parse_labels(request, labels);
    if (!error.empty()) return "error " + error;
    return on_game(
        [&] {
            size_t n = labels.size();
            g_game.set_labels(std::move(labels));
            return "labeled " + std::to_string(n);
        },
        true);
}

std::string ap_newgame(const std::string& request) {
    zap::NewGamePlan plan;
    std::string error = zap::parse_newgame(request, plan);
    if (!error.empty()) return "error " + error;
    return on_game(
        [&] {
            g_game.arm_newgame(std::move(plan));
            return std::string("armed");
        },
        true);
}

std::string config(const std::string& request) {
    std::map<std::string, uint64_t> rvas;
    std::string error = zap::parse_config(request, rvas);
    if (!error.empty()) return "error " + error;
    HMODULE exe = GetModuleHandleW(nullptr);
    return on_game(
        [&] {
            set_hooks();
            std::string e = g_game.configure(exe, rvas);
            return e.empty() ? std::string("ok") : "error " + e;
        },
        false);
}

std::string apply(const std::string& request) {
    zap::Batch batch;
    std::string error = zap::parse_batch(request, batch);
    if (!error.empty()) return "error " + error;
    // Game::apply refuses itself while not configured; a batch no frame took is never applied.
    return on_game([&] { return g_game.apply(batch); }, false);
}

const Request kRequests[] = {
    {"shield", Match::kName, false, [] { return g_game.spawn_shield(GetTickCount64()); }},
    {"die", Match::kName, false, [] { return g_game.kill(GetTickCount64()); }},
    {"assists lock", Match::kName, true,
     [] {
         g_game.set_assists_locked(true);
         return std::string("locked");
     }},
    {"assists free", Match::kName, true,
     [] {
         g_game.set_assists_locked(false);
         return std::string("free");
     }},
    // The client's session sees a randomizer save in play that is not its seed's (offline.h).
    {"other_save", Match::kName, true,
     [] { return g_game.other_save(); }},
    // The client's session sees its seed's save in play: if it was played offline, the player is
    // told the client is back (offline.h).
    {"client_back", Match::kName, true,
     [] { return std::string(g_game.client_back() ? "shown" : "none"); }},
    // The Area Map named INTEL CHIP, its map reveal off (intel.h), or put back.
    {"intel on", Match::kName, true,
     [] {
         g_game.set_intel(true);
         return std::string("intel on");
     }},
    {"intel off", Match::kName, true,
     [] {
         g_game.set_intel(false);
         return std::string("intel off");
     }},
    // Offer RETURN HOME WITH THE DISKS? when the game is next idle in play (mission.h).
    {"mission_prompt", Match::kName, true,
     [] {
         g_game.request_mission();
         return std::string("queued");
     }},
    {"mission_status", Match::kName, true, [] { return g_game.mission_status(); }},
    {"disk_slots", Match::kWord, false, nullptr, &disk_slots},
    {"discs_status", Match::kName, false, [] { return g_game.equipment_status(); }},
    {"discs", Match::kWord, false, nullptr, &discs},
    {"ap_labels", Match::kPrefix, false, nullptr, &ap_labels},
    {"ap_newgame", Match::kPrefix, false, nullptr, &ap_newgame},
    {"disarm", Match::kName, false,
     [] {
         g_game.disarm_newgame();
         return std::string("disarmed");
     }},
    {"newgame_status", Match::kName, false, [] { return g_game.newgame_status(); }},
    {"config", Match::kPrefix, false, nullptr, &config},
    {"apply", Match::kPrefix, false, nullptr, &apply},
};

bool matches(const Request& r, const std::string& request) {
    switch (r.match) {
        case Match::kName:
            return request == r.name;
        case Match::kPrefix:
            return request.rfind(r.name, 0) == 0;
        case Match::kWord:
            return request == r.name || request.rfind(std::string(r.name) + " ", 0) == 0;
    }
    return false;
}

std::string handle(const std::string& request, bool& stop) {
    if (request == "hello") return hello();
    if (request == "ping") return "pong frames=" + std::to_string(g_frames.load());
    if (request.rfind("offline_put ", 0) == 0) {
        // Written by the helper, not the client: a packaged client's AppData writes are
        // redirected (offline.h).
        return zap::put_offline_file(zap::offline_folder(), g_build, request);
    }
    if (request == "unload") {
        std::string why = undo_for_unload();
        if (!why.empty()) return "error " + why + "; not unloading";
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
    // From here no frame offers ARCHIPELAGO again; then one frame takes it off the menu.
    set_unloading(true);
    // What clients set, armed again should the unload be refused (they would not resend it).
    bool saved = false;
    zap::ClientState state;
    if (g_slot != nullptr) {
        std::string ignored;
        g_mailbox.call(
            [&] {
                state = g_game.snapshot();
                saved = true;
                g_game.put_back_all(&archipelago_chosen);
                g_game.hide_title();  // a refused unload shows them again (its watch)
                publish_blocker();
                return std::string();
            },
            kFrameWaitMs, ignored);
    }
    auto refuse = [&](const std::string& why) {
        set_unloading(false);
        // Re-armed by the next frame however late it comes; a mailbox call could be withdrawn
        // and the client would not resend. One left from an earlier refusal was taken already:
        // a frame ran the snapshot after it.
        if (saved) delete g_rearm.exchange(new zap::ClientState(std::move(state)));
        return why;
    };
    if (const char* blocker = set_unloading(true)) {
        return refuse(std::string(blocker) + (saved ? "" : " (no frame came)"));
    }
    if (!unhook()) return refuse(kHookReplaced);
    delete g_rearm.exchange(nullptr);  // one no frame took: unloading anyway
    return "";
}

// Whether this DLL sits in the game executable's own folder.
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
        g_game.set_title_version(kVersion);  // before the hook: no frame runs yet
        hook();
        if (g_slot != nullptr) read_offline();
        if (!zap::serve_pipe(zap::pipe_name(GetCurrentProcessId()), handle, [] {
                g_client_gone.store(true);
                if (g_slot != nullptr) read_offline();  // the client may have written them
            })) {
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
