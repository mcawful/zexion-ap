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
// The pickup popup's `mov edx, 24`, in a section of its own that executes, as in the game's code.
#pragma section(".zapcode", read, execute)
__declspec(allocate(".zapcode")) inline unsigned char g_popup_width_code[16] = {0xBA, 0x18, 0, 0,
                                                                              0,    0xC3};
inline BYTE* g_records_ptr = g_records;
// The live item objects, the palette table, the item clock and the sprite templates
// (docs/re/memory.md, "Live item objects").
inline BYTE g_item_pool[128 * 0x3B0];
inline BYTE g_palette_objects[64][16];
inline BYTE* g_palette_table[64];
inline int64_t g_frame_clock = 1000;
inline BYTE g_frames[152 * 0x38];
inline BYTE g_item_template[0x1D8];
inline BYTE* g_template_list[40];
inline BYTE** g_sprite_templates = g_template_list;
// The area-event handlers; type 11's records what it was called with.
inline int g_spawn_calls = 0;
inline int g_spawn_row = -1, g_spawn_column = -1;
inline BYTE* g_spawn_event = nullptr;
inline void spawn_handler(char row, char column, BYTE* event) {
    ++g_spawn_calls;
    g_spawn_row = row;
    g_spawn_column = column;
    g_spawn_event = event;
}
inline void* g_event_handlers[32];
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
// The loaded area events: 0x28 bytes each, behind a pointer, with their count; event 3665 as
// the game loads it (flag test: flag 250, then 3668, else none).
constexpr int kStandInEvents = 3700;
inline BYTE g_events[kStandInEvents * 0x28];
inline BYTE* g_event_table = g_events;
inline int16_t g_event_count = kStandInEvents;
inline BYTE* event_3665() { return g_events + 3665 * 0x28; }
inline void reset_events() {
    std::memset(g_events, 0, sizeof(g_events));
    BYTE* r = event_3665();
    const int16_t words[] = {250, 3668, -1, -1};
    r[0] = 14;
    std::memcpy(r + 2, words, sizeof(words));
    const int16_t self = 3665;
    std::memcpy(r + 0x24, &self, sizeof(self));
    g_event_table = g_events;
    g_event_count = kStandInEvents;
}
inline int16_t event_else() {
    int16_t v;
    std::memcpy(&v, event_3665() + 6, sizeof(v));
    return v;
}

// The randomizer menu's start table: a count, the rooms' cells, their flags.
inline int16_t g_start_count = 0;
inline int16_t g_start_cells[64 * 2];
inline uint8_t g_start_flags[64];

// The sprite data: [holder] + 0x40 points at the texture object, whose first dword is the GL
// texture name.
inline uint32_t g_texture_object[4] = {7, 0, 0, 0};
inline BYTE g_sprite_data[0x80];
inline BYTE* g_sprite_holder = g_sprite_data;
inline void reset_sprites() {
    std::memset(g_sprite_data, 0, sizeof(g_sprite_data));
    *reinterpret_cast<uint32_t**>(g_sprite_data + 0x40) = g_texture_object;
    g_texture_object[0] = 7;
}

inline BYTE* g_current_menu = nullptr;
inline BYTE g_randomizer_menu[0x114];
inline void widget_handler_fn() {}
inline void panel_handler_fn() {}
inline void rando_menu_fn(void*, int) {}
inline std::string g_steps;
inline bool g_slot_taken = false;
// The placement: reads the start, copies the menu's amounts into the section, and writes its
// own item table for the plan to write over.
inline uint8_t g_start_seen = 0;
inline void placement_fn() {
    g_steps += "placement ";
    g_start_seen = g_section[0x310];
    std::memcpy(g_section + 0x6C0, g_randomizer_menu + 0x88, 12);
    g_section[0x26C] = 0x77;
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

// The death routine and the byte it returns early on.
inline BYTE g_halt_flag = 0;
inline int g_deaths = 0;
inline void* g_death_player = nullptr;
inline int16_t g_death_energy = -100;
inline char g_death_kind = -1;
inline void death_fn(void* player, char kind) {
    ++g_deaths;
    g_death_player = player;
    g_death_kind = kind;
    std::memcpy(&g_death_energy, static_cast<BYTE*>(player) + 0xCEA, sizeof(g_death_energy));
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
        {"placement_fn", rva(reinterpret_cast<void*>(&placement_fn))},
        {"rando_newgame_fn", rva(reinterpret_cast<void*>(&rando_newgame_fn))},
        {"event_table", rva(&g_event_table)},
        {"event_count", rva(&g_event_count)},
        {"start_count", rva(&g_start_count)},
        {"start_cells", rva(g_start_cells)},
        {"start_flags", rva(g_start_flags)},
        {"sprite_texture", rva(&g_sprite_holder)},
        {"popup_width_code", rva(g_popup_width_code)},
        {"item_pool", rva(g_item_pool)},
        {"palette_table", rva(g_palette_table)},
        {"frame_clock", rva(&g_frame_clock)},
        {"sprite_templates", rva(&g_sprite_templates)},
        {"event_handlers", rva(g_event_handlers)},
        {"death_fn", rva(reinterpret_cast<void*>(&death_fn))},
        {"halt_flag", rva(&g_halt_flag)},
    };
}

inline void reset_game() {
    std::memset(g_item_pool, 0, sizeof(g_item_pool));
    std::memset(g_event_handlers, 0, sizeof(g_event_handlers));
    g_event_handlers[11] = reinterpret_cast<void*>(&spawn_handler);
    g_spawn_calls = 0;
    g_spawn_event = nullptr;
    g_halt_flag = 0;
    g_deaths = 0;
    g_death_player = nullptr;
    g_death_energy = -100;
    g_death_kind = -1;
    for (int i = 0; i < 64; ++i) g_palette_table[i] = g_palette_objects[i];
    g_frame_clock = 1000;
    g_template_list[34] = g_item_template;
    BYTE* frames = g_frames;
    std::memcpy(g_item_template + 0x1A8, &frames, sizeof(frames));
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
