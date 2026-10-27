// Where the game's state and functions are: one member per config key (game_calls.cpp,
// kConfigKeys, which fills them from the RVAs the client resolved by byte pattern:
// client/patterns.py), and the helper's wrappers the game is pointed at. Game keeps one and
// hands it to every part. The client-only keys' members (kConfigKeys, client_only) may be null
// after the offline cache's configure: only the client's parts read them (Game::client_ready).
// Function arguments as traced in docs/re/memory.md, "Game functions the helper calls"; each
// part's header says what it reads.
#pragma once

#include <windows.h>

namespace zap {

// The helper's wrappers the game is pointed at while a part is on (dllmain.cpp).
struct Hooks {
    void* panel = nullptr;            // the game-mode panel's handler (menu.h)
    void* equipment = nullptr;        // EQUIPMENT's callback (equip.h)
    void* equipment_panel = nullptr;  // the Energy Tank panel's handler (equip.h)
    void* mission_open = nullptr;     // the mission prompt's opener, no arguments (mission.h)
    void* mission_yes = nullptr;      // the prompt's YES callback (mission.h)
    void* file_entry = nullptr;       // a save entry's handler (files.h)
    void* map = nullptr;              // the map's per-frame handler (mapmarks.h)
};

struct GameAddresses {
    // The player state, the save and the menus.
    BYTE* inventory = nullptr;  // in the player state (game_calls.cpp)
    BYTE* save_loaded = nullptr;
    BYTE* menu_state = nullptr;
    BYTE* item_records = nullptr;  // holds the pointer to the item records
    BYTE* last_message = nullptr;  // holds a pointer; the sequence number follows at +8
    BYTE* weapon_strip_gate = nullptr;
    BYTE* sound_system = nullptr;
    BYTE* message_fn = nullptr;
    BYTE* sound_fn = nullptr;
    BYTE* hud_number_fn = nullptr;
    BYTE* hud_layout_fn = nullptr;
    BYTE* energy_hud_fn = nullptr;
    BYTE* weapon_strip_fn = nullptr;
    BYTE* suit_color_fn = nullptr;
    // New games (requests.h, NewGamePlan).
    BYTE* rando_blob = nullptr;  // the loaded randomizer section, kSectionSize bytes
    BYTE* rando_marker = nullptr;
    BYTE* transition_fn = nullptr;     // holds the routine the game runs when its timer ends
    BYTE* transition_timer = nullptr;  // 32-bit frames left
    BYTE* current_slot = nullptr;      // 32-bit, 0-based
    BYTE* newgame_fn = nullptr;
    BYTE* current_menu = nullptr;  // holds a pointer to the top open menu
    BYTE* randomizer_menu = nullptr;  // the randomizer menu's state, kMenuSize bytes
    BYTE* widget_handler_fn = nullptr;  // the generic widget handler
    BYTE* panel_handler_fn = nullptr;   // the game-mode panel's handler
    BYTE* rando_menu_fn = nullptr;      // RANDOMIZER's own callback
    BYTE* placement_fn = nullptr;
    BYTE* rando_newgame_fn = nullptr;
    BYTE* event_table = nullptr;  // holds the pointer to the loaded area events
    BYTE* event_count = nullptr;
    BYTE* start_count = nullptr;  // 16-bit
    BYTE* start_cells = nullptr;  // (x, y) 16-bit each
    BYTE* start_flags = nullptr;
    // Other players' items (apitem.h, looks.h).
    BYTE* sprite_texture = nullptr;  // holds the pointer to the sprite data
    // The mov edx, 24 that sets the pickup popup's width for items with no icon (the
    // stand-ins); set to the named label's width (docs/re/memory.md, "Other players' items").
    BYTE* popup_width_code = nullptr;
    BYTE* item_pool = nullptr;      // 128 item objects, 0x3B0 bytes each
    BYTE* palette_table = nullptr;  // the palette objects by number (a glyph's +0x198, +0x1A0)
    BYTE* frame_clock = nullptr;    // 64-bit count the item animations follow
    BYTE* sprite_templates = nullptr;  // holds the pointer to the sprite definitions' templates
    BYTE* event_handlers = nullptr;    // the area-event handlers, by event type
    BYTE* death_fn = nullptr;
    BYTE* halt_flag = nullptr;
    BYTE* assist_menu_fn = nullptr;  // ASSIST's own callback (the options menu, a save loaded)
    BYTE* object_pool = nullptr;  // the general objects, 1000 of 0x5C8 bytes
    BYTE* text_table = nullptr;   // holds the pointer to the loaded Text table
    // The OS Install Disks goal (equip.h, pause.h, files.h, mission.h).
    BYTE* equipment_fn = nullptr;    // EQUIPMENT's callback: builds the screen
    BYTE* equip_panel_fn = nullptr;  // the equipment screen's panel handler
    BYTE* widget_move_fn = nullptr;  // (widget, dx, dy, set): places a widget and its label
    BYTE* text_place_fn = nullptr;   // (text, x, y): places a text object's glyphs
    BYTE* malloc_slot = nullptr;     // the import slots of the C runtime's malloc and free
    BYTE* free_slot = nullptr;
    BYTE* widget_new_fn = nullptr;    // makes a widget (label, glyphs, panel, handler, ...)
    BYTE* sprite_sheet_fn = nullptr;  // (object, template): gives it a sprite definition's frames
    BYTE* panel_link_fn = nullptr;    // (panel): links its widgets into the menu's draw list
    BYTE* text_set_fn = nullptr;      // (text, string, first glyph, glyphs)
    BYTE* routine_table = nullptr;    // the transition routines (0x3CF5F0)
    BYTE* fade = nullptr;             // the global level's fade (mission.h, fade_state)
    BYTE* fade_level = nullptr;       // the global level, a signed byte
    BYTE* keys_branch = nullptr;      // the `jne` (75 2E) that skips the pause screen's KEYS
    BYTE* file_panel_fn = nullptr;    // the save entries' panel handler
    // The save entries' handlers: the file select's, COPY FILE's and DELETE FILE's.
    BYTE* file_entry_fn = nullptr;
    BYTE* copy_entry_fn = nullptr;
    BYTE* delete_entry_fn = nullptr;
    // What the offline look reads whole (offline.h).
    BYTE* map_grid = nullptr;
    BYTE* flags = nullptr;
    BYTE* slot_array = nullptr;
    BYTE* rando_slots = nullptr;  // each save slot's copy of the randomizer section
    // The title screen (title.h).
    BYTE* text_new_fn = nullptr;   // (characters, length, 32): a text object and its glyphs
    BYTE* text_link_fn = nullptr;  // (text): its glyphs into its list
    BYTE* draw_layer = nullptr;    // holds the draw list new screen objects join
    BYTE* map_handler = nullptr;   // holds the map's per-frame handler (0x169E50; mapmarks.h)
    // Feats and achievements (feats.h).
    BYTE* ending_unlocks = nullptr;     // the ending's first Steam call (6 bytes)
    BYTE* ending_join = nullptr;        // where it goes on, just before it sets flag 250
    BYTE* stats_context = nullptr;      // ISteamUserStats' accessor context
    BYTE* context_init_slot = nullptr;  // the import slot of SteamInternal_ContextInit

    // Not a key: set before configure (Game::set_hooks). A null hook leaves that part's game
    // function as the game's.
    Hooks hooks;
};

}  // namespace zap
