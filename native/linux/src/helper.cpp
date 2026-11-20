// libzexion_ap.so: the Linux helper, preloaded into Zexion's Linux build (native/README.md,
// "The Linux helper"). In any other program, or another build, it does nothing but pass
// SDL_GL_SwapWindow on.

#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>
#include <unwind.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "core.h"
#include "addresses.h"   // GameAddresses (native/src)
#include "game_calls.h"  // Game: the parts the Linux helper runs (below)
#include "game_mem.h"    // in_image_code
#include "mailbox.h"
#include "requests.h"  // parse_newgame, parse_batch
#include "offline.h"  // put_offline_file: a seed's own traps and disks

namespace {

using namespace zap::linux_helper;

constexpr int kClientSilenceSeconds = 30;  // a client that sends nothing this long is dropped
constexpr int kAuthSeconds = 5;  // a connection that has not shown the token by then is dropped

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
std::atomic<uint64_t> g_frame_calls{0};  // the calls from the frame function (SWHardwareEnd)

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
// games and the ARCHIPELAGO choice, `apply` and its popups, other players' items, this world's
// own traps and disks and the received ones, and the rest native/README.md's "The Linux helper"
// lists; the rest's addresses stay null and nothing calls them.
constexpr DWORD kFrameWaitMs = 1000;  // as the Windows helper's
// A global the helper never destroys: it is never unloaded, and at the game's exit the serving
// thread may still be using it (a destructor run by exit() on the game thread would pull it from
// under that thread). Built in place by its own constructor; used through -> and *.
template <class T>
class Kept {
public:
    Kept() { ::new (static_cast<void*>(bytes_)) T(); }
    Kept(const Kept&) = delete;
    Kept& operator=(const Kept&) = delete;
    T* operator->() { return std::launder(reinterpret_cast<T*>(bytes_)); }
    T& operator*() { return *operator->(); }

private:
    alignas(T) unsigned char bytes_[sizeof(T)];
};
static_assert(std::is_trivially_destructible_v<Kept<std::string>>, "nothing run at exit");

// Where `write` may change the game, inside that .bss: what the client writes and no more
// (core.h, client_write_ranges), from the game's own symbols, set once before the serving thread
// starts. The game calls procedures through pointers all over its .bss (the exit action and its
// table, the move, area-event, enemy-action and sprite-draw procedures, each sprite record's own:
// docs/re/linux.md), so a list of those to leave alone could never be whole; this list of what the
// client needs is short. Empty: nothing is written.
__attribute__((init_priority(101))) Kept<std::vector<Range>> g_client_writes;

// Both constructed before the helper's own constructor (start) runs, which sets the game's title
// version and starts the serving thread: a constructor function runs, in the library's
// .init_array, before this file's ordinary dynamic initializers, which would otherwise build
// these afresh over what start set (init_priority puts theirs first).
__attribute__((init_priority(101))) Kept<zap::Mailbox> g_mailbox;
__attribute__((init_priority(101))) Kept<zap::Game> g_game;  // game thread only
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

// The game's count of sprite definitions: the translations' icon sheets are definitions count + 5
// and + 8 (loadSprites: docs/re/linux.md, "Mapped by disassembly: the helper's translated text"),
// 101 and 104 as the helper paints them only with 96 (apitem.h, kHdIconSheet). In the test build,
// the stand-in game's.
#ifdef ZAP_HOOKTEST
constexpr char kSpriteDefCount[] = "zap_sprite_def_count";
#else
constexpr char kSpriteDefCount[] = "gSpriteDefCount";
#endif
constexpr int16_t kTracedSpriteDefs = zap::kHdIconSheet - 5;
uint64_t g_sprite_def_count = 0;

// The game's pointer to the player in play: the helper configures itself, and runs with no client
// configured, only while it points at the player state the config's `inventory` lies in, as the
// client checks before it attaches (client/game.py; docs/re/linux.md, "Read from the running
// game"). In the test build, the stand-in game's.
#ifdef ZAP_HOOKTEST
constexpr char kPlayerPointer[] = "zap_main_character";
#else
constexpr char kPlayerPointer[] = "gMainCharacterSpriteP";
#endif
uint64_t g_player_pointer = 0;

// SteamUserStats()'s context, the one the Feats guard's export answers for (feats.h,
// feats_context_init). In the test build, the stand-in game's.
#ifdef ZAP_HOOKTEST
constexpr char kStatsContext[] = "zap_stats_context";
constexpr char kSetAchievement[] = "zap_set_the_achievement";
#else
constexpr char kStatsContext[] = "_ZZ14SteamUserStatsvE27s_CallbackCounterAndContext";
constexpr char kSetAchievement[] = "_Z17setTheAchievementi";  // the ending's call's target
#endif

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
// only inside the main program's .bss (g_writable) and what the client writes there
// (g_client_writes).
bool write_own(uint64_t address, const std::string& data, std::string& out) {
    if (!in_game_data(address, data.size())) {
        out = "not_in_the_game's_writable_data";
        return false;
    }
    if (!within_ranges(*g_client_writes, address, data.size())) {
        out = "not_in_what_the_client_writes";
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
// The item and general object pools (looks.h), the palette table's 65 entries and the event
// handlers' 48 (docs/re/linux.md).
constexpr size_t kItemPool = zap::ItemLooks::kSlots * zap::obj::kItemSize;
constexpr size_t kObjectPool = zap::ItemLooks::kObjectSlots * zap::obj::kGeneralSize;
constexpr size_t kPaletteTable = zap::sprite::kPaletteCount * sizeof(void*),
                 kEventHandlers = 48 * sizeof(void*);
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
    // The translations (translation.h): the object, its tables' pointers, the counts, the
    // message's and the label's builders (docs/re/linux.md, "Mapped by disassembly:
    // translations"). The label's string is the helper's own on Linux: no wstring_new_fn.
    {"translation", &G::translation, zap::tr::kSize, "gCurrentTranslation", 0},
    {"small_text", &G::small_text, sizeof(void*), "gCurrentTranslation", 0x30},
    {"small_offsets", &G::small_offsets, sizeof(void*), "gCurrentTranslation", 0x58},
    {"item_names", &G::item_names, sizeof(void*), "gCurrentTranslation", 0x48},
    {"item_name_offsets", &G::item_name_offsets, sizeof(void*), "gCurrentTranslation", 0x78},
    {"small_count", &G::small_count, sizeof(int16_t), "gSmallTextCount", 0},
    {"item_name_count", &G::item_name_count, sizeof(int16_t), "gPowerUpDefCount", 0},
    {"hd_text_fn", &G::hd_text_fn, 0, "_Z28createTranslationGameMessagePKwiiiiiissP9SpriteRec", 0},
    {"hd_build_fn", &G::hd_build_fn, 0,
     "_Z12generateTextffNSt7__cxx1112basic_stringIwSt11char_traitsIwESaIwEEEP8GameFontP12SpriteStringb",
     0},
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
    // Other players' items (Game::watch_ap_items: apitem.h, looks.h). The popup's width is the
    // immediate of `mov esi, 24` in the pickup (docs/re/linux.md, `popup_width_code`).
    {"sprite_texture", &G::sprite_texture, sizeof(void*), "gSpriteWorldP", 0},
    {"popup_width_code", &G::popup_width_code, 0,
     "_Z17playerCollideProcP10CollideRecS0_P6dPointc", 0x251E},
    {"item_pool", &G::item_pool, kItemPool, "gGameState", 0x79E990},
    {"palette_table", &G::palette_table, kPaletteTable, "gSpriteDrawProcs", 0},
    {"frame_clock", &G::frame_clock, sizeof(int64_t), "gGameState", 0xB0},
    {"sprite_templates", &G::sprite_templates, sizeof(void*), "gMasterSpritesP", 0},
    {"object_pool", &G::object_pool, kObjectPool, "gGameState", 0x388DA0},
    {"flags", &G::flags, zap::kFlagCount, "gGameState", 0xED},
    {"map_grid", &G::map_grid, zap::kMapCells, "gGameState", 0x14FFD},
    {"event_table", &G::event_table, sizeof(void*), "gAreaEvents", 0},
    {"event_count", &G::event_count, sizeof(int16_t), "gAreaEventCount", 0},
    {"event_handlers", &G::event_handlers, kEventHandlers, "gAreaEventProcs", 0},
    // Traps (Game::pump_traps, run_trap; trap.h): AILMENTS, its two tests in weaponCool that the
    // trap's patches skip (docs/re/linux.md, `ailments_hit_code`, `ailments_wave_code`), and the
    // game's halt byte.
    {"ailments", &G::ailments, sizeof(int16_t), "gGameState", 0x13724},
    {"ailments_hit_code", &G::ailments_hit_code, 0, "_Z10weaponCoolP12PlayerStruct", 0xAD5},
    {"ailments_wave_code", &G::ailments_wave_code, 0, "_Z10weaponCoolP12PlayerStruct", 0xB65},
    {"halt_flag", &G::halt_flag, 1, "gEditorMode", 0},
    // The map (mapmarks.h): the item update's two tests of the ID the seen-marker patch spans
    // (docs/re/linux.md, `seen_marker_code`), the map's move procedure's slot (gMoveProcs entry
    // 120: `map_handler`), and the function that sets a text's glyphs (`text_set_fn`).
    {"seen_marker_code", &G::seen_marker_code, 0, "_Z15PowerUpMoveProcP9SpriteRec", 0x788},
    {"map_handler", &G::map_handler, sizeof(void*), "gMoveProcs", 0x3C0},
    {"text_set_fn", &G::text_set_fn, 0, "_Z17alterSpriteStringP9SpriteRecPKcii", 0},
    // The OS Install Disks' count (equip.h, pause.h, scan.h): the scanner's Text entries, the
    // equipment screen's builder, the Energy Tank panel's handler and the menu functions the
    // disk's widget is made with, the pause screen's KEYS branch and its way back from OPTION
    // MENU. No allocator slots: the game's malloc and free are the helper's (equip.cpp).
    {"text_table", &G::text_table, sizeof(void*), "gSmallTexts", 0},
    {"equipment_fn", &G::equipment_fn, 0, "_Z13EquipmentMenuv", 0},
    {"equip_panel_fn", &G::equip_panel_fn, 0, "_Z14EquipmentFrameP12GameMenuItemi", 0},
    {"widget_move_fn", &G::widget_move_fn, 0, "_Z16moveGameMenuItemP9SpriteReciib", 0},
    {"text_place_fn", &G::text_place_fn, 0, "_Z16moveSpriteStringP9SpriteRecii", 0},
    {"widget_new_fn", &G::widget_new_fn, 0,
     "_Z18createGameMenuItemPKciP8GameMenuPFvP12GameMenuItemiEPFvvEPvS9_iP5sRect", 0},
    {"sprite_sheet_fn", &G::sprite_sheet_fn, 0, "_Z20SWChangeMasterSpriteP9SpriteRecS0_", 0},
    {"panel_link_fn", &G::panel_link_fn, 0, "_Z11addGameMenuP8GameMenu", 0},
    {"keys_branch", &G::keys_branch, 0, "_Z9PauseGamev", 0x9C},
    {"options_back_fn", &G::options_back_fn, 0, "_Z22refreshPauseFromOptionP14GameMenuScreen", 0},
    // RETURN HOME WITH THE DISKS? (mission.h): the game's fade (its block and level) and its
    // routines (the pause opener, the ending, the escape skip's prompt).
    {"fade", &G::fade, zap::fade_state::kSize, "gGameState", 0x13448},
    {"fade_level", &G::fade_level, 1, "gGameState", 0x138A3},
    {"routine_table", &G::routine_table, 16 * sizeof(void*), "gExitActions", 0},
    // DeathLink's received deaths (Game::kill): the player's death, as the damage code calls it.
    {"death_fn", &G::death_fn, 0, "_Z17PlayerDestructionP9SpriteRecc", 0},
    // The ASSIST lock (menu.h): ASSIST's own callback, put back when it is unlocked.
    {"assist_menu_fn", &G::assist_menu_fn, 0, "_Z10AssistMenuv", 0},
    // The title screen's lines (title.h): the layer the title draws on, the text functions.
    {"draw_layer", &G::draw_layer, sizeof(void*), "gStatsSpriteLayerP", 0},
    {"text_new_fn", &G::text_new_fn, 0, "_Z15newSpriteStringPKcii", 0},
    {"text_link_fn", &G::text_link_fn, 0, "_Z15addSpriteStringP9SpriteRec", 0},
    // The Feats guard's load test (feats.h): the slots' records and their randomizer sections.
    {"slot_array", &G::slot_array, zap::slot::kCount * zap::slot::kStride, "gSaveGames", 0},
    {"rando_slots", &G::rando_slots, zap::slot::kCount * zap::kSectionSize, "gSavedRandos", 0},
    // The ending's patch (feats.h): its first achievement call, and the join it jumps to.
    {"ending_unlocks", &G::ending_unlocks, 0, "_Z14EndingCutscenev", 0x3B},
    {"ending_join", &G::ending_join, 0, "_Z14EndingCutscenev", 0x39C},
    // The Feats of Natu screen's note in a translation: LargeText's count (its title's entry).
    {"large_count", &G::large_count, sizeof(int16_t), "gLargeTextCount", 0},
    // The file select's AP logo and disk counts (files.h): the save panel's handler, and the
    // entries' of the file select, COPY FILE and DELETE FILE, whose handlers the helper wraps.
    {"file_panel_fn", &G::file_panel_fn, 0, "_Z20SaveGamePlayerCursorP12GameMenuItemi", 0},
    {"file_entry_fn", &G::file_entry_fn, 0, "_Z18SaveGameMenuActionP12GameMenuItemi", 0},
    {"copy_entry_fn", &G::copy_entry_fn, 0, "_Z18CopyGameMenuActionP12GameMenuItemi", 0},
    {"delete_entry_fn", &G::delete_entry_fn, 0, "_Z19ClearGameMenuActionP12GameMenuItemi", 0},
    // The major-item list (the file select's ITEMS row), with its length (game_layout.h pickups).
    {"pickup_list", &G::pickup_list, zap::pickups::kSize, "gGameState", 0xAE0},
    // The text window's top cut, patched so tall popups' lines are cut right (text.h, TextCut).
    {"text_cut_code", &G::text_cut_code, 0, "_Z28setGameMessageStringClippingP9SpriteRecii",
     0x661},
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
    {"tr_flag", &G::tr_flag, 1, "zap_translation", 0x91},
    {"translation", &G::translation, zap::tr::kSize, "zap_translation", 0},
    {"small_text", &G::small_text, sizeof(void*), "zap_translation", 0x30},
    {"small_offsets", &G::small_offsets, sizeof(void*), "zap_translation", 0x58},
    {"item_names", &G::item_names, sizeof(void*), "zap_translation", 0x48},
    {"item_name_offsets", &G::item_name_offsets, sizeof(void*), "zap_translation", 0x78},
    {"small_count", &G::small_count, sizeof(int16_t), "zap_small_count", 0},
    {"item_name_count", &G::item_name_count, sizeof(int16_t), "zap_item_name_count", 0},
    {"hd_text_fn", &G::hd_text_fn, 0, "zap_hd_text", 0},
    {"hd_build_fn", &G::hd_build_fn, 0, "zap_build_text", 0},
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
    {"sprite_texture", &G::sprite_texture, sizeof(void*), "zap_sprite_world", 0},
    {"popup_width_code", &G::popup_width_code, 0, "zap_pickup", 0},
    {"item_pool", &G::item_pool, kItemPool, "zap_item_pool", 0},
    {"palette_table", &G::palette_table, kPaletteTable, "zap_palettes", 0},
    {"frame_clock", &G::frame_clock, sizeof(int64_t), "zap_frame_clock", 0},
    {"sprite_templates", &G::sprite_templates, sizeof(void*), "zap_templates", 0},
    {"object_pool", &G::object_pool, kObjectPool, "zap_object_pool", 0},
    {"flags", &G::flags, zap::kFlagCount, "zap_flags", 0},
    {"map_grid", &G::map_grid, zap::kMapCells, "zap_map_grid", 0},
    {"event_table", &G::event_table, sizeof(void*), "zap_events", 0},
    {"event_count", &G::event_count, sizeof(int16_t), "zap_event_count", 0},
    {"event_handlers", &G::event_handlers, kEventHandlers, "zap_event_procs", 0},
    {"ailments", &G::ailments, sizeof(int16_t), "zap_ailments", 0},
    {"ailments_hit_code", &G::ailments_hit_code, 0, "zap_weapon_cool", 0},
    {"ailments_wave_code", &G::ailments_wave_code, 0, "zap_weapon_cool", 0x35},
    {"halt_flag", &G::halt_flag, 1, "zap_halt", 0},
    {"seen_marker_code", &G::seen_marker_code, 0, "zap_power_up", 0},
    {"map_handler", &G::map_handler, sizeof(void*), "zap_move_procs", 0x3C0},
    {"text_set_fn", &G::text_set_fn, 0, "zap_text_set", 0},
    {"text_table", &G::text_table, sizeof(void*), "zap_text_table", 0},
    {"equipment_fn", &G::equipment_fn, 0, "zap_equipment_menu", 0},
    {"equip_panel_fn", &G::equip_panel_fn, 0, "zap_equipment_frame", 0},
    {"widget_move_fn", &G::widget_move_fn, 0, "zap_widget_move", 0},
    {"text_place_fn", &G::text_place_fn, 0, "zap_text_place", 0},
    {"widget_new_fn", &G::widget_new_fn, 0, "zap_widget_new", 0},
    {"sprite_sheet_fn", &G::sprite_sheet_fn, 0, "zap_sprite_sheet", 0},
    {"panel_link_fn", &G::panel_link_fn, 0, "zap_panel_link", 0},
    {"keys_branch", &G::keys_branch, 0, "zap_pause_game", 0},
    {"options_back_fn", &G::options_back_fn, 0, "zap_options_back", 0},
    {"fade", &G::fade, zap::fade_state::kSize, "zap_fade", 0},
    {"fade_level", &G::fade_level, 1, "zap_fade_level", 0},
    {"routine_table", &G::routine_table, 16 * sizeof(void*), "zap_routines", 0},
    {"death_fn", &G::death_fn, 0, "zap_death", 0},
    {"assist_menu_fn", &G::assist_menu_fn, 0, "zap_assist_menu", 0},
    {"draw_layer", &G::draw_layer, sizeof(void*), "zap_draw_layer", 0},
    {"text_new_fn", &G::text_new_fn, 0, "zap_text_new", 0},
    {"text_link_fn", &G::text_link_fn, 0, "zap_text_link", 0},
    {"slot_array", &G::slot_array, zap::slot::kCount * zap::slot::kStride, "zap_save_games", 0},
    {"rando_slots", &G::rando_slots, zap::slot::kCount * zap::kSectionSize, "zap_saved_randos", 0},
    {"ending_unlocks", &G::ending_unlocks, 0, "zap_ending", 0},
    {"ending_join", &G::ending_join, 0, "zap_ending_join", 0},
    {"large_count", &G::large_count, sizeof(int16_t), "zap_large_count", 0},
    {"file_panel_fn", &G::file_panel_fn, 0, "zap_file_panel", 0},
    {"file_entry_fn", &G::file_entry_fn, 0, "zap_file_entry", 0},
    {"copy_entry_fn", &G::copy_entry_fn, 0, "zap_copy_entry", 0},
    {"delete_entry_fn", &G::delete_entry_fn, 0, "zap_delete_entry", 0},
    {"pickup_list", &G::pickup_list, zap::pickups::kSize, "zap_pickup_list", 0},
    {"text_cut_code", &G::text_cut_code, 0, "zap_text_cut", 0},
};
#endif
constexpr size_t kConfigKeyCount = sizeof(kConfigKeys) / sizeof(kConfigKeys[0]);

// The game's object `write` is bounded by (kWriteBase): gGameState, which the client's writes lie
// in. In the test build the stand-in's `zap_no_write` is the one place in its .bss not written.
#ifdef ZAP_HOOKTEST
constexpr char kWriteBase[] = "zap_no_write";
#else
constexpr char kWriteBase[] = "gGameState";
#endif

// g_client_writes from the symbol `base`: each range of client_write_ranges that lies in the
// game's .bss and in the symbol's own extent; one that doesn't is left out, so none is written.
void set_client_writes(const zap::linux_helper::SymbolSpan& base) {
    g_client_writes->clear();
#ifdef ZAP_HOOKTEST
    for (int i = 0; i < g_writable_count; ++i) {  // all of the stand-in's .bss but its one hole
        const uint64_t start = g_writable[i][0], end = g_writable[i][1];
        const uint64_t hole = base.value, hole_end = base.value + base.size;
        if (hole < start || hole_end > end) {
            g_client_writes->push_back({start, end});
        } else {
            g_client_writes->push_back({start, hole});
            g_client_writes->push_back({hole_end, end});
        }
    }
#else
    const std::vector<Range> wanted = client_write_ranges(base.value);
    std::vector<Range> dropped;
    *g_client_writes = keep_ranges(wanted, base, in_game_data, dropped);
    if (!dropped.empty()) {  // the game is not as recorded: every write there is refused
        for (const Range& range : dropped) {
            std::fprintf(stderr,
                         "zexion-ap %s: write: %#llx-%#llx is outside gGameState (%#llx, %#llx "
                         "bytes) or the game's data: the client's writes there are refused\n",
                         kVersion, static_cast<unsigned long long>(range.start),
                         static_cast<unsigned long long>(range.end),
                         static_cast<unsigned long long>(base.value),
                         static_cast<unsigned long long>(base.size));
        }
    }
#endif
    uint64_t bytes = 0;
    for (const Range& range : *g_client_writes) bytes += range.end - range.start;
    std::fprintf(stderr, "zexion-ap %s: write: %zu ranges, %llu bytes allowed\n", kVersion,
                 g_client_writes->size(), static_cast<unsigned long long>(bytes));
}

// Each key's address by the game's own symbols (kConfigKeys), read from its binary at start-up:
// a config must give these and no others, so a client whose mapping drifted changes nothing.
uint64_t g_expected[kConfigKeyCount] = {};
bool g_expected_known = false;

// The game binary's symbols the helper uses, from /proc/self/exe: the config keys' and the frame
// function's. False, nothing set, if any is missing.
bool read_own_symbols() {
    std::ifstream file("/proc/self/exe", std::ios::binary);
    std::string elf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::string> names{kFrameFunction, kSpriteDefCount, kPlayerPointer, kStatsContext,
                                   kSetAchievement, kWriteBase};
    for (const ConfigKey& key : kConfigKeys) names.emplace_back(key.symbol);
    auto found = zap::linux_helper::find_symbols(elf, names);
    for (const std::string& name : names) {
        if (found.find(name) == found.end()) return false;
    }
    g_frame_function = found[kFrameFunction];
    g_sprite_def_count = found[kSpriteDefCount].value;
    g_player_pointer = found[kPlayerPointer].value;
    zap::set_feats_stats_context(reinterpret_cast<const void*>(found[kStatsContext].value));
    zap::set_feats_achievement_fn(reinterpret_cast<const void*>(found[kSetAchievement].value));
    for (size_t i = 0; i < kConfigKeyCount; ++i) {
        g_expected[i] = found[kConfigKeys[i].symbol].value + kConfigKeys[i].offset;
    }
    set_client_writes(found[kWriteBase]);
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
        g_game->start_archipelago(&archipelago_chosen);  // nothing unless configured
    } catch (...) {
    }
}

// The game-mode panel's handler while the choice is up: the game's own, then the hover border
// fitted around ARCHIPELAGO in the frame the game laid it out (choice.h, fit_border).
void panel_hook(void* widget, int event) {
    auto original = reinterpret_cast<void (*)(void*, int)>(g_game->panel_handler());
    if (original != nullptr) original(widget, event);
    try {
        g_game->fit_archipelago_border();
    } catch (...) {
    }
}

// The map's move procedure while the names are up (mapmarks.h, MapCursorNames): the game's own,
// MapMoveProc, which the game calls with the map's child alone (docs/re/linux.md, `map_handler`),
// then the cursor's name written over a stand-in's in the frame the game wrote it.
void map_hook(void* child) {
    auto original = reinterpret_cast<void (*)(void*)>(g_game->map_original(child));
    if (original != nullptr) original(child);
    try {
        g_game->map_after();
    } catch (...) {
    }
}

// EQUIPMENT's callback while the disks are counted (equip.h): the game's screen, then the disk's
// widget added. The game calls a menu callback with no arguments on Linux (`map_hook`'s note).
void equipment_hook() {
    auto original = reinterpret_cast<void (*)()>(g_game->equipment_fn());
    if (original != nullptr) original();
    try {
        g_game->equipment_built();
    } catch (...) {
    }
}

// The Energy Tank panel's handler while it shows the disk (equip.h): the game's, then the disk's
// name and description when it is hovered or selected. Handlers take (widget, event) on Linux too.
void equipment_panel_hook(void* widget, int event) {
    auto original = reinterpret_cast<void (*)(void*, int)>(g_game->equip_panel_fn());
    if (original != nullptr) original(widget, event);
    try {
        g_game->equipment_event(widget, event);
    } catch (...) {
    }
}

// A save entry's handler on the file select, COPY FILE and DELETE FILE while the logo or the disk
// counts are on (files.h): the game's, then the entry rewritten after its fill (event 0xF).
void file_entry_hook(void* widget, int event) {
    auto original = reinterpret_cast<void (*)(void*, int)>(g_game->file_entry_original(widget));
    if (original != nullptr) original(widget, event);
    try {
        g_game->file_event(widget, event);
    } catch (...) {
    }
}

// The pause menu's way back from OPTION MENU (pause.h): the game's, then the disk row turned in
// the same call (refreshPauseFromOption takes the menu's screen).
void options_back_hook(void* menu) {
    auto original = reinterpret_cast<void (*)(void*)>(g_game->options_back_fn());
    if (original != nullptr) original(menu);
    try {
        g_game->options_returned(menu);
    } catch (...) {
    }
}

// RETURN HOME WITH THE DISKS? (mission.h): the opener and the ending run as the game's
// transition; YES is the prompt's menu callback, which the game calls with no arguments on Linux,
// and so is the game's NO it calls once the helper has taken YES.
void mission_open_hook() {
    try {
        g_game->mission_open();
    } catch (...) {
    }
}

void mission_yes_hook() {
    void* no = nullptr;
    try {
        no = g_game->mission_yes();
    } catch (...) {
    }
    if (no == nullptr) return;
    reinterpret_cast<void (*)()>(no)();
    try {
        g_game->mission_after_no();
    } catch (...) {
    }
}

void mission_end_hook() {
    try {
        g_game->mission_end();
    } catch (...) {
    }
}

// Each part on its own, so one that throws keeps none of the others from running, and nothing
// reaches the game.
template <class Work>
void guarded(Work&& work) {
    try {
        work();
    } catch (...) {
    }
}

// Game thread: what a client's going puts back. A client's popups stay queued for its save, as on
// Windows; its plan, its labels, its Intel Chips and its ASSIST lock go (the records, the popup's
// width, the entry and the pixels put back); the look without the client (watch_offline) may
// dress the save again.
void client_gone() {
    guarded([] { g_game->client_left(); });  // an own disk's count the helper's again
    // Its seed (put by offline_put) no longer the one a look applies in: client_gone's
    // forget_client_seed on Windows. Run for any connection that set state (sets_client_state,
    // offline_put among them), as its seed was noted.
    guarded([] { g_game->forget_client_seed(); });
    // RETURN HOME's prompt and ending, the map's names and the choice put back in open menus,
    // before the disks go below (which drop the prompt asked for), as on Windows.
    guarded([] { g_game->put_back_client_menus(&archipelago_chosen); });
    // The look without the client dressed again from scratch at the next look, its not-connected
    // notice with it, as client_gone does on Windows (put_back_game).
    guarded([] { g_game->leave_offline(); });
    guarded([] { g_game->disarm_newgame(); });
    guarded([] { g_game->restore_ap_items(); });
    guarded([] { g_game->restore_intel(); });  // the Area Map's record the game's again
    guarded([] {  // ASSIST unlocked, as drop_save_look on Windows
        g_game->set_assists_locked(false);
        g_game->restore_assists();
    });
    guarded([] { g_game->restore_translation(); });  // the names, entry 70, the icons
    guarded([] {  // the disks' count off and put back, as drop_save_look on Windows
        g_game->set_discs(0, 0, "");
        g_game->restore_discs();
    });
    // Its notices not shown yet withdrawn (another seed's, CLIENT CONNECTED.), as client_gone does
    // on Windows: the offline look dresses the save again with no client from here.
    guarded([] { g_game->drop_client_notices(); });
}

// Game thread: client_gone once if a client has gone since it last ran. At the top of each frame
// and of each request's work (on_game): the serving thread marks a client gone before it takes the
// next, so whatever the next client asks, the last one's plan is put back first and is never
// taken for the new one's. Run once, whichever comes first (the flag's exchange).
void client_gone_if_left() {
    if (g_client_left.exchange(false)) client_gone();
}

// `work` run on the game's thread at its next frame call; "refused not configured" first when
// `needs_config` and no config came; "refused no frame" if no frame took it in time (it then
// never runs); "error <why>" at once for a request not parsed. As the Windows helper's on_game.
// Serving thread: whether this request's work went to the mailbox (may have run), for what a
// throw costs: a throw before it is answered "error exception" (nothing done), one after drops
// the client (serve).
thread_local bool t_posted = false;

std::string on_game(const std::function<std::string()>& work, bool needs_config,
                    const std::string& parse_error = "") {
    if (!parse_error.empty()) return "error " + parse_error;
    std::string result;
    t_posted = true;
    bool ran = g_mailbox->call(
        [&] {
            client_gone_if_left();  // the last client's, before anything this one asks
            if (needs_config && !g_game->configured()) return std::string("refused not configured");
            return work();
        },
        kFrameWaitMs, result);
    return ran ? result : "refused no frame";
}

// Game thread: the game configured with `found` (parse_config's: always the game's own, so one
// after the first changes nothing the choice or the popups hold), a client's (`active`) or the
// helper's own (configure_self).
void install_config(const zap::GameAddresses& found, bool active) {
    g_game->set_hooks({.panel = reinterpret_cast<void*>(&panel_hook),
                      .equipment = reinterpret_cast<void*>(&equipment_hook),
                      .equipment_panel = reinterpret_cast<void*>(&equipment_panel_hook),
                      .mission_open = reinterpret_cast<void*>(&mission_open_hook),
                      .mission_yes = reinterpret_cast<void*>(&mission_yes_hook),
                      .mission_end = reinterpret_cast<void*>(&mission_end_hook),
                      .file_entry = reinterpret_cast<void*>(&file_entry_hook),
                      .map = reinterpret_cast<void*>(&map_hook),
                      .options_back = reinterpret_cast<void*>(&options_back_hook)});
    g_game->configure(found, active);
    // The icon sheets as traced only with the game's definitions counted as traced, read again at
    // each config (a self-config at the first frame may come before the game counts them).
    int16_t defs = 0;
    auto* count = reinterpret_cast<const BYTE*>(g_sprite_def_count);
    g_game->hold_icon_sheets(count == nullptr || !zap::read(count, 0, defs) ||
                            defs != kTracedSpriteDefs);
}

// Whether the game's player pointer reads the player state `inventory` (the game's own symbols'
// address, g_expected) lies in.
bool player_where_expected() {
    uint64_t inventory = 0;
    for (size_t i = 0; i < kConfigKeyCount; ++i) {
        if (std::strcmp(kConfigKeys[i].name, "inventory") == 0) inventory = g_expected[i];
    }
    uint64_t player = 0;
    auto* pointer = reinterpret_cast<const BYTE*>(g_player_pointer);
    return g_expected_known && inventory != 0 && pointer != nullptr &&
           zap::read(pointer, 0, player) && player == inventory - kInventoryInPlayer;
}

// AP saves without the client (native/README.md, "AP saves without the client"): the seeds'
// files (offline.h) read on the serving thread at start and each time a client goes, and the
// seeds put since; the next frame takes both (take_offline).
__attribute__((init_priority(101))) std::mutex g_offline_mutex;
__attribute__((init_priority(101))) Kept<std::optional<zap::OfflineFiles>>
    g_offline_files;  // read, not yet taken
struct PutSeed {
    zap::OfflineSeed seed;
    bool kept;  // its file written ("stored", "unchanged")
};
__attribute__((init_priority(101))) Kept<std::vector<PutSeed>> g_put_seeds;  // put since, not taken
// The folder, found once in the constructor (getenv there, before the game's threads run: never
// while they may set the environment).
__attribute__((init_priority(101))) Kept<std::wstring> g_offline_folder;
// The game's own save folder (offline.h, save_folder), found there too: the seeds' copies Steam
// Cloud carries with the saves are written into it, and read from it.
__attribute__((init_priority(101))) Kept<std::wstring> g_save_folder;
// The seeds of the game's randomizer saves as its memory holds the slots (Game::saved_seeds),
// published from the frame call, read by the serving thread and the copy thread: a seed's copy goes
// into the save folder only while a slot here uses it, and no save file of the game is opened.
__attribute__((init_priority(101))) Kept<zap::SavedSeeds> g_saved;

// The slots for a call of the serving thread: null before the first list (none known: no copy).
const std::vector<zap::OfflineSeed>* saved_list(std::vector<zap::OfflineSeed>& held) {
    if (g_saved->generation() == 0) return nullptr;
    held = g_saved->get();
    return &held;
}
// The client's token (core.h), made in the constructor and written into that folder.
__attribute__((init_priority(101))) Kept<std::string> g_token;

// Serving thread (file I/O; never the frame call). A file that could not be read is said once.
void read_offline() {
    std::vector<zap::OfflineSeed> held;
    zap::OfflineFiles files = zap::read_offline_files(*g_offline_folder, g_build, *g_save_folder,
                                                      true, saved_list(held));
    static Kept<std::string> said;  // as the globals above: never destroyed under this thread
    if (!files.error.empty() && files.error != *said) {
        std::fprintf(stderr, "zexion-ap %s: seed files: %s\n", kVersion, files.error.c_str());
        *said = files.error;
    }
    std::lock_guard<std::mutex> lock(g_offline_mutex);
    *g_offline_files = std::move(files);
}

// Serving thread: a seed a client has just put, for the next frame (one of its name waiting is
// replaced, as on Windows).
void note_put_seed(zap::OfflineSeed seed, bool kept) {
    std::lock_guard<std::mutex> lock(g_offline_mutex);
    // One of the same name still waiting is replaced, and the newest put is always last
    // (take_offline: the client's seed is the last put), as on Windows.
    std::erase_if(*g_put_seeds, [&](const PutSeed& s) { return s.seed.name == seed.name; });
    g_put_seeds->push_back({std::move(seed), kept});
}

// Game thread: with no client yet, the helper configured from the game's own symbols (the
// addresses a client's config must give: parse_config), inactive, as the Windows helper's own scan's
// (or its cache's) is: only what runs with no client uses it. Only with a seed file read to work from, so
// a game with none is left alone until a client configures, and only once the game's player
// pointer reads where the config's player state is (tried again each frame until then).
bool g_seed_files = false;  // a seed file has been read
void configure_self() {
    static bool failed = false;
    if (failed || !g_seed_files || g_game->configured() || !g_expected_known) return;
    if (!player_where_expected()) return;
    std::string request = "config";
    char address[32];
    for (size_t i = 0; i < kConfigKeyCount; ++i) {
        std::snprintf(address, sizeof address, "%#llx",
                      static_cast<unsigned long long>(g_expected[i]));
        request += std::string(" ") + kConfigKeys[i].name + "=" + address;
    }
    zap::GameAddresses found;
    std::string why = parse_config(request, found);
    if (why.empty()) {
        install_config(found, false);
        std::fprintf(stderr, "zexion-ap %s: configured from the game's symbols, no client\n",
                     kVersion);
    } else {
        failed = true;  // the same symbols would fail the same: not tried again
        std::fprintf(stderr, "zexion-ap %s: not configured with no client: %s\n", kVersion,
                     why.c_str());
    }
}

// Game thread: the files read last and the seeds put since, if new. Files read with no client
// replace the last ones; with one, they are added to them (Game::set_offline, merge_offline).
// A seed taken but not kept (no folder, a write that failed) is the helper's until the game
// closes: added again after each read of the files replaces the seeds (set_offline).
void take_offline() {
    // The slots' seeds for the other threads: the first frame and every 16th (cheap).
    static uint32_t frame_count = 0;
    if ((frame_count++ & 15u) == 0) {
        guarded([] { g_saved->publish(g_game->saved_seeds()); });  // a throw skips no other work
    }
    static std::vector<zap::OfflineSeed> unkept;
    std::optional<zap::OfflineFiles> files;
    std::vector<PutSeed> put;
    if (g_offline_mutex.try_lock()) {
        files = std::exchange(*g_offline_files, std::nullopt);
        put = std::exchange(*g_put_seeds, {});
        g_offline_mutex.unlock();
    }
    if (files) {
        if (!files->seeds.empty()) g_seed_files = true;
        if (g_game->active()) {
            g_game->merge_offline(std::move(files->seeds));
        } else {
            g_game->set_offline(std::move(files->seeds));
            if (!unkept.empty()) g_game->merge_offline(std::vector<zap::OfflineSeed>(unkept));
        }
    }
    std::vector<zap::OfflineSeed> seeds;
    for (PutSeed& p : put) {
        if (!p.kept) {
            std::erase_if(unkept, [&](const zap::OfflineSeed& s) { return s.name == p.seed.name; });
            unkept.push_back(p.seed);
        }
        seeds.push_back(std::move(p.seed));
    }
    if (!seeds.empty()) {
        // The client puts its own seed alone: the one its look applies in (Game::seed_look_loaded).
        // Noted here, on the game thread, after the client-gone work of this frame (client_gone:
        // a put from a client that has gone is ignored, note_client_seed).
        guarded([&] { g_game->note_client_seed(seeds.back()); });
        g_game->merge_offline(std::move(seeds));  // after the files, read earlier
    }
    configure_self();  // each frame until it has configured, a seed file once read
}

// The requests that act on the game (native/README.md, "The Linux helper"), or nothing for one
// core.cpp answers.
std::optional<std::string> game_request(std::string_view request) {
    if (request == "config" || request.starts_with("config ")) {
        zap::GameAddresses found;
        std::string error = parse_config(request, found);
        return on_game(
            [&] {
                install_config(found, true);
                return std::string("ok");
            },
            false, error);
    }
    if (request.starts_with("ap_newgame\n") || request == "ap_newgame") {
        zap::NewGamePlan plan;
        std::string error = zap::parse_newgame(std::string(request), plan);
        return on_game(
            [&] {
                return std::string(g_game->arm_newgame(std::move(plan)) ? "armed"
                                                                       : "refused not configured");
            },
            true, error);
    }
    if (request == "disarm") {
        return on_game([] { g_game->disarm_newgame(); return std::string("disarmed"); }, false);
    }
    if (request.starts_with("ap_labels\n") || request == "ap_labels") {
        std::map<int, zap::Label> labels;
        std::string error = zap::parse_labels(std::string(request), labels);
        return on_game(
            [&] {
                size_t n = labels.size();
                g_game->set_labels(std::move(labels));
                return "labeled " + std::to_string(n);
            },
            true, error);
    }
    if (request == "lang") {
        // The language the game shows, one word (as the Windows helper's hello's `lang`): "?"
        // unconfigured or with no translation's name readable.
        return on_game(
            [] {
                std::string name = g_game->language();
                std::replace(name.begin(), name.end(), ' ', '_');
                return "lang " + name;
            },
            false);
    }
    if (request.starts_with("discs ") || request == "discs") {
        // The OS Install Disks goal's count and the scanner's line (scan.h; requests.h's format),
        // as the Windows helper takes them.
        int count = 0, required = 0;
        bool told = false;
        std::string line;
        std::string error = zap::parse_discs(std::string(request), count, required, line, told);
        return on_game(
            [&] {
                const bool on = !line.empty();
                g_game->set_discs(count, required, std::move(line), told);
                return std::string(on ? "discs on" : "discs off");
            },
            true, error);
    }
    if (request.starts_with("mission_prompt")) {
        // RETURN HOME WITH THE DISKS? offered once the game is idle in play at home (mission.h;
        // requests.h's format), as the Windows helper takes it.
        int column = -1, row = -1;
        std::string error = zap::parse_mission(std::string(request), column, row);
        return on_game(
            [&] {
                return std::string(g_game->request_mission(column, row) ? "queued"
                                                                       : "refused not configured");
            },
            true, error);
    }
    if (request == "mission_status") {
        return on_game([] { return g_game->mission_status(); }, true);
    }
    if (request == "mission_black") {
        return on_game([] { return g_game->mission_black(); }, true);
    }
    if (request == "discs_status") {
        return on_game([] { return g_game->equipment_status(); }, false);
    }
    if (request == "trap_status") {
        return on_game([] { return g_game->trap_status(); }, true);
    }
    if (request == "shield") {  // a received Energy Shield (game_calls.h, spawn_shield)
        return on_game([] { return g_game->spawn_shield(zap::ticks_ms()); }, false);
    }
    if (request == "other_save") {  // a client of another seed's: the save's look kept
        return on_game([] { return g_game->other_save(); }, true);
    }
    if (request == "client_back") {  // CLIENT CONNECTED, once, for a save dressed with none
        return on_game([] { return std::string(g_game->client_back() ? "shown" : "none"); }, true);
    }
    if (request == "assists lock" || request == "assists free") {  // the ASSIST lock (menu.h)
        const bool lock = request == "assists lock";
        return on_game(
            [lock] {
                g_game->set_assists_locked(lock);
                return std::string(lock ? "locked" : "free");
            },
            true);
    }
    if (request == "intel on" || request == "intel off") {  // Intel Chips (intel.h)
        const bool on = request == "intel on";
        return on_game(
            [on] {
                g_game->set_intel(on);
                return std::string(on ? "intel on" : "intel off");
            },
            true);
    }
    if (request == "die") {  // a DeathLink death received (game_calls.h, kill)
        return on_game([] { return g_game->kill(zap::ticks_ms()); }, false);
    }
#ifdef ZAP_HOOKTEST
    if (request == "frame_calls") {  // the calls frame work was taken for (tests only)
        return "frame_calls " + std::to_string(g_frame_calls.load());
    }
#endif
    if (request == "feats_status") {  // the Feats guard (feats.h, state): checks
        return on_game([] { return g_game->feats_state(); }, false);
    }
    if (request == "title_status") {  // the title's lines (game_calls.h, title_state): checks
        return on_game([] { return g_game->title_state(); }, false);
    }
    if (request == "drop_status") {  // the received scraps' drops (game_calls.h): live checks
        return on_game([] { return g_game->drop_status(); }, false);
    }
    if (request.starts_with("offline_put ")) {
        // Here, on the serving thread: the seed's file written into the helper's folder
        // (offline.cpp, offline_folder), and its text the running helper's from the next frame
        // (take_offline) whether the file could be kept or not, as on Windows.
        zap::OfflineSeed seed;
        std::string save_error;
        std::vector<zap::OfflineSeed> held;
        std::string reply = zap::put_offline_file(*g_offline_folder, g_build, std::string(request),
                                                  &seed, *g_save_folder, &save_error,
                                                  saved_list(held));
        if (!save_error.empty()) {
            std::fprintf(stderr, "zexion-ap %s: the seed's copy in the save folder: %s\n",
                         kVersion, save_error.c_str());
        }
        if (!seed.name.empty()) {
            note_put_seed(std::move(seed), reply == "stored" || reply == "unchanged");
        }
        return reply;
    }
    if (request == "newgame_status") {
        return on_game([] { return g_game->newgame_status(); }, false);
    }
    if (request.starts_with("apply\n") || request == "apply") {
        zap::Batch batch;
        std::string error = zap::parse_batch(std::string(request), batch);
        return on_game(
            [&] { return g_game->apply(batch); }, true, error);
    }
    return std::nullopt;
}

// A folder and the ones above it made if they are not there yet, each only for the user (mode
// 0700); false if it is not there after.
bool make_private_folders(const std::string& path) {
    for (size_t slash = path.find('/', 1); slash != std::string::npos;
         slash = path.find('/', slash + 1)) {
        mkdir(path.substr(0, slash).c_str(), 0700);  // there already: EEXIST, left as it is
    }
    mkdir(path.c_str(), 0700);
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

// `size` random bytes from the kernel; false if it won't give them.
bool random_bytes(unsigned char* out, size_t size) {
    for (size_t done = 0; done < size;) {
        ssize_t n = getrandom(out + done, size - done, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

// The client's token (core.h) made and written to `folder`/token for the user alone: a new file
// (mode 0600, never through a link) renamed over the last run's, so no one sees a half-written
// one. Said on stderr why not, false, and the helper serves no one: a socket anyone could use
// is not served.
bool make_token(const std::string& folder) {
    unsigned char bytes[zap::linux_helper::kTokenBytes];
    if (!random_bytes(bytes, sizeof bytes)) {
        std::fprintf(stderr, "zexion-ap %s: no token: the kernel's random bytes: %s\n", kVersion,
                     std::strerror(errno));
        return false;
    }
    std::string token = hex_text(bytes, sizeof bytes);
    std::memset(bytes, 0, sizeof bytes);
#ifdef ZAP_HOOKTEST
    // The test build writes no folder that is not a test's own, the token's included (the folder
    // already went through restrict_to_test_folder; this is the check at the write itself).
    {
        std::wstring named;
        for (char c : folder) named += static_cast<wchar_t>(static_cast<unsigned char>(c));
        if (zap::restrict_to_test_folder(named).empty()) {
            std::fprintf(stderr, "zexion-ap %s: no token: not a test folder\n", kVersion);
            return false;
        }
    }
#endif
    if (folder.empty() || !make_private_folders(folder)) {
        std::fprintf(stderr, "zexion-ap %s: no token: no folder to put it in (%s)\n", kVersion,
                     folder.empty() ? "no data folder" : folder.c_str());
        return false;
    }
    const std::string target = folder + "/" + kTokenFile, temp = target + ".tmp";
    unlink(temp.c_str());  // a stale one (or a link planted there) gone, then made afresh
    int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    bool ok = fd >= 0;
    int why = ok ? 0 : errno;
    for (size_t done = 0; ok && done < token.size();) {
        ssize_t n = ::write(fd, token.data() + done, token.size() - done);
        if (n < 0 && errno == EINTR) continue;
        ok = n > 0;
        if (ok) done += static_cast<size_t>(n);
        else why = errno;
    }
    if (fd >= 0 && close(fd) != 0 && ok) {
        ok = false;
        why = errno;
    }
    if (ok && rename(temp.c_str(), target.c_str()) != 0) {
        ok = false;
        why = errno;
    }
    if (!ok) {
        unlink(temp.c_str());
        std::fprintf(stderr, "zexion-ap %s: no token: %s: %s\n", kVersion, target.c_str(),
                     std::strerror(why));
        return false;
    }
    *g_token = token;
    return true;
}

// Whether a request sets something a client's going must put back (a plan, labels, the ASSIST
// lock, Intel Chips, the disks' count, a prompt asked for, a seed put): by its first word, the
// others only read the game, answer it, or act once (a grant, a death, a shield). `config` is one
// as it installs the client as the active one.
bool sets_client_state(std::string_view request) {
    std::string_view word = request.substr(0, request.find_first_of(" \n"));
    for (std::string_view name :
         {"config", "ap_newgame", "ap_labels", "discs", "mission_prompt", "other_save",
          "client_back", "assists", "intel", "offline_put"}) {
        if (word == name) return true;
    }
    return false;
}

// One client: a request a message, a reply a message, until it goes, sends too much, or says
// nothing for kClientSilenceSeconds. It serves nothing but one `hello` until the client has shown
// the token (core.h, gate), and is dropped if its first words are anything else, or not the
// token, or the token has not come kAuthSeconds after it was accepted (one deadline: asking
// `hello` again does not extend it). `left_state` is set once it sends a request that sets
// something (sets_client_state): only a client that did has anything to put back when it goes,
// whether or not it sent `config` (a connection made again in the middle of a poll may not have).
void serve_client(int client, bool& left_state) {
    const uint64_t deadline = zap::ticks_ms() + kAuthSeconds * 1000ull;
    bool authed = false, said_hello = false;
    std::vector<char> buffer(kMaxRequest);
    for (;;) {
        if (!authed) {
            const uint64_t now = zap::ticks_ms();
            if (now >= deadline) return;
            const uint64_t left = deadline - now;
            timeval waiting{static_cast<time_t>(left / 1000),
                            static_cast<suseconds_t>((left % 1000) * 1000)};
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &waiting, sizeof waiting);
        }
        ssize_t n = recv(client, buffer.data(), buffer.size(), MSG_TRUNC);
        if (n <= 0 || static_cast<size_t>(n) > buffer.size()) return;
        std::string_view request(buffer.data(), static_cast<size_t>(n));
        if (!authed) {
            Gate checked = gate(request, *g_token, state());
            if (request == "hello") {
                if (said_hello) checked = Gate{"error auth required", false, true};
                said_hello = true;
            }
            if (send(client, checked.answer.data(), checked.answer.size(), MSG_NOSIGNAL) < 0 ||
                checked.drop) {
                return;
            }
            if (checked.authed) {
                authed = true;
                timeval silence{kClientSilenceSeconds, 0};
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &silence, sizeof silence);
            }
            continue;
        }
        if (sets_client_state(request)) left_state = true;
        std::optional<std::string> game;
        t_posted = false;
        try {
            game = game_request(request);
        } catch (...) {
            if (t_posted) throw;  // its work may have run: the client dropped (serve)
            game = std::string("error exception");  // parsed only: nothing done, tried again
        }
        std::string answer = game ? *game : reply(request, state(), read_own, write_own);
        if (send(client, answer.data(), answer.size(), MSG_NOSIGNAL) < 0) return;
    }
}

// An exception must not reach the game: one thrown on this thread would end at pthread's start
// and terminate the game (Windows' pipe thread catches all too). Thrown while serving a client,
// the client is dropped, as a lost reply it reads as maybe applied (never as refused), and the
// next one served.
void read_offline_guarded() {
    try {
        read_offline();
    } catch (...) {
    }
}

void* serve(void* arg) {
    int listener = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    read_offline_guarded();  // the seeds' files, for the first frame (take_offline)
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
            bool left_state = false;
            try {
                serve_client(client, left_state);
            } catch (...) {
            }
            // As the Windows helper does, a client's plan is not kept for it: the next frame puts
            // back what it set (client_gone), and before any request of the next client
            // (on_game). A connection that set nothing (`helper.py status`, a probe, or one made
            // again after a drop that has not sent its `config` yet) has nothing to put back, and
            // its going changes nothing: the offline look as it was. The seeds' files are read
            // again for a client that did set something, as there.
            if (left_state) {
                g_client_left = true;
                read_offline_guarded();
            }
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

// The copy thread: when the game's slots change (a save made, a new game, a deleted save) it reads
// the seed files and writes the copy of each seed a slot now uses into the save folder: the case of
// a seed a client put before the game's first save, whatever the client does after. Never the
// frame call; looks at the slots' generation every 250 ms. The helper stays for the game's run.
void* copy_loop(void*) {
    uint64_t done = 0;
    for (;;) {
        usleep(250 * 1000);
        const uint64_t generation = g_saved->generation();
        if (generation == done) continue;
        done = generation;
        try {
            std::vector<zap::OfflineSeed> saved = g_saved->get();
            zap::OfflineFiles files = zap::read_offline_files(*g_offline_folder, g_build,
                                                              *g_save_folder, true, &saved);
            if (!files.error.empty()) {
                std::fprintf(stderr, "zexion-ap %s: seed copies: %s\n", kVersion,
                             files.error.c_str());
            }
        } catch (...) {
        }
    }
    return nullptr;
}

bool start_copy_thread() {
    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_SETMASK, &all, &old);
    pthread_t thread;
    int failed = pthread_create(&thread, nullptr, copy_loop, nullptr);
    pthread_sigmask(SIG_SETMASK, &old, nullptr);
    if (failed != 0) return false;
    pthread_detach(thread);
    return true;
}

extern "C" void* SteamInternal_ContextInit(void* context);  // defined at the end, exported

// The kernel's copy of this process's own memory must work for the helper's reads (`read`, the
// frame's checks): a sandbox's seccomp filter may refuse process_vm_readv, and the helper would
// then write items it could not show. "" if it does, else why not.
std::string memory_reads_refused() {
    static const char kProbe[] = "zexion-ap";
    std::string copy;
    if (!read_own(reinterpret_cast<uintptr_t>(&kProbe[0]), sizeof kProbe, copy)) return copy;
    return copy == std::string(kProbe, sizeof kProbe) ? "" : "read back differently";
}

// The constructor's work. Any exception is the caller's to catch: it must not leave the library.
void start_inside() {
    // Every library the program needs is mapped before any constructor runs.
    zap::set_feats_export(reinterpret_cast<const void*>(&SteamInternal_ContextInit));
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
    std::string refused = memory_reads_refused();
    if (!refused.empty()) {
        std::fprintf(stderr, "zexion-ap %s: memory reads refused: %s\n", kVersion,
                     refused.c_str());
        return;
    }
    g_folder = !folder.empty() && folder == folder_of("/proc/self/exe");
#ifdef ZAP_HOOKTEST
    // The test build serves any program, never the game: every folder it writes goes through
    // restrict_to_test_folder (inside the temporary folder, never the real ones). Its save
    // folder is only what a test names in ZAP_TEST_SAVE_DIR, not $HOME or $XDG_DATA_HOME.
    *g_offline_folder = zap::restrict_to_test_folder(zap::offline_folder());
    const char* test_save = std::getenv("ZAP_TEST_SAVE_DIR");
    std::wstring named;  // the folder's bytes, as offline.cpp holds a path in a wstring
    for (const char* c = test_save; c != nullptr && *c != '\0'; ++c) {
        named += static_cast<wchar_t>(static_cast<unsigned char>(*c));
    }
    *g_save_folder = zap::restrict_to_test_folder(named);
#else
    *g_offline_folder = zap::offline_folder();  // here, before the game's threads (getenv)
    *g_save_folder = zap::save_folder();
#endif
    g_game->set_title_version(kVersion);  // the title's version line (title.h), as on Windows
    if (!read_own_symbols()) {
        std::fprintf(stderr, "zexion-ap %s: the game's symbols were not found: no new games\n",
                     kVersion);
    }
    // The token before the socket: a client that finds the socket finds the token's file.
    std::string data_folder;  // the folder's bytes, as offline.cpp holds a path in a wstring
    for (wchar_t c : *g_offline_folder) {
        data_folder += static_cast<char>(static_cast<unsigned char>(c));
    }
    if (!make_token(data_folder)) return;
    int fd = listen_socket();
    if (fd < 0) return;
    if (!start_thread(fd)) {
        close(fd);
        return;
    }
    g_active = true;
    start_copy_thread();  // no copies if it fails: the local files and the puts still work
    std::fprintf(stderr, "zexion-ap %s: serving build %s\n", kVersion, g_build);
}

__attribute__((constructor)) void start() {
    // A constructor that throws ends every program the preload reaches, before main: whatever
    // fails here leaves the helper off (g_active false), passing the frame call on.
    try {
        start_inside();
    } catch (...) {
        std::fprintf(stderr, "zexion-ap %s: failed to start\n", kVersion);
    }
}

}  // namespace

namespace {

// The game's thread, at its frame swap (SWHardwareEnd's call, or a wrapper's: game_frame): a
// client gone's plan and labels dropped, the mailbox's work, an armed new game watched, the
// ARCHIPELAGO choice, other players' items, the traps, then the next popup. Each part guarded on
// its own, so one that throws keeps none of the others from running, and nothing reaches the
// game. A fault in the game's own placement is not an exception: it stays the game's crash.
// One at a time, as the Windows helper's frame: a game function the frame work calls that swaps
// the window again (a draw) finds this running, and only passes the swap on.
std::atomic_flag g_in_frame_work;  // clear: C++20's default

struct FrameScope {
    bool entered = !g_in_frame_work.test_and_set(std::memory_order_acquire);
    ~FrameScope() {
        if (entered) g_in_frame_work.clear(std::memory_order_release);
    }
    FrameScope(const FrameScope&) = delete;
    FrameScope& operator=(const FrameScope&) = delete;
    FrameScope() = default;
};

bool in_frame_function(uintptr_t address) {
    return address >= g_frame_function.value &&
           address < g_frame_function.value + g_frame_function.size;
}

// One level of wrapper: another library's SDL_GL_SwapWindow, preloaded ahead of the helper's (an
// overlay, a frame limiter), calls ours from its own code, and the game's frame function is the
// wrapper's caller (docs/re/linux.md, "The per-frame call"). Found by the unwinder, from every
// library's .eh_frame, not by frame pointers, which a wrapper built without them does not keep:
// the first few frames of the stack, to the one whose address is `site` (the wrapper's call), and
// the address above it. A stack the unwinder cannot read ends the walk: no frame work, nothing more.
struct StackWalk {
    uintptr_t site;       // where our caller will return: the wrapper's frame
    bool found = false;   // `site`'s frame has been passed
    uintptr_t above = 0;  // the frame above it: where the wrapper itself will return
    int steps = 0;
};

_Unwind_Reason_Code walk_one_frame(_Unwind_Context* context, void* data) {
    constexpr int kMaxSteps = 6;  // ours, the wrapper's, its caller's, and room for a helper frame
    auto* walk = static_cast<StackWalk*>(data);
    const auto address = static_cast<uintptr_t>(_Unwind_GetIP(context));
    if (walk->found) {
        walk->above = address;
        return _URC_END_OF_STACK;
    }
    if (address == walk->site) {
        walk->found = true;
    } else if (++walk->steps >= kMaxSteps) {
        return _URC_END_OF_STACK;
    }
    return _URC_NO_REASON;
}

// Whether `site`'s caller was called from the frame function: it is a wrapper the frame function
// calls. Only for a call not itself from the frame function (a direct call is the common case and
// costs no walk), and never when the frame function is unknown.
__attribute__((noinline)) bool called_through_wrapper(uintptr_t site) {
    if (g_frame_function.size == 0) return false;
    StackWalk walk{site};
    _Unwind_Backtrace(walk_one_frame, &walk);
    return walk.found && walk.above != 0 && in_frame_function(walk.above);
}

// A wrapper may call the helper's swap more than once for one call of its own from the frame
// function (a frame pacer, a duplicating layer), and frame work should run once a game frame, as
// its frame-counted timers (the popups' gap, the file select's, the Steam retry) assume. The
// helper cannot tell the game's frames apart from the wrapper's calls: the game's frame_clock
// (`gGameState+0xB0`, docs/re/linux.md) is recorded, but stands still while a menu pauses the
// game, so it cannot key frame work. So a call through a wrapper runs the work only if
// kWrapperGapUs had passed, when it began, since the last call through a wrapper, of any kind,
// returned from the real swap: every such call stamps its return, so a wrapper's third and fourth
// calls follow the second's return as the second followed the first's, however long the real swap
// waits for the refresh (inferred from the game's measured call rate, not seen). The premise, an
// assumption: the game's work for a frame, from one swap's return to the next call, takes longer
// than kWrapperGapUs. Backstop: a tight loop of calls through a wrapper (a game loop with no
// work between swaps) would never be due, so the work runs anyway once kWrapperBackstopUs have
// passed since the last run began. Limits, both timing: a wrapper that does something slow
// between its repeated calls still has the work run for each; and two real frames closer than
// the gap (`loadState`'s swap, then the frame's) lose the second's work, which waits one frame.
// The direct path has neither.
constexpr int64_t kWrapperGapUs = 100;
constexpr int64_t kWrapperBackstopUs = 50000;
std::atomic<int64_t> g_wrapper_returned_us{INT64_MIN / 2};  // the last wrapper call's return
std::atomic<int64_t> g_wrapper_ran_us{INT64_MIN / 2};       // the last run through one, begun

int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void frame_work() {
    g_frame_calls.fetch_add(1, std::memory_order_relaxed);
    FrameScope scope;
    if (!scope.entered) return;
    // First, as the Windows helper's frame: the cell clock a death, a shield or a drop waits on
    // (settled) is this frame's when the mailbox's requests run (note_play does nothing until
    // configured).
    guarded([] { g_game->note_play(zap::ticks_ms()); });
    // A client gone (client_gone): before the mailbox, so nothing that next client asks is
    // dropped with the last one's.
    client_gone_if_left();
    // The seeds' files read since (and, with none configured, the game's own config), before
    // the mailbox as on Windows: a client's config this frame comes after them.
    guarded([] { take_offline(); });
    guarded([] { g_mailbox->serve(); });
    if (!g_game->configured()) return;
    // Feats and Steam achievements kept out of AP saves: the guard on while one is loaded (and on
    // Linux, after an AP ending the patch did not jump over, until a save loads), and the load
    // test over the slots holding them
    // (feats.h; the export below decides each call). Before the gate below, so what the export
    // blocks follows the game whatever the player pointer reads. Its writes come before the gate
    // too: the ending's patch, which checks its own bytes and the call's target first, and the
    // Feats of Natu screen's note, which writes only into a top menu of that screen's whole shape
    // (MISSION COMPLETE's statistics under it, a live title widget, every glyph writable), and
    // only after an ending the patch jumped over.
    guarded([] { g_game->watch_feats(); });
    // Configured with no client: nothing touches the game while its player pointer reads
    // elsewhere, as the client's attach checks (it read the player state at the title screen
    // before any save, and in play: docs/re/linux.md, "Read from the running game").
    if (!g_game->active() && !player_where_expected()) return;
    try {
        g_game->watch_newgame();
    } catch (...) {
        guarded([] { g_game->refuse_newgame("the placement failed"); });
    }
    // The ARCHIPELAGO choice while a plan offers it, RANDOMIZER back otherwise.
    guarded([] { g_game->watch_menu(&archipelago_chosen); });
    // An AP save's look with no client, or with one of another seed (labels, INTEL CHIP, ASSIST,
    // the disks), set before the watches below apply it, as watch_all orders it on Windows.
    guarded([] { g_game->watch_offline(); });
    // The file select, COPY FILE and DELETE FILE: the AP logo on AP saves, each disk seed's save
    // its own count (files.h), after the look above has set the counts.
    guarded([] { g_game->watch_files(); });
    // Other players' items: the stand-ins' records named and restyled, the logo painted, the
    // popup's width, other Zexion players' items dressed as themselves (apitem.h, looks.h).
    guarded([] { g_game->watch_map_names(); });  // the cursor's names while the map is open
    guarded([] { g_game->watch_ap_items(); });
    guarded([] { g_game->watch_intel(); });  // the Area Map named INTEL CHIP while `intel on`
    guarded([] { g_game->watch_assists(); });  // ASSIST locked in the options menu while asked
    guarded([] { g_game->watch_title(); });  // the title screen's lines, with no save loaded
    guarded([] { g_game->watch_map_marks(); });  // the squares raised, the on-screen write patched
    // The major-item list: each owned item the game's pickup would list, in play in an AP save,
    // for the file select's ITEMS row once the game saves or reaches a checkpoint (game_layout.h
    // pickups; docs/re/linux.md, "Mapped by disassembly: the major-item list").
    guarded([] { g_game->watch_pickups(); });
    // In a translation: the helper's names in its tables, its icons in its sheets, the popups'
    // borrowed entries emptied once their message has gone (translation.h).
    guarded([] { g_game->watch_translation(); });
    // The OS Install Disks' count: the scanner's line, the equipment screen's disk and the pause
    // screen's disk row, the disk glyph (equip.h, pause.h, scan.h).
    guarded([] { g_game->watch_discs(); });
    guarded([] { g_game->watch_equipment(); });
    guarded([] { g_game->watch_mission(); });  // RETURN HOME WITH THE DISKS? (mission.h)
    // Traps: this world's own at their pickup (from the seed a client put), the AILMENTS tests'
    // patches while a trap's timer runs, then one trap set off (a received one as its popup
    // shows), as the Windows helper's frame (trap.h; native/README.md, "Traps").
    guarded([] { g_game->watch_own_traps(); });
    // This world's own disks: their cells watched with a client or none, a pickup's count shown
    // with none, as the Windows helper's (game_offline.cpp).
    guarded([] { g_game->watch_own_disks(); });
    guarded([] { g_game->watch_trap_patches(); });
    // Tall popups' lines cut right: the text window's top cut takes off each glyph's row (text.h).
    guarded([] { g_game->watch_text_cut(); });
    guarded([] { g_game->pump_traps(zap::ticks_ms()); });
    guarded([] { g_game->pump_popups(); });
    // A received scrap's drop once its popup has shown and the player has settled, as the
    // Windows helper's frame (pump_drops; native/README.md, "The Linux helper").
    guarded([] { g_game->pump_drops(zap::ticks_ms()); });
}

// A call to the helper's swap, from `site`, begun at `start_us`: frame work for a call from the
// frame function, or from a wrapper it calls (kWrapperGapUs above). Returns whether the call came
// through a wrapper, which the caller stamps the return of; any other call, UpdatePalette's say,
// only passes on.
bool game_frame(uintptr_t site, int64_t start_us) {
    if (in_frame_function(site)) {
        frame_work();
        return false;
    }
    if (!called_through_wrapper(site)) return false;
    const bool apart = start_us - g_wrapper_returned_us.load(std::memory_order_relaxed) >=
                       kWrapperGapUs;
    const bool overdue = start_us - g_wrapper_ran_us.load(std::memory_order_relaxed) >=
                         kWrapperBackstopUs;
    if (apart || overdue) {
        g_wrapper_ran_us.store(start_us, std::memory_order_relaxed);
        frame_work();
    }
    return true;
}

}  // namespace

namespace {

// Frame work runs only for calls from the game's frame function (game_frame). If none of the first
// kAuditCalls come from it while the game's symbols give it, another library's SDL_GL_SwapWindow
// stands between the game and the helper (an overlay or a recorder preloaded ahead of it), and the
// frame requests will all be refused: said once, with whose code the calls come from.
constexpr uint64_t kAuditCalls = 1200;  // some seconds of frames, a few with the title's slowest

void audit_frame_calls() {
    if (g_frame_function.size == 0 || g_frame_calls.load(std::memory_order_relaxed) != 0) return;
    uintptr_t busiest = 0;
    uint64_t most = 0;
    for (int i = 0; i < kCallSites; ++i) {
        uint64_t calls = g_site_calls[i].load(std::memory_order_relaxed);
        if (calls > most) {
            most = calls;
            busiest = g_site[i].load(std::memory_order_acquire);
        }
    }
    Dl_info info{};
    const char* from = busiest != 0 && dladdr(reinterpret_cast<void*>(busiest), &info) != 0 &&
                               info.dli_fname != nullptr
                           ? info.dli_fname
                           : "somewhere else";
    std::fprintf(stderr,
                 "zexion-ap %s: no frame call came from the game's own frame function in %llu "
                 "calls; they come through %s: put libzexion_ap.so first in LD_PRELOAD\n",
                 kVersion, static_cast<unsigned long long>(kAuditCalls), from);
}

}  // namespace

// The game's per-frame call (native/README.md, "The Linux helper"). SDL2's returns nothing and
// SDL3's a bool: returned as an int, which an SDL2 caller ignores.
extern "C" __attribute__((visibility("default"))) int SDL_GL_SwapWindow(void* window) {
    bool wrapped = false;
    if (g_active.load(std::memory_order_relaxed)) {
        const int64_t start_us = now_us();  // before the walk, which may stall
        const uint64_t calls = g_frames.fetch_add(1, std::memory_order_relaxed) + 1;
        auto site = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        count_call_site(site);
        wrapped = game_frame(site, start_us);
        if (calls == kAuditCalls) audit_frame_calls();
    }
    Swap next = g_next_swap;
    if (next == nullptr) {  // SDL loaded after the constructor ran: look once more
        next = reinterpret_cast<Swap>(dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
        if (next == nullptr) return 0;
    }
    const int result = next(window);
    // Where a wrapper's repeated call is told from the next frame's (game_frame).
    if (wrapped) g_wrapper_returned_us.store(now_us(), std::memory_order_relaxed);
    return result;
}

// Steam's SteamInternal_ContextInit, in libsteam_api's place: the dynamic linker binds the game's
// call (its two accessors', SteamUserStats() and SteamUtils(): docs/re/linux.md, "Mapped by
// disassembly: the Feats guard's Steam and load test") to this definition, the preloaded
// library's. The Feats guard answers the stats context while it blocks (feats.h,
// feats_context_init); every other call goes on to libsteam_api's own, found after the helper
// (never through the game's slot, which holds this one). In any other program, and before the
// helper knows the stats context (the game's first call, SteamUtils(), comes before its config),
// everything goes on.
extern "C" __attribute__((visibility("default"))) void* SteamInternal_ContextInit(void* context) {
    return zap::feats_context_init(context);
}
