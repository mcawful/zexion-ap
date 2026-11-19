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

#include <algorithm>
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
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
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
// games and the ARCHIPELAGO choice, `apply` and its popups, other players' items, this world's
// own traps and disks and the received ones, and the rest native/README.md's "The Linux helper"
// lists; the rest's addresses stay null and nothing calls them.
constexpr DWORD kFrameWaitMs = 1000;  // as the Windows helper's
// Both constructed before the helper's own constructor (start) runs, which sets the game's title
// version and starts the serving thread: a constructor function runs, in the library's
// .init_array, before this file's ordinary dynamic initializers, which would otherwise build
// these afresh over what start set (init_priority puts theirs first).
__attribute__((init_priority(101))) zap::Mailbox g_mailbox;
__attribute__((init_priority(101))) zap::Game g_game;  // game thread only
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
    std::vector<std::string> names{kFrameFunction, kSpriteDefCount, kPlayerPointer, kStatsContext,
                                   kSetAchievement};
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

// The map's move procedure while the names are up (mapmarks.h, MapCursorNames): the game's own,
// MapMoveProc, which the game calls with the map's child alone (docs/re/linux.md, `map_handler`),
// then the cursor's name written over a stand-in's in the frame the game wrote it.
void map_hook(void* child) {
    auto original = reinterpret_cast<void (*)(void*)>(g_game.map_original(child));
    if (original != nullptr) original(child);
    try {
        g_game.map_after();
    } catch (...) {
    }
}

// EQUIPMENT's callback while the disks are counted (equip.h): the game's screen, then the disk's
// widget added. The game calls a menu callback with no arguments on Linux (`map_hook`'s note).
void equipment_hook() {
    auto original = reinterpret_cast<void (*)()>(g_game.equipment_fn());
    if (original != nullptr) original();
    try {
        g_game.equipment_built();
    } catch (...) {
    }
}

// The Energy Tank panel's handler while it shows the disk (equip.h): the game's, then the disk's
// name and description when it is hovered or selected. Handlers take (widget, event) on Linux too.
void equipment_panel_hook(void* widget, int event) {
    auto original = reinterpret_cast<void (*)(void*, int)>(g_game.equip_panel_fn());
    if (original != nullptr) original(widget, event);
    try {
        g_game.equipment_event(widget, event);
    } catch (...) {
    }
}

// The pause menu's way back from OPTION MENU (pause.h): the game's, then the disk row turned in
// the same call (refreshPauseFromOption takes the menu's screen).
void options_back_hook(void* menu) {
    auto original = reinterpret_cast<void (*)(void*)>(g_game.options_back_fn());
    if (original != nullptr) original(menu);
    try {
        g_game.options_returned(menu);
    } catch (...) {
    }
}

// RETURN HOME WITH THE DISKS? (mission.h): the opener and the ending run as the game's
// transition; YES is the prompt's menu callback, which the game calls with no arguments on Linux,
// and so is the game's NO it calls once the helper has taken YES.
void mission_open_hook() {
    try {
        g_game.mission_open();
    } catch (...) {
    }
}

void mission_yes_hook() {
    void* no = nullptr;
    try {
        no = g_game.mission_yes();
    } catch (...) {
    }
    if (no == nullptr) return;
    reinterpret_cast<void (*)()>(no)();
    try {
        g_game.mission_after_no();
    } catch (...) {
    }
}

void mission_end_hook() {
    try {
        g_game.mission_end();
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

// Game thread: the game configured with `found` (parse_config's: always the game's own, so one
// after the first changes nothing the choice or the popups hold), a client's (`active`) or the
// helper's own (configure_self).
void install_config(const zap::GameAddresses& found, bool active) {
    g_game.set_hooks({.panel = reinterpret_cast<void*>(&panel_hook),
                      .equipment = reinterpret_cast<void*>(&equipment_hook),
                      .equipment_panel = reinterpret_cast<void*>(&equipment_panel_hook),
                      .mission_open = reinterpret_cast<void*>(&mission_open_hook),
                      .mission_yes = reinterpret_cast<void*>(&mission_yes_hook),
                      .mission_end = reinterpret_cast<void*>(&mission_end_hook),
                      .map = reinterpret_cast<void*>(&map_hook),
                      .options_back = reinterpret_cast<void*>(&options_back_hook)});
    g_game.configure(found, active);
    // The icon sheets as traced only with the game's definitions counted as traced, read again at
    // each config (a self-config at the first frame may come before the game counts them).
    int16_t defs = 0;
    auto* count = reinterpret_cast<const BYTE*>(g_sprite_def_count);
    g_game.hold_icon_sheets(count == nullptr || !zap::read(count, 0, defs) ||
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
std::mutex g_offline_mutex;
std::optional<zap::OfflineFiles> g_offline_files;  // read, not yet taken
struct PutSeed {
    zap::OfflineSeed seed;
    bool kept;  // its file written ("stored", "unchanged")
};
std::vector<PutSeed> g_put_seeds;  // put since, not yet taken
// The folder, found once in the constructor (getenv there, before the game's threads run: never
// while they may set the environment).
std::wstring g_offline_folder;

// Serving thread (file I/O; never the frame call). A file that could not be read is said once.
void read_offline() {
    zap::OfflineFiles files = zap::read_offline_files(g_offline_folder, g_build);
    static std::string said;
    if (!files.error.empty() && files.error != said) {
        std::fprintf(stderr, "zexion-ap %s: seed files: %s\n", kVersion, files.error.c_str());
        said = files.error;
    }
    std::lock_guard<std::mutex> lock(g_offline_mutex);
    g_offline_files = std::move(files);
}

// Serving thread: a seed a client has just put, for the next frame (one of its name waiting is
// replaced, as on Windows).
void note_put_seed(zap::OfflineSeed seed, bool kept) {
    std::lock_guard<std::mutex> lock(g_offline_mutex);
    auto same = std::find_if(g_put_seeds.begin(), g_put_seeds.end(),
                             [&](const PutSeed& s) { return s.seed.name == seed.name; });
    if (same != g_put_seeds.end()) {
        *same = {std::move(seed), kept};
    } else {
        g_put_seeds.push_back({std::move(seed), kept});
    }
}

// Game thread: with no client yet, the helper configured from the game's own symbols (the
// addresses a client's config must give: parse_config), inactive, as the Windows helper's cached
// config is: only what runs with no client uses it. Only with a seed file read to work from, so
// a game with none is left alone until a client configures, and only once the game's player
// pointer reads where the config's player state is (tried again each frame until then).
bool g_seed_files = false;  // a seed file has been read
void configure_self() {
    static bool failed = false;
    if (failed || !g_seed_files || g_game.configured() || !g_expected_known) return;
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
    static std::vector<zap::OfflineSeed> unkept;
    std::optional<zap::OfflineFiles> files;
    std::vector<PutSeed> put;
    if (g_offline_mutex.try_lock()) {
        files = std::exchange(g_offline_files, std::nullopt);
        put = std::exchange(g_put_seeds, {});
        g_offline_mutex.unlock();
    }
    if (files) {
        if (!files->seeds.empty()) g_seed_files = true;
        if (g_game.active()) {
            g_game.merge_offline(std::move(files->seeds));
        } else {
            g_game.set_offline(std::move(files->seeds));
            if (!unkept.empty()) g_game.merge_offline(std::vector<zap::OfflineSeed>(unkept));
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
    if (!seeds.empty()) g_game.merge_offline(std::move(seeds));  // after the files, read earlier
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
                return std::string(g_game.arm_newgame(std::move(plan)) ? "armed"
                                                                       : "refused not configured");
            },
            true, error);
    }
    if (request == "disarm") {
        return on_game([] { g_game.disarm_newgame(); return std::string("disarmed"); }, false);
    }
    if (request.starts_with("ap_labels\n") || request == "ap_labels") {
        std::map<int, zap::Label> labels;
        std::string error = zap::parse_labels(std::string(request), labels);
        return on_game(
            [&] {
                size_t n = labels.size();
                g_game.set_labels(std::move(labels));
                return "labeled " + std::to_string(n);
            },
            true, error);
    }
    if (request == "lang") {
        // The language the game shows, one word (as the Windows helper's hello's `lang`): "?"
        // unconfigured or with no translation's name readable.
        return on_game(
            [] {
                std::string name = g_game.language();
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
                g_game.set_discs(count, required, std::move(line), told);
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
                return std::string(g_game.request_mission(column, row) ? "queued"
                                                                       : "refused not configured");
            },
            true, error);
    }
    if (request == "mission_status") {
        return on_game([] { return g_game.mission_status(); }, true);
    }
    if (request == "mission_black") {
        return on_game([] { return g_game.mission_black(); }, true);
    }
    if (request == "discs_status") {
        return on_game([] { return g_game.equipment_status(); }, false);
    }
    if (request == "trap_status") {
        return on_game([] { return g_game.trap_status(); }, true);
    }
    if (request == "shield") {  // a received Energy Shield (game_calls.h, spawn_shield)
        return on_game([] { return g_game.spawn_shield(zap::ticks_ms()); }, false);
    }
    if (request == "other_save") {  // a client of another seed's: the save's look kept
        return on_game([] { return g_game.other_save(); }, true);
    }
    if (request == "client_back") {  // CLIENT CONNECTED, once, for a save dressed with none
        return on_game([] { return std::string(g_game.client_back() ? "shown" : "none"); }, true);
    }
    if (request == "assists lock" || request == "assists free") {  // the ASSIST lock (menu.h)
        const bool lock = request == "assists lock";
        return on_game(
            [lock] {
                g_game.set_assists_locked(lock);
                return std::string(lock ? "locked" : "free");
            },
            true);
    }
    if (request == "intel on" || request == "intel off") {  // Intel Chips (intel.h)
        const bool on = request == "intel on";
        return on_game([on] { g_game.set_intel(on); return std::string(on ? "intel on" : "intel off"); },
                       true);
    }
    if (request == "die") {  // a DeathLink death received (game_calls.h, kill)
        return on_game([] { return g_game.kill(zap::ticks_ms()); }, false);
    }
    if (request == "feats_status") {  // the Feats guard (feats.h, state): checks
        return on_game([] { return g_game.feats_state(); }, false);
    }
    if (request == "title_status") {  // the title's lines (game_calls.h, title_state): checks
        return on_game([] { return g_game.title_state(); }, false);
    }
    if (request == "drop_status") {  // the received scraps' drops (game_calls.h): live checks
        return on_game([] { return g_game.drop_status(); }, false);
    }
    if (request.starts_with("offline_put ")) {
        // Here, on the serving thread: the seed's file written into the helper's folder
        // (offline.cpp, offline_folder), and its text the running helper's from the next frame
        // (take_offline) whether the file could be kept or not, as on Windows.
        zap::OfflineSeed seed;
        std::string reply =
            zap::put_offline_file(g_offline_folder, g_build, std::string(request), &seed);
        if (!seed.name.empty()) {
            note_put_seed(std::move(seed), reply == "stored" || reply == "unchanged");
        }
        return reply;
    }
    if (request == "newgame_status") {
        return on_game([] { return g_game.newgame_status(); }, false);
    }
    if (request.starts_with("apply\n") || request == "apply") {
        zap::Batch batch;
        std::string error = zap::parse_batch(std::string(request), batch);
        return on_game(
            [&] { return g_game.apply(batch); }, true, error);
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
    read_offline();  // the seeds' files, for the first frame (take_offline)
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
            read_offline();        // and the seeds' files read again, as there
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

extern "C" void* SteamInternal_ContextInit(void* context);  // defined at the end, exported

__attribute__((constructor)) void start() {
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
    g_folder = !folder.empty() && folder == folder_of("/proc/self/exe");
    g_offline_folder = zap::offline_folder();  // here, before the game's threads (getenv)
    g_game.set_title_version(kVersion);  // the title's version line (title.h), as on Windows
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

// The game's thread, at its frame swap (SWHardwareEnd's call: `site`, the return address): a
// client gone's plan and labels dropped, the mailbox's work, an armed new game watched, the
// ARCHIPELAGO choice, other players' items, the traps, then the next popup. Each part guarded on
// its own, so one that throws keeps none of the others from running, and nothing reaches the
// game. A fault in the game's own placement is
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
    // First, as the Windows helper's frame: the cell clock a death, a shield or a drop waits on
    // (settled) is this frame's when the mailbox's requests run (note_play does nothing until
    // configured).
    guarded([] { g_game.note_play(zap::ticks_ms()); });
    // A client's popups stay queued for its save, as on Windows; its plan, its labels, its Intel
    // Chips and its ASSIST lock go (the records, the popup's width, the entry and the pixels put
    // back); the look without the client (watch_offline, below) may dress the save again.
    // Before the mailbox: the serving thread marks a client gone before it takes the next one,
    // so nothing that next client asks is dropped with the last one's.
    if (g_client_left.exchange(false)) {
        guarded([] { g_game.client_left(); });  // an own disk's count the helper's again
        // The look without the client dressed again from scratch at the next look, its
        // not-connected notice with it, as client_gone does on Windows (put_back_game).
        guarded([] { g_game.leave_offline(); });
        guarded([] { g_game.disarm_newgame(); });
        guarded([] { g_game.restore_ap_items(); });
        guarded([] { g_game.restore_intel(); });  // the Area Map's record the game's again
        guarded([] {  // ASSIST unlocked, as drop_save_look on Windows
            g_game.set_assists_locked(false);
            g_game.restore_assists();
        });
        guarded([] { g_game.restore_translation(); });  // the names, entry 70, the icons
        guarded([] {  // the disks' count off and put back, as drop_save_look on Windows
            g_game.set_discs(0, 0, "");
            g_game.restore_discs();
        });
        // Its notices not shown yet withdrawn (another seed's, CLIENT CONNECTED.), as client_gone
        // does on Windows: the offline look dresses the save again with no client from here.
        guarded([] { g_game.drop_client_notices(); });
    }
    // The seeds' files read since (and, with none configured, the game's own config), before
    // the mailbox as on Windows: a client's config this frame comes after them.
    guarded([] { take_offline(); });
    guarded([] { g_mailbox.serve(); });
    if (!g_game.configured()) return;
    // Feats and Steam achievements kept out of AP saves: the guard on while one is loaded (and on
    // Linux, after an AP ending the patch did not jump over, until a save loads), and the load
    // test over the slots holding them
    // (feats.h; the export below decides each call). Before the gate below, so what the export
    // blocks follows the game whatever the player pointer reads. Its writes come before the gate
    // too: the ending's patch, which checks its own bytes and the call's target first, and the
    // Feats of Natu screen's note, which writes only into a top menu of that screen's whole shape
    // (MISSION COMPLETE's statistics under it, a live title widget, every glyph writable), and
    // only after an ending the patch jumped over.
    guarded([] { g_game.watch_feats(); });
    // Configured with no client: nothing touches the game while its player pointer reads
    // elsewhere, as the client's attach checks (it read the player state at the title screen
    // before any save, and in play: docs/re/linux.md, "Read from the running game").
    if (!g_game.active() && !player_where_expected()) return;
    try {
        g_game.watch_newgame();
    } catch (...) {
        guarded([] { g_game.refuse_newgame("the placement failed"); });
    }
    // The ARCHIPELAGO choice while a plan offers it, RANDOMIZER back otherwise.
    guarded([] { g_game.watch_menu(&archipelago_chosen); });
    // An AP save's look with no client, or with one of another seed (labels, INTEL CHIP, ASSIST,
    // the disks), set before the watches below apply it, as watch_all orders it on Windows.
    guarded([] { g_game.watch_offline(); });
    // Other players' items: the stand-ins' records named and restyled, the logo painted, the
    // popup's width, other Zexion players' items dressed as themselves (apitem.h, looks.h).
    guarded([] { g_game.watch_map_names(); });  // the cursor's names while the map is open
    guarded([] { g_game.watch_ap_items(); });
    guarded([] { g_game.watch_intel(); });  // the Area Map named INTEL CHIP while `intel on`
    guarded([] { g_game.watch_assists(); });  // ASSIST locked in the options menu while asked
    guarded([] { g_game.watch_title(); });  // the title screen's lines, with no save loaded
    guarded([] { g_game.watch_map_marks(); });  // the squares raised, the on-screen write patched
    // In a translation: the helper's names in its tables, its icons in its sheets, the popups'
    // borrowed entries emptied once their message has gone (translation.h).
    guarded([] { g_game.watch_translation(); });
    // The OS Install Disks' count: the scanner's line, the equipment screen's disk and the pause
    // screen's disk row, the disk glyph (equip.h, pause.h, scan.h).
    guarded([] { g_game.watch_discs(); });
    guarded([] { g_game.watch_equipment(); });
    guarded([] { g_game.watch_mission(); });  // RETURN HOME WITH THE DISKS? (mission.h)
    // Traps: this world's own at their pickup (from the seed a client put), the AILMENTS tests'
    // patches while a trap's timer runs, then one trap set off (a received one as its popup
    // shows), as the Windows helper's frame (trap.h; native/README.md, "Traps").
    guarded([] { g_game.watch_own_traps(); });
    // This world's own disks: their cells watched with a client or none, a pickup's count shown
    // with none, as the Windows helper's (game_offline.cpp).
    guarded([] { g_game.watch_own_disks(); });
    guarded([] { g_game.watch_trap_patches(); });
    guarded([] { g_game.pump_traps(zap::ticks_ms()); });
    guarded([] { g_game.pump_popups(); });
    // A received scrap's drop once its popup has shown and the player has settled, as the
    // Windows helper's frame (pump_drops; native/README.md, "The Linux helper").
    guarded([] { g_game.pump_drops(zap::ticks_ms()); });
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
