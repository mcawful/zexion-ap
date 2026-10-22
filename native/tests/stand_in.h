// A stand-in "game" for the native tests: this program's own data and functions, laid out
// as the helper expects the game's (player state, inventory, item records, the popup's last
// message, and functions with the traced prototypes). Configured by RVA like the real game.
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace stand_in {

inline BYTE g_player[0x2000];  // player state; the inventory is at +0x108E
inline BYTE* const g_inventory = g_player + 0x108E;
inline BYTE g_save_loaded = 0;
inline BYTE g_menu = 0;
inline BYTE g_records[51 * 100];
inline BYTE* g_records_ptr = g_records;
struct LastMessage {
    BYTE* last;
    int32_t sequence;
};
inline LastMessage g_last_message = {nullptr, 0};
inline BYTE g_message[0x200];
inline BYTE g_strip_gate = 0;
inline BYTE g_sound_system[16];
// The randomizer section, its marker, and the new-game routine the game queues with a timer.
inline BYTE g_section[0x908 + 16];
inline uint16_t g_rando_marker = 0;
inline void* g_transition_fn = nullptr;
inline int32_t g_transition_timer = 0;
inline int32_t g_current_slot = 0;
inline int g_newgames = 0;
inline void newgame_fn() { ++g_newgames; }

// The game-mode panel's surroundings: the most recent menu, the randomizer menu's state, the
// menu functions, and START GAME's steps, which log their order and queue a new game as the
// game's do (unless the slot is "taken").
inline BYTE* g_current_menu = nullptr;
inline BYTE g_randomizer_menu[0x140];
inline void widget_handler_fn() {}
inline void panel_handler_fn() {}
inline void rando_menu_fn(void*, int) {}
inline std::string g_steps;
inline bool g_slot_taken = false;
inline void start_choice_fn() { g_steps += "start "; }
inline void placement_fn() {
    g_steps += "placement ";
    g_section[0x310] = 0x77;  // the game's own start, for the plan to write over
}
inline void rando_newgame_fn() {
    g_steps += "queue ";
    if (g_slot_taken) return;
    g_transition_fn = reinterpret_cast<void*>(&newgame_fn);
    g_transition_timer = 40;
    g_rando_marker = 1;
}

template <typename T>
T& field(ptrdiff_t offset) {
    return *reinterpret_cast<T*>(g_player + offset);
}

inline std::string g_text;
inline int g_message_args[5];
inline bool g_message_fails = false;
inline int g_messages = 0;
inline void* message_fn(const char* text, int per_line, int x, int y, int, int, int frames) {
    g_text = text;
    g_message_args[0] = per_line;
    g_message_args[1] = x;
    g_message_args[2] = y;
    g_message_args[3] = frames;
    ++g_messages;
    return g_message_fails ? nullptr : g_message;
}

inline void* g_sound_system_seen = nullptr;
inline short g_sound = -1;
inline float g_sound_x = 0, g_sound_y = 0, g_sound_volume = 0, g_sound_pitch = 0;
inline short g_sound_category = 0;
inline void sound_fn(void* system, short id, float x, float y, float volume, float pitch, short,
              short category) {
    g_sound_system_seen = system;
    g_sound = id;
    g_sound_x = x;
    g_sound_y = y;
    g_sound_volume = volume;
    g_sound_pitch = pitch;
    g_sound_category = category;
}

inline std::string g_calls;
inline double g_hud_values[2];
inline int g_hud_numbers = 0;
inline void hud_number_fn(void* object, double value) {
    if (object == g_player + 0xB30) g_hud_values[0] = value;
    if (object == g_player + 0xBB8) g_hud_values[1] = value;
    ++g_hud_numbers;
    g_calls += "number ";
}
inline void hud_layout_fn(void* player) { g_calls += player == g_player ? "layout " : "layout? "; }
inline char g_energy_force = 0;
inline void energy_hud_fn(void* player, char force) {
    g_energy_force = force;
    g_calls += player == g_player ? "energy " : "energy? ";
}
inline void weapon_strip_fn(void* player) { g_calls += player == g_player ? "strip " : "strip? "; }
inline short g_palette = -1;
inline void suit_color_fn(int slot, short palette) {
    g_palette = palette;
    g_calls += slot == 0 ? "suit " : "suit? ";
}

inline uint64_t rva(const void* p) {
    return reinterpret_cast<uint64_t>(p) - reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
}

inline std::map<std::string, uint64_t> stand_in_config() {
    return {
        {"inventory", rva(g_inventory)},
        {"save_loaded", rva(&g_save_loaded)},
        {"menu_state", rva(&g_menu)},
        {"item_records", rva(&g_records_ptr)},
        {"last_message", rva(&g_last_message)},
        {"weapon_strip_gate", rva(&g_strip_gate)},
        {"sound_system", rva(g_sound_system)},
        {"message_fn", rva(reinterpret_cast<void*>(&message_fn))},
        {"sound_fn", rva(reinterpret_cast<void*>(&sound_fn))},
        {"hud_number_fn", rva(reinterpret_cast<void*>(&hud_number_fn))},
        {"hud_layout_fn", rva(reinterpret_cast<void*>(&hud_layout_fn))},
        {"energy_hud_fn", rva(reinterpret_cast<void*>(&energy_hud_fn))},
        {"weapon_strip_fn", rva(reinterpret_cast<void*>(&weapon_strip_fn))},
        {"suit_color_fn", rva(reinterpret_cast<void*>(&suit_color_fn))},
        {"rando_blob", rva(g_section)},
        {"rando_marker", rva(&g_rando_marker)},
        {"transition_fn", rva(&g_transition_fn)},
        {"transition_timer", rva(&g_transition_timer)},
        {"current_slot", rva(&g_current_slot)},
        {"newgame_fn", rva(reinterpret_cast<void*>(&newgame_fn))},
        {"current_menu", rva(&g_current_menu)},
        {"randomizer_menu", rva(g_randomizer_menu)},
        {"widget_handler_fn", rva(reinterpret_cast<void*>(&widget_handler_fn))},
        {"panel_handler_fn", rva(reinterpret_cast<void*>(&panel_handler_fn))},
        {"rando_menu_fn", rva(reinterpret_cast<void*>(&rando_menu_fn))},
        {"start_choice_fn", rva(reinterpret_cast<void*>(&start_choice_fn))},
        {"placement_fn", rva(reinterpret_cast<void*>(&placement_fn))},
        {"rando_newgame_fn", rva(reinterpret_cast<void*>(&rando_newgame_fn))},
    };
}

inline void reset_game() {
    std::memset(g_player, 0, sizeof(g_player));
    g_save_loaded = 3;
    g_menu = 0;
    field<int16_t>(0xCEA) = 48;  // energy
    field<int16_t>(0xCEC) = 48;
    field<int16_t>(0xCBC) = 2;  // missiles, of a max of 3
    field<int16_t>(0xCC0) = 3;
    field<double>(0x1100) = 120.5;
    field<double>(0x1108) = 64.0;
    g_inventory[0] = 3;
    g_inventory[1] = 3;
    std::memset(g_records, 0, sizeof(g_records));
    BYTE* box = g_records + 36 * 100;  // Missile Box
    box[0x08] = 0xC5;                   // icon
    std::memcpy(box + 0x0A, "MISSILE BOX", sizeof("MISSILE BOX"));
    *reinterpret_cast<int16_t*>(box + 0x5A) = 61;  // sound
    *reinterpret_cast<float*>(box + 0x5C) = 1.0f;
    g_calls.clear();
    g_hud_numbers = 0;
    g_messages = 0;
    g_message_fails = false;
    g_last_message = {nullptr, 0};
    g_strip_gate = 0;
    g_palette = -1;
    g_sound = -1;
}

}  // namespace stand_in
