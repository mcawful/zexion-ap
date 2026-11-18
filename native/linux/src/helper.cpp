// libzexion_ap.so: the Linux helper, preloaded into Zexion's Linux build (native/README.md,
// "The Linux helper"). In any other program, or another build, it does nothing but pass
// SDL_GL_SwapWindow on.

#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core.h"
#include "addresses.h"   // GameAddresses (native/src)
#include "game_calls.h"  // Game: the parts the Linux helper runs (new games, the choice, grants)
#include "game_mem.h"    // in_image_code
#include "mailbox.h"
#include "requests.h"  // parse_newgame, parse_batch
#include "trap.h"      // is_trap

namespace {

using namespace zap::linux_helper;

constexpr int kClientSilenceSeconds = 30;  // a client that sends nothing this long is dropped

using Swap = int (*)(void*);
Swap g_next_swap = nullptr;  // the SDL_GL_SwapWindow after ours, set by the constructor
std::atomic<bool> g_active{false};
std::atomic<uint64_t> g_frames{0};

// The frame calls by call site (return address), claimed lock-free on a site's first call; calls
// from sites past kCallSites are only counted together.
constexpr int kCallSites = 8;
std::atomic<uintptr_t> g_site[kCallSites]{};
std::atomic<uint64_t> g_site_calls[kCallSites]{};
std::atomic<uint64_t> g_other_calls{0};

void count_call_site(uintptr_t site) {
    for (int i = 0; i < kCallSites; ++i) {
        uintptr_t held = g_site[i].load(std::memory_order_acquire);
        if (held == 0) {
            uintptr_t empty = 0;
            if (g_site[i].compare_exchange_strong(empty, site, std::memory_order_acq_rel)) {
                held = site;
            } else {
                held = empty;  // another thread claimed it first: this site or another
            }
        }
        if (held == site) {
            g_site_calls[i].fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    g_other_calls.fetch_add(1, std::memory_order_relaxed);
}
const char* g_build = nullptr;
bool g_folder = false;

// The zero-filled part of the main program's writable segments (PT_LOAD with PF_W, from
// p_filesz to p_memsz: its .bss), as [start, end): the only memory a `write` may change. Not
// its code, nor its initialized data: .data's function pointers and .got.plt's import slots,
// which the game calls through, stay the game's.
constexpr int kMaxSegments = 4;
uintptr_t g_writable[kMaxSegments][2] = {};
int g_writable_count = 0;

// The game's thread's work (native/src/mailbox.h), and the Windows helper's Game, configured by
// a client's `config` (native/src/game_calls.h). Of Game, only the parts ported run here: new
// games and the ARCHIPELAGO choice, `apply` and its popups (native/README.md, "The Linux
// helper"); the rest's addresses stay null and nothing calls them.
constexpr DWORD kFrameWaitMs = 1000;  // as the Windows helper's
zap::Mailbox g_mailbox;
zap::Game g_game;  // game thread only
std::atomic<bool> g_client_left{false};  // a client went: its plan is dropped at the next frame

// The game's own function that swaps its window once a frame (docs/re/linux.md, "The per-frame
// call"): frame work runs only for its calls, by their return address. In the test build, the
// stand-in game's (tests/fake_game.cpp).
#ifdef ZAP_HOOKTEST
constexpr char kFrameFunction[] = "draw_frame";
#else
constexpr char kFrameFunction[] = "_Z13SWHardwareEndP14SpriteWorldRec";
#endif
zap::linux_helper::SymbolSpan g_frame_function;  // from the game's own symbols at start-up

// The main program's GNU build id, and its writable segments: dl_iterate_phdr lists it first.
int main_program_build_id(dl_phdr_info* info, size_t, void* data) {
    auto* out = static_cast<std::optional<std::string>*>(data);
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_LOAD && (ph.p_flags & PF_W) && g_writable_count < kMaxSegments) {
            uintptr_t start = info->dlpi_addr + ph.p_vaddr;
            if (ph.p_memsz > ph.p_filesz) {
                g_writable[g_writable_count][0] = start + ph.p_filesz;
                g_writable[g_writable_count][1] = start + ph.p_memsz;
                ++g_writable_count;
            }
        }
        if (ph.p_type != PT_NOTE || *out) continue;
        auto* notes = reinterpret_cast<const unsigned char*>(info->dlpi_addr + ph.p_vaddr);
        *out = note_build_id(notes, ph.p_memsz);
    }
    return 1;  // the first object only
}

std::string folder_of(const char* path) {
    char resolved[PATH_MAX];
    if (path == nullptr || realpath(path, resolved) == nullptr) return std::string();
    std::string text(resolved);
    size_t slash = text.rfind('/');
    return slash == std::string::npos ? std::string() : text.substr(0, slash);
}

std::string own_folder() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&main_program_build_id), &info) == 0) return std::string();
    return folder_of(info.dli_fname);
}

[[maybe_unused]] bool looks_like_zexion() {  // unused in the hooktest build
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return false;
    exe[n] = '\0';
    const char* name = std::strrchr(exe, '/');
    return std::strcmp(name ? name + 1 : exe, "Zexion") == 0;
}

// This process's memory, copied by the kernel: an address not mapped is EFAULT, not a fault.
bool read_own(uint64_t address, size_t size, std::string& out) {
    out.assign(size, '\0');
    iovec local{out.data(), size};
    iovec remote{reinterpret_cast<void*>(static_cast<uintptr_t>(address)), size};
    ssize_t n = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    if (n == static_cast<ssize_t>(size)) return true;
    out = n < 0 ? std::string(std::strerror(errno)) : "read " + std::to_string(n) + " of " +
                                                           std::to_string(size) + " bytes";
    for (char& c : out) {
        if (c == ' ') c = '_';  // one word, as the Windows helper's errors' fields
    }
    return false;
}

// Whether [address, address + size) lies in the game binary's .bss (g_writable).
bool in_game_data(uint64_t address, size_t size) {
    for (int i = 0; i < g_writable_count; ++i) {
        if (address >= g_writable[i][0] && address + size >= address &&
            address + size <= g_writable[i][1]) {
            return true;
        }
    }
    return false;
}

// Into this process's memory, by the kernel (a page not writable is EFAULT, not a fault), and
// only inside the main program's .bss (g_writable).
bool write_own(uint64_t address, const std::string& data, std::string& out) {
    if (!in_game_data(address, data.size())) {
        out = "not_in_the_game's_writable_data";
        return false;
    }
    iovec local{const_cast<char*>(data.data()), data.size()};
    iovec remote{reinterpret_cast<void*>(static_cast<uintptr_t>(address)), data.size()};
    ssize_t n = process_vm_writev(getpid(), &local, 1, &remote, 1, 0);
    if (n == static_cast<ssize_t>(data.size())) return true;
    out = n < 0 ? std::string(std::strerror(errno)) : "wrote " + std::to_string(n) + " of " +
                                                           std::to_string(data.size()) + " bytes";
    for (char& c : out) {
        if (c == ' ') c = '_';
    }
    return false;
}

State state() {
    State s;
    s.build = g_build;
    s.hooked = g_active.load();
    s.folder = g_folder;
    s.frames = g_frames.load();
    for (int i = 0; i < kCallSites; ++i) {
        uintptr_t site = g_site[i].load(std::memory_order_acquire);
        if (site != 0) s.callers.emplace_back(site, g_site_calls[i].load());
    }
    s.other_callers = g_other_calls.load();
    return s;
}

// `config name=ADDRESS ...`: the addresses of the parts Game runs here, the game's own by its
// symbols (the client resolves them: client/linux_targets.py). Each data one must lie in the
// game's .bss at its full size (and `before` bytes before it) and each function in its code; all
// of them, or nothing changes.
struct ConfigKey {
    const char* name;
    BYTE* zap::GameAddresses::*field;
    size_t size;         // 0: a function
    const char* symbol;  // where it is: this symbol of the game's own, plus `offset`
    uint64_t offset;
    uint64_t before = 0;  // bytes before it that must lie in the game's .bss too
};
// The player state around the inventory, as Game reads it (game_calls.cpp, kInventoryInPlayer
// and kPlayerStateSize); a message's last pointer and its sequence (kLastSequence).
constexpr uint64_t kInventoryInPlayer = 0x108E, kPlayerStateSize = 0x1110;
constexpr size_t kLastMessage = 12;
constexpr size_t kHdMessages = zap::hd::kSlots * zap::hd::kSize;
constexpr size_t kMessageSlots = zap::hd::kSlots * zap::message::kSize;
// Keep in step with client/linux_targets.py (TARGETS and HELPER_TARGETS, by CONFIG_KEYS);
// tests/test_linux_targets.py checks them. The test build's are the stand-in game's.
using G = zap::GameAddresses;
#ifndef ZAP_HOOKTEST
constexpr ConfigKey kConfigKeys[] = {
    {"rando_blob", &G::rando_blob, zap::kSectionSize, "gCurrentRando", 0},
    {"randomizer_menu", &G::randomizer_menu, zap::kMenuSize, "gRandoPreferences", 0},
    {"transition_fn", &G::transition_fn, sizeof(void*), "gExitActionP", 0},
    {"transition_timer", &G::transition_timer, sizeof(int32_t), "gExitDelay", 0},
    {"newgame_fn", &G::newgame_fn, 0, "_Z16StartNewGameExitv", 0},
    {"placement_fn", &G::placement_fn, 0, "_Z13generateRandov", 0},
    {"rando_marker", &G::rando_marker, sizeof(uint16_t), "gGameState", 0x13738},
    {"current_slot", &G::current_slot, sizeof(int32_t), "gCurrentSaveGame", 0},
    {"start_count", &G::start_count, sizeof(int16_t), "rStartingLocationCount", 0},
    {"start_cells", &G::start_cells, 4 * zap::kStartTableMax, "rStartingLocations", 0},
    {"start_flags", &G::start_flags, zap::kStartTableMax, "rStartingLocationCage", 0},
    {"current_menu", &G::current_menu, sizeof(void*), "gLastGameMenuP", 0},
    {"rando_menu_fn", &G::rando_menu_fn, 0, "_Z14RandomizerMenuv", 0},
    {"widget_handler_fn", &G::widget_handler_fn, 0, "_Z18StandardMenuActionP12GameMenuItemi", 0},
    {"panel_handler_fn", &G::panel_handler_fn, 0, "_Z16FrameWrapOrArrowP12GameMenuItemi", 0},
    {"rando_newgame_fn", &G::rando_newgame_fn, 0, "_Z9forceSeedv", 0},
    {"tr_flag", &G::tr_flag, 1, "gCurrentTranslation", 0x91},
    // Grants and their popups (Game::apply, pump_popups). The inventory: the player state's
    // (gGameState+0x1CD50) at kInventoryInPlayer.
    {"inventory", &G::inventory, kPlayerStateSize - kInventoryInPlayer, "gGameState", 0x1DDDE,
     kInventoryInPlayer},
    {"save_loaded", &G::save_loaded, 1, "gGameState", 0xACA},
    {"menu_state", &G::menu_state, 1, "gCursorMode", 0},
    {"item_records", &G::item_records, sizeof(void*), "gPowerUpDefs", 0},
    {"last_message", &G::last_message, kLastMessage, "gGameState", 0x13350},
    {"weapon_strip_gate", &G::weapon_strip_gate, 1, "gPreferences", 0x1DD},
    {"sound_system", &G::sound_system, 1, "gGameSoundManager", 0},
    {"message_fn", &G::message_fn, 0, "_Z17createGameMessagePKciiiiii", 0},
    {"sound_fn", &G::sound_fn, 0, "_Z13PlayGameSoundP16GameSoundManagersffffss", 0},
    {"hud_number_fn", &G::hud_number_fn, 0, "_Z19updateNumberDisplayP13NumberDisplaydb", 0},
    {"hud_layout_fn", &G::hud_layout_fn, 0, "_Z29updateNumberDisplayVisibilityP12PlayerStruct",
     0},
    {"energy_hud_fn", &G::energy_hud_fn, 0, "_Z15updateEnergyBarP12PlayerStructb", 0},
    {"weapon_strip_fn", &G::weapon_strip_fn, 0, "_Z19updateWeaponDisplayP12PlayerStruct", 0},
    {"suit_color_fn", &G::suit_color_fn, 0, "_Z16setSpritePalettehs", 0},
    // The translated marks a popup's message slot may hold (translation.cpp, clear_mark).
    {"hd_messages", &G::hd_messages, kHdMessages, "gTranslationGameMessages", 0},
    {"message_slots", &G::message_slots, kMessageSlots, "gGameState", 0x8C6B48},
};
#else
constexpr ConfigKey kConfigKeys[] = {
    {"rando_blob", &G::rando_blob, zap::kSectionSize, "zap_section", 0},
    {"randomizer_menu", &G::randomizer_menu, zap::kMenuSize, "zap_menu", 0},
    {"transition_fn", &G::transition_fn, sizeof(void*), "zap_transition", 0},
    {"transition_timer", &G::transition_timer, sizeof(int32_t), "zap_timer", 0},
    {"newgame_fn", &G::newgame_fn, 0, "zap_new_game", 0},
    {"placement_fn", &G::placement_fn, 0, "zap_placement", 0},
    {"rando_marker", &G::rando_marker, sizeof(uint16_t), "zap_marker", 0},
    {"current_slot", &G::current_slot, sizeof(int32_t), "zap_slot", 0},
    {"start_count", &G::start_count, sizeof(int16_t), "zap_start_count", 0},
    {"start_cells", &G::start_cells, 4 * zap::kStartTableMax, "zap_start_cells", 0},
    {"start_flags", &G::start_flags, zap::kStartTableMax, "zap_start_flags", 0},
    {"current_menu", &G::current_menu, sizeof(void*), "zap_menu_top", 0},
    {"rando_menu_fn", &G::rando_menu_fn, 0, "zap_rando_menu", 0},
    {"widget_handler_fn", &G::widget_handler_fn, 0, "zap_widget_handler", 0},
    {"panel_handler_fn", &G::panel_handler_fn, 0, "zap_panel_handler", 0},
    {"rando_newgame_fn", &G::rando_newgame_fn, 0, "zap_force_seed", 0},
    {"tr_flag", &G::tr_flag, 1, "zap_tr_flag", 0},
    {"inventory", &G::inventory, kPlayerStateSize - kInventoryInPlayer, "zap_player",
     kInventoryInPlayer, kInventoryInPlayer},
    {"save_loaded", &G::save_loaded, 1, "zap_save_loaded", 0},
    {"menu_state", &G::menu_state, 1, "zap_cursor", 0},
    {"item_records", &G::item_records, sizeof(void*), "zap_records", 0},
    {"last_message", &G::last_message, kLastMessage, "zap_last_message", 0},
    {"weapon_strip_gate", &G::weapon_strip_gate, 1, "zap_strip_gate", 0},
    {"sound_system", &G::sound_system, 1, "zap_sounds", 0},
    {"message_fn", &G::message_fn, 0, "zap_message", 0},
    {"sound_fn", &G::sound_fn, 0, "zap_sound", 0},
    {"hud_number_fn", &G::hud_number_fn, 0, "zap_hud_number", 0},
    {"hud_layout_fn", &G::hud_layout_fn, 0, "zap_hud_layout", 0},
    {"energy_hud_fn", &G::energy_hud_fn, 0, "zap_energy_hud", 0},
    {"weapon_strip_fn", &G::weapon_strip_fn, 0, "zap_weapon_strip", 0},
    {"suit_color_fn", &G::suit_color_fn, 0, "zap_suit_color", 0},
    {"hd_messages", &G::hd_messages, kHdMessages, "zap_hd_messages", 0},
    {"message_slots", &G::message_slots, kMessageSlots, "zap_message_slots", 0},
};
#endif
constexpr size_t kConfigKeyCount = sizeof(kConfigKeys) / sizeof(kConfigKeys[0]);

// Each key's address by the game's own symbols (kConfigKeys), read from its binary at start-up:
// a config must give these and no others, so a client whose mapping drifted changes nothing.
uint64_t g_expected[kConfigKeyCount] = {};
bool g_expected_known = false;

// The game binary's symbols the helper uses, from /proc/self/exe: the config keys' and the frame
// function's. False, nothing set, if any is missing.
bool read_own_symbols() {
    std::ifstream file("/proc/self/exe", std::ios::binary);
    std::string elf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::string> names{kFrameFunction};
    for (const ConfigKey& key : kConfigKeys) names.emplace_back(key.symbol);
    auto found = zap::linux_helper::find_symbols(elf, names);
    for (const std::string& name : names) {
        if (found.find(name) == found.end()) return false;
    }
    g_frame_function = found[kFrameFunction];
    for (size_t i = 0; i < kConfigKeyCount; ++i) {
        g_expected[i] = found[kConfigKeys[i].symbol].value + kConfigKeys[i].offset;
    }
    g_expected_known = true;
    return true;
}

std::string parse_config(std::string_view request, zap::GameAddresses& out) {
    std::map<std::string, uint64_t> given;
    std::istringstream words{std::string(request.substr(std::string_view("config").size()))};
    std::string word;
    while (words >> word) {
        size_t eq = word.find('=');
        if (eq == std::string::npos) return "config takes name=address pairs";
        char* end = nullptr;
        uint64_t address = std::strtoull(word.c_str() + eq + 1, &end, 0);
        if (end == nullptr || *end != '\0' || address == 0) return "a bad address for " + word;
        given[word.substr(0, eq)] = address;  // a name the helper doesn't use: ignored
    }
    if (!g_expected_known) return "the game's symbols were not read";
    for (size_t i = 0; i < kConfigKeyCount; ++i) {
        const ConfigKey& key = kConfigKeys[i];
        auto found = given.find(key.name);
        if (found == given.end()) return std::string("missing ") + key.name;
        if (found->second != g_expected[i]) {
            return std::string(key.name) + " is not where the game's symbols put it";
        }
        auto* at = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(found->second));
        bool ok = key.size == 0 ? zap::in_image_code(at)
                                : found->second >= key.before &&
                                      in_game_data(found->second - key.before,
                                                   key.before + key.size);
        if (!ok) {
            return std::string(key.name) +
                   (key.size == 0 ? " is not in the game's code" : " is not in the game's data");
        }
        out.*key.field = at;
    }
    return "";
}

// ARCHIPELAGO's callback: the game calls it with no arguments (docs/re/linux.md, "Mapped by
// disassembly: menus and UI"), on its thread; the parameters are WidgetCallback's and unused.
void archipelago_chosen(void*, int) {
    try {
        g_game.start_archipelago(&archipelago_chosen);  // nothing unless configured
    } catch (...) {
    }
}

// The game-mode panel's handler while the choice is up: the game's own, then the hover border
// fitted around ARCHIPELAGO in the frame the game laid it out (choice.h, fit_border).
void panel_hook(void* widget, int event) {
    reinterpret_cast<void (*)(void*, int)>(g_game.panel_handler())(widget, event);
    try {
        g_game.fit_archipelago_border();
    } catch (...) {
    }
}

// `work` run on the game's thread at its next frame call; "refused not configured" first when
// `needs_config` and no config came; "refused no frame" if no frame took it in time (it then
// never runs); "error <why>" at once for a request not parsed. As the Windows helper's on_game.
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

// The requests that act on the game (native/README.md, "The Linux helper"), or nothing for one
// core.cpp answers.
std::optional<std::string> game_request(std::string_view request) {
    if (request == "config" || request.starts_with("config ")) {
        zap::GameAddresses found;
        std::string error = parse_config(request, found);
        return on_game(
            [&] {
                // A config's addresses are always the game's own (parse_config), so one after
                // the first changes nothing the choice or the popups hold.
                g_game.set_hooks({.panel = reinterpret_cast<void*>(&panel_hook)});
                g_game.configure(found);
                // The translation's font for popups is not ported (4-byte characters): pixel
                // text, the slot's translated mark cleared (translation.cpp, clear_mark's Linux
                // test of the flag alone), as the Windows helper's fallback.
                g_game.hold_translated_popups();
                return std::string("ok");
            },
            false, error);
    }
    if (request.starts_with("ap_newgame\n") || request == "ap_newgame") {
        zap::NewGamePlan plan;
        std::string error = zap::parse_newgame(std::string(request), plan);
        return on_game(
            [&] {
                return std::string(g_game.arm_newgame(std::move(plan)) ? "armed"
                                                                       : "refused not configured");
            },
            true, error);
    }
    if (request == "disarm") {
        return on_game([] { g_game.disarm_newgame(); return std::string("disarmed"); }, false);
    }
    if (request == "newgame_status") {
        return on_game([] { return g_game.newgame_status(); }, false);
    }
    if (request.starts_with("apply\n") || request == "apply") {
        zap::Batch batch;
        std::string error = zap::parse_batch(std::string(request), batch);
        return on_game(
            [&] {
                // A received trap's popup holds the popups behind it until the trap goes off,
                // which is not ported here: none is taken (the client sends none: session.py).
                for (const zap::Popup& popup : batch.popups) {
                    if (!popup.bare && zap::trap::is_trap(popup.item)) {
                        return std::string("refused traps are not ported to the Linux build");
                    }
                }
                return g_game.apply(batch);
            },
            true, error);
    }
    return std::nullopt;
}

// One client: a request a message, a reply a message, until it goes, sends too much, or says
// nothing for kClientSilenceSeconds.
void serve_client(int client) {
    timeval silence{kClientSilenceSeconds, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &silence, sizeof silence);
    std::vector<char> buffer(kMaxRequest);
    for (;;) {
        ssize_t n = recv(client, buffer.data(), buffer.size(), MSG_TRUNC);
        if (n <= 0 || static_cast<size_t>(n) > buffer.size()) return;
        std::string_view request(buffer.data(), static_cast<size_t>(n));
        std::optional<std::string> game = game_request(request);
        std::string answer = game ? *game : reply(request, state(), read_own, write_own);
        if (send(client, answer.data(), answer.size(), MSG_NOSIGNAL) < 0) return;
    }
}

void* serve(void* arg) {
    int listener = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    for (;;) {
        int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno != EINTR && errno != ECONNABORTED) sleep(1);
            continue;
        }
        ucred cred{};
        socklen_t size = sizeof cred;
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &size) == 0 &&
            cred.uid == geteuid()) {
            serve_client(client);
            g_client_left = true;  // as the Windows helper does, its plan is not kept for it
        }
        close(client);
    }
    return nullptr;
}

// The socket, bound and listening, or -1 (said why on stderr).
int listen_socket() {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::string name = socket_name(static_cast<long>(getpid()));
    std::memcpy(address.sun_path + 1, name.data(), name.size());  // abstract: a NUL first
    auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size());
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), length) != 0 || listen(fd, 1) != 0) {
        std::fprintf(stderr, "zexion-ap %s: socket %s not opened: %s\n", kVersion, name.c_str(),
                     std::strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

// The serving thread, with every signal blocked from its first instruction: the game's signals
// stay the game's threads'.
bool start_thread(int fd) {
    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_SETMASK, &all, &old);
    pthread_t thread;
    int failed = pthread_create(&thread, nullptr, serve,
                                reinterpret_cast<void*>(static_cast<intptr_t>(fd)));
    pthread_sigmask(SIG_SETMASK, &old, nullptr);
    if (failed != 0) return false;
    pthread_detach(thread);
    return true;
}

__attribute__((constructor)) void start() {
    // Every library the program needs is mapped before any constructor runs.
    g_next_swap = reinterpret_cast<Swap>(dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
    std::string folder = own_folder();
    if (!folder.empty() && access((folder + "/" + kDisabledName).c_str(), F_OK) == 0) return;
    std::optional<std::string> id;
    dl_iterate_phdr(main_program_build_id, &id);
#ifdef ZAP_HOOKTEST
    (void)id;
    g_build = "test";  // tests/test_native_linux.py's stand-in game: any program
#else
    g_build = id ? identify_build(*id) : nullptr;
    if (g_build == nullptr) {
        if (looks_like_zexion()) {
            std::fprintf(stderr, "zexion-ap %s: not a known build of Zexion (build id %s)\n",
                         kVersion, id ? id->c_str() : "none");
        }
        return;
    }
#endif
    if (g_next_swap == nullptr) {
        std::fprintf(stderr, "zexion-ap %s: no SDL_GL_SwapWindow after the helper's\n", kVersion);
    }
    g_folder = !folder.empty() && folder == folder_of("/proc/self/exe");
    if (!read_own_symbols()) {
        std::fprintf(stderr, "zexion-ap %s: the game's symbols were not found: no new games\n",
                     kVersion);
    }
    int fd = listen_socket();
    if (fd < 0) return;
    if (!start_thread(fd)) {
        close(fd);
        return;
    }
    g_active = true;
    std::fprintf(stderr, "zexion-ap %s: serving build %s\n", kVersion, g_build);
}

}  // namespace

namespace {

// The game's thread, at its frame swap (SWHardwareEnd's call: `site`, the return address): the
// mailbox's work, a plan dropped for a client gone, an armed new game watched, the ARCHIPELAGO
// choice, then the next popup. Each part guarded on its own, so one that throws keeps none of
// the others from running, and nothing reaches the game. A fault in the game's own placement is
// not an exception: it stays the game's crash.
void game_frame(uintptr_t site) {
    if (site < g_frame_function.value || site >= g_frame_function.value + g_frame_function.size) {
        return;  // another swap (UpdatePalette's window), or the frame function unknown
    }
    auto guarded = [](auto&& work) {
        try {
            work();
        } catch (...) {
        }
    };
    guarded([] { g_mailbox.serve(); });
    // A client's popups stay queued for its save, as on Windows; only its plan goes.
    if (g_client_left.exchange(false)) guarded([] { g_game.disarm_newgame(); });
    if (!g_game.configured()) return;
    try {
        g_game.watch_newgame();
    } catch (...) {
        guarded([] { g_game.refuse_newgame("the placement failed"); });
    }
    // The ARCHIPELAGO choice while a plan offers it, RANDOMIZER back otherwise.
    guarded([] { g_game.watch_menu(&archipelago_chosen); });
    guarded([] { g_game.pump_popups(); });
}

}  // namespace

// The game's per-frame call (native/README.md, "The Linux helper"). SDL2's returns nothing and
// SDL3's a bool: returned as an int, which an SDL2 caller ignores.
extern "C" __attribute__((visibility("default"))) int SDL_GL_SwapWindow(void* window) {
    if (g_active.load(std::memory_order_relaxed)) {
        g_frames.fetch_add(1, std::memory_order_relaxed);
        auto site = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        count_call_site(site);
        game_frame(site);
    }
    Swap next = g_next_swap;
    if (next == nullptr) {  // SDL loaded after the constructor ran: look once more
        next = reinterpret_cast<Swap>(dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
        if (next == nullptr) return 0;
    }
    return next(window);
}
