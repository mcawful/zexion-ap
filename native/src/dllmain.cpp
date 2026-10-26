// zexion_ap.dll: the native helper the AP client loads into the running game, or the proxy
// version.dll loads as the game starts (native/proxy/).
//
// On load it starts one thread of its own. If the game is a build it knows, that thread points
// the game's import-table slot for SDL_GL_SwapWindow at a trampoline (iat.h), which calls the
// helper's frame function and then SDL. The game makes that call from its main loop
// (docs/re/memory.md, "The per-frame call"), so the frame function runs on the game's thread,
// between frames. The thread then serves the command pipe (pipe.h). "unload" points the slot
// and the trampoline back at SDL, waits until no call is inside the helper, and unloads the
// DLL, so a rebuilt helper can be loaded without restarting the game.
//
// Each frame call counts itself; puts back everything the helper changed if the pipe's client
// went away; takes the files a client left for AP saves without it (offline.h); serves the
// mailbox (requests from the pipe thread that touch the game, mailbox.h); runs each part's
// watch (game_calls.h: the new game and ARCHIPELAGO, ASSIST, the escape skip, the OS Install
// Disks goal's screens, Intel Chips, the title screen, the offline look, the map and other
// players' items); publishes what an unload must wait for; and shows queued popups. None of it
// can race the game.
//
// The target is set at build time: the game's SDL_GL_SwapWindow, behind the build check, in
// zexion_ap.dll; this test program's own GetTickCount, with no build check, in the
// zexion_ap_hooktest.dll that native/tests/ load and unload under load.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
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

constexpr char kVersion[] = "0.27.0";
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
// Whether an item record is restyled (the stand-ins, apitem.h; the Area Map, intel.h), which
// unload must undo first.
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
// frame puts back everything the helper changed and goes inactive, before it serves any
// request, so a reconnected client's `config` and requests come after it. Without a client the
// game is the base game (the helper may have been loaded with the game, by the proxy).
std::atomic<bool> g_client_gone{false};
// What clients had set, from a refused unload (undo_for_unload): the next frame sets it again
// before anything else, so before any request after the refusal. The pipe thread publishes it,
// the frame takes it; neither waits.
std::atomic<zap::ClientState*> g_rearm{nullptr};
// Whether the helper was loaded from the game's own folder (by the proxy, native/proxy/, or by
// a client with no build of its own) rather than from elsewhere: `hello` reports it.
bool g_from_game_folder = false;
// What the client left for AP saves without it (offline.h), read by the helper's thread when it
// starts and by the pipe thread each time a client goes away, and handed to the next frame.
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
void publish_blockers() {
    g_menu_installed.store(g_game.menu_installed());
    g_items_restyled.store(g_game.ap_items_restyled());
    g_scan_written.store(g_game.scan_written());
    g_branch_patched.store(g_game.keys_branch_patched());
}

// ARCHIPELAGO's callback. The game's own widget code calls it, on the game's thread but outside
// the frame call (so outside the trampoline's count). Unload therefore first takes it off the
// menu on the game's thread, through the mailbox, refuses to unload while it may still be
// there, and waits for a call still inside it. The helper's other functions below are called
// the same way and handled the same way.
void archipelago_chosen(void*, int) {
    InCallback in;
    guarded([] { g_game.start_archipelago(&archipelago_chosen); });
    g_menu_installed.store(g_game.menu_installed());
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

// EQUIPMENT's callback while the disk count is shown (equip.h): the game's own builds the
// screen, then the count is added in the same call, before the screen's first frame.
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

// Game thread: the helper's stand-ins the game's menus are pointed at, given to the game
// object before it is configured (by a client, or from the offline cache).
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
            if (zap::ClientState* state = g_rearm.exchange(nullptr)) {
                std::unique_ptr<zap::ClientState> owned(state);
                g_game.rearm(std::move(*owned));
            }
            if (g_client_gone.exchange(false)) {
                // The client went away: everything it asked for goes, and nothing comes back
                // (the escape skip included) until a client configures the helper again.
                g_game.put_back_all(&archipelago_chosen);
                g_game.clear_popups();
                g_game.set_active(false);
            }
            take_offline();
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
                    g_game.watch_intel();
                    g_game.watch_title();
                    g_game.watch_offline();
                    g_game.watch_map_names();
                    g_game.watch_ap_items();
                    g_game.watch_map_marks();
                }
                publish_blockers();
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
// `word` (the name alone, or the name, a space and anything).
enum class Match { kName, kPrefix, kWord };
struct Request {
    const char* name;
    Match match;
    std::string (*run)(const std::string& request);
};

std::string set_assists(bool lock) {
    return on_game(
        [lock] {
            g_game.set_assists_locked(lock);
            return std::string(lock ? "locked" : "free");
        },
        true);
}

std::string set_intel(bool on) {
    return on_game(
        [on] {
            g_game.set_intel(on);
            return std::string(on ? "intel on" : "intel off");
        },
        true);
}

std::string disk_slots(const std::string& request) {
    // "disk_slots": off. "disk_slots COUNT REQUIRED SLOTS": the file select's disk count for the
    // slots holding the seed (files.h); REQUIRED is checked but not shown (requests.h).
    int count = 0;
    std::set<int> slots;
    std::string error = zap::parse_disk_slots(request, count, slots);
    if (!error.empty()) return "error " + error;
    return on_game(
        [&] {
            bool on = !slots.empty();
            g_game.set_disk_slots(count, std::move(slots));
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
    {"shield", Match::kName,
     [](const std::string&) {
         return on_game([] { return g_game.spawn_shield(GetTickCount64()); }, false);
     }},
    {"die", Match::kName,
     [](const std::string&) {
         return on_game([] { return g_game.kill(GetTickCount64()); }, false);
     }},
    {"assists lock", Match::kName, [](const std::string&) { return set_assists(true); }},
    {"assists free", Match::kName, [](const std::string&) { return set_assists(false); }},
    // The client's session sees a randomizer save in play that is not its seed's (offline.h).
    {"other_save", Match::kName,
     [](const std::string&) {
         return on_game([] { return std::string(g_game.other_save() ? "told" : "already told"); },
                        true);
     }},
    // The client's session sees its seed's save in play: if it was played offline, the player is
    // told the client is back (offline.h).
    {"client_back", Match::kName,
     [](const std::string&) {
         return on_game([] { return std::string(g_game.client_back() ? "shown" : "none"); }, true);
     }},
    // The Area Map named INTEL CHIP, its map reveal off (intel.h), or put back.
    {"intel on", Match::kName, [](const std::string&) { return set_intel(true); }},
    {"intel off", Match::kName, [](const std::string&) { return set_intel(false); }},
    // Offer RETURN HOME WITH THE DISKS? when the game is next idle in play (mission.h).
    {"mission_prompt", Match::kName,
     [](const std::string&) {
         return on_game(
             [] {
                 g_game.request_mission();
                 return std::string("queued");
             },
             true);
     }},
    {"mission_status", Match::kName,
     [](const std::string&) { return on_game([] { return g_game.mission_status(); }, true); }},
    {"disk_slots", Match::kWord, &disk_slots},
    {"discs_status", Match::kName,
     [](const std::string&) { return on_game([] { return g_game.equipment_status(); }, false); }},
    {"discs", Match::kWord, &discs},
    {"ap_labels", Match::kPrefix, &ap_labels},
    {"ap_newgame", Match::kPrefix, &ap_newgame},
    {"disarm", Match::kName,
     [](const std::string&) {
         return on_game(
             [] {
                 g_game.disarm_newgame();
                 return std::string("disarmed");
             },
             false);
     }},
    {"newgame_status", Match::kName,
     [](const std::string&) { return on_game([] { return g_game.newgame_status(); }, false); }},
    // The client's popups not shown yet (from `apply`; the helper's own notices left out).
    {"popups", Match::kName,
     [](const std::string&) {
         return on_game([] { return "pending " + std::to_string(g_game.pending_popups()); },
                        false);
     }},
    {"config", Match::kPrefix, &config},
    {"apply", Match::kPrefix, &apply},
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
        // What the client leaves for AP saves without it (offline.h), written from here so it
        // lands where the game reads it.
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
        return r.run(request);
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
                publish_blockers();
                return std::string();
            },
            kFrameWaitMs, ignored);
    }
    auto refuse = [&](const std::string& why) {
        set_unloading(false);
        // Set again by the next frame, however long it takes to come (a call through the mailbox
        // could be withdrawn for want of one, and the client would never resend it). One left
        // from an earlier refusal was taken already: a frame ran the snapshot after it.
        if (saved) delete g_rearm.exchange(new zap::ClientState(std::move(state)));
        return why;
    };
    if (set_unloading(true)) {
        return refuse("a menu still holds the helper's callback (no frame came)");
    }
    if (g_items_restyled.load()) return refuse("an item record is still restyled (no frame came)");
    if (g_scan_written.load()) {
        return refuse("the scanner's message is still the helper's (no frame came)");
    }
    if (g_branch_patched.load()) {
        return refuse("the pause screen's branch is still the helper's (no frame came)");
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
