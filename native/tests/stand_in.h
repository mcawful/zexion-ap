// A stand-in "game" for the native tests: this program's own data and functions, laid out
// as the helper expects the game's (player state, inventory, item records, the popup's last
// message, and functions with the traced prototypes). Configured by RVA like the real game.
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "game_calls.h"

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
// The general object pool and the object definitions (items in reversed-gravity rooms).
inline BYTE g_object_pool[1000 * 0x5C8];

inline BYTE g_palette_objects[64][16];
inline BYTE* g_palette_table[64];
inline int64_t g_frame_clock = 1000;
inline BYTE g_frames[152 * 0x38];
inline BYTE g_item_template[0x1D8];
inline BYTE* g_template_list[105];  // through the icon sheets' (101 and 104)
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
// The game's fade (mission.h, fade_state) and the global level it moves.
inline BYTE g_fade[zap::fade_state::kSize] = {};
inline int8_t g_fade_level = 4;
// Sets the fade as 0x1F6C40 does for layer 0: the level, unless 0xFE (left as it is), then the
// step, the countdown (the step) and the target.
inline void set_fade(int8_t target, int8_t level, int8_t step) {
    if (level != static_cast<int8_t>(0xFE)) g_fade_level = level;
    at<int8_t>(g_fade, zap::fade_state::kStep) = step;
    at<int8_t>(g_fade, zap::fade_state::kCountdown) = step;
    at<int8_t>(g_fade, zap::fade_state::kTarget) = target;
}
// Type 1's (the fade, mission.h): counts its calls, keeps each record it was given as it was
// then (the last one's address too), and sets the fade from it (target, level, step words).
inline int g_fade_calls = 0;
inline BYTE* g_fade_event = nullptr;
inline std::vector<std::vector<BYTE>> g_fades;
inline void fade_handler(char, char, BYTE* event) {
    ++g_fade_calls;
    g_fade_event = event;
    g_fades.emplace_back(event, event + 0x28);
    set_fade(static_cast<int8_t>(event[4]), static_cast<int8_t>(event[6]),
             static_cast<int8_t>(event[8]));
}
// The fade done at the normal level, nothing run through type 1 yet.
inline void reset_fade() {
    std::memset(g_fade, 0, sizeof(g_fade));
    g_fade[zap::fade_state::kTarget] = 4;
    g_fade_level = 4;
    g_fade_calls = 0;
    g_fade_event = nullptr;
    g_fades.clear();
}
inline void* g_event_handlers[32];
struct LastMessage {
    BYTE* last;
    int32_t sequence;
};
inline LastMessage g_last_message = {nullptr, 0};
// The four message slots (0x1628 bytes each); a message is slot 0 unless a test picks another.
inline BYTE g_slots[4 * 0x1628];
inline BYTE* const g_message = g_slots;
inline int g_message_slot = 0;
// With `g_slots_live`, the message routine also numbers its message as the game does: its slot
// at +0x1E0, a new serial at +0x1D8 and its frames left at +0x1EC (translation_tests.cpp).
inline bool g_slots_live = false;
inline int32_t g_serial = 100;
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
// The loaded area events: 0x28 bytes each, behind a pointer, with their count; event 3667 as
// the game loads it (the fade to black, type 1: layers 0 to 0, to -1 from 4, a step every 8
// frames, no sound, no next event).
constexpr int kStandInEvents = 3700;
inline BYTE g_events[kStandInEvents * 0x28];
inline BYTE* g_event_table = g_events;
inline int16_t g_event_count = kStandInEvents;
inline BYTE* event_3667() { return g_events + 3667 * 0x28; }
inline void reset_events() {
    std::memset(g_events, 0, sizeof(g_events));
    BYTE* fade = event_3667();
    const int16_t fade_words[] = {0, -1, 4, 8};
    fade[0] = 1;
    std::memcpy(fade + 2, fade_words, sizeof(fade_words));
    put<int16_t>(fade, 0x1A, -1);  // no sound
    put<int16_t>(fade, 0x20, -1);  // no next event
    put<int16_t>(fade, 0x24, 3667);
    g_event_table = g_events;
    g_event_count = kStandInEvents;
}
// The loaded Text table: 32 bytes an entry, behind a pointer; entry 12 the scanner's message.
inline BYTE g_texts[20 * 32];
inline BYTE* g_text_table = g_texts;
inline BYTE* text_12() { return g_texts + 12 * 32; }
inline void reset_texts() {
    std::memset(g_texts, 0, sizeof(g_texts));
    std::memcpy(text_12(), "ENERGY KEYS DETECTED!", 21);
    g_text_table = g_texts;
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
inline void assist_menu_fn(void*, int) {}
// The equipment screen's functions (equip.h), never called by these tests: bodies of their
// own, so the linker keeps them apart.
inline int g_equip_calls = 0;
inline void equipment_fn(void*, int) { g_equip_calls += 1; }
inline void equip_panel_fn() { g_equip_calls += 2; }
inline void widget_move_fn(void*, int, int, bool) { g_equip_calls += 3; }
inline void text_place_fn(void*, int, int) { g_equip_calls += 4; }
inline void* widget_new_fn() { g_equip_calls += 5; return nullptr; }
inline void sprite_sheet_fn(void*, void*) { g_equip_calls += 6; }
inline void panel_link_fn(void*) { g_equip_calls += 7; }
inline void text_set_fn(void*, const char*, int, int) { g_equip_calls += 8; }
inline void file_panel_fn() { g_equip_calls += 9; }
inline void file_entry_fn() { g_equip_calls += 10; }
inline void copy_entry_fn() { g_equip_calls += 11; }
inline void delete_entry_fn() { g_equip_calls += 12; }
// The title screen's (title.h): never called here, as no draw list is set.
inline void* text_new_fn() { g_equip_calls += 13; return nullptr; }
inline void text_link_fn(void*) { g_equip_calls += 14; }
inline BYTE* g_draw_layer = nullptr;
inline void* g_map_handler = nullptr;  // no map open
inline void* g_malloc_slot = nullptr;
inline void* g_routine_table[16] = {};  // the transition routines (mission.h)
// The pause screen's `jne` past its KEYS panel (pause.h), in code as the game's.
__declspec(allocate(".zapcode")) inline unsigned char g_keys_branch[4] = {0x75, 0x2E, 0x90, 0xC3};
// The item update's write of an on-screen item's square (mapmarks.h), as on B1.
constexpr unsigned char kSeenMarkerGame[50] = ZAP_SEEN_MARKER_GAME;
__declspec(allocate(".zapcode")) inline unsigned char g_seen_marker_code[50] = ZAP_SEEN_MARKER_GAME;
inline void* g_free_slot = nullptr;
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

// Item `item`'s name in its record, as the game shows it.
inline std::string record_name(int item) {
    return std::string(reinterpret_cast<const char*>(g_records + item * 100 + 0x0A));
}

inline std::string g_text;
inline int g_message_args[5];
inline bool g_message_fails = false;
inline int g_messages = 0;
inline void* message_fn(const char* text, int per_line, int x, int y, int, int, int frames) {
    // As the game's (0x182670): the box's center at y - 1, its half-height 4 per line past the
    // first plus 7.
    const int len = static_cast<int>(std::strlen(text));
    const int extra = per_line > 0 && len > 0 ? (len - 1) / per_line : 0;
    BYTE* message = g_slots + 0x1628 * g_message_slot;
    *reinterpret_cast<double*>(message + 0x48) = y - 1;
    *reinterpret_cast<int32_t*>(message + 0x1FC) = 4 * extra + 7;
    if (g_slots_live) {
        *reinterpret_cast<int32_t*>(message + 0x1E0) = g_message_slot;
        *reinterpret_cast<int32_t*>(message + 0x1D8) = ++g_serial;
        *reinterpret_cast<int32_t*>(message + 0x1EC) = frames;
    }
    g_text = text;
    g_message_args[0] = per_line;
    g_message_args[1] = x;
    g_message_args[2] = y;
    g_message_args[3] = frames;
    ++g_messages;
    return g_message_fails ? nullptr : message;
}

// The translation (translation.h): the object (the loaded folder's name at +0, the tables'
// pointers at +0x30, +0x48, +0x58 and +0x78, the flag at +0x91, the font at +0xD0), the tables
// as the game's parser leaves them (each entry's header line, its text and a 0, then the
// separator), their counts, the HD message objects and the game's text functions, which keep
// what they were given and do to the objects the helper reads what the game's do.
// Through the font object (+0xD0) and its metrics (+0x40040 to +0x40068 past it).
inline BYTE g_translation[0xD0 + 0x40070];
inline int16_t g_small_count = 0, g_item_name_count = 0, g_large_count = 0;
inline BYTE g_hd_messages[4 * 0x238];
struct LoadedTable {
    std::vector<wchar_t> text;
    std::vector<int32_t> offsets;
};
// Every table loaded, kept for the test program's life: a language change leaves the old ones
// as they were last written, so a test can see the helper did not write into them.
inline std::vector<std::unique_ptr<LoadedTable>> g_loaded;
inline LoadedTable* g_small = nullptr;
inline LoadedTable* g_items = nullptr;
inline LoadedTable* g_large = nullptr;
// A table of `count` entries, entry k's text `texts[k]` (none: offset -1).
inline LoadedTable* make_table(int count, const std::map<int, std::wstring>& texts) {
    auto t = std::make_unique<LoadedTable>();
    for (int k = 0; k < count; ++k) {
        std::wstring header = std::to_wstring(k) + L")ENTRY " + std::to_wstring(k) + L"\r\n";
        t->text.insert(t->text.end(), header.begin(), header.end());
        auto it = texts.find(k);
        if (it == texts.end()) {
            t->offsets.push_back(-1);
            continue;
        }
        t->offsets.push_back(static_cast<int32_t>(t->text.size()));
        t->text.insert(t->text.end(), it->second.begin(), it->second.end());
        const std::wstring after = std::wstring(1, L'\0') + L"\n-----\r\n";
        t->text.insert(t->text.end(), after.begin(), after.end());
    }
    t->text.push_back(L'\0');
    t->text.push_back(L'\0');
    g_loaded.push_back(std::move(t));
    return g_loaded.back().get();
}
// The English(HD) the tests load: SmallText's entries the helper reads or writes (12, the run
// 66 to 72 a lone carriage return each as the parser leaves an empty one, then 73, 137, 139,
// 354, 374) and ItemNames' 44 to 50. `tag` makes another load's texts differ.
inline std::map<int, std::wstring> hd_small_texts(const std::wstring& tag = L"") {
    std::map<int, std::wstring> out = {
        {12, L"Energy Keys detected!" + tag}, {65, L"Scanlines"}, {73, L"Off"},
        {98, L"Game Saved"}, {137, L"Assist"}, {139, L"Keys"}, {354, L"Randomizer" + tag},
        {374, L"Skip Escape and Final Battle?"}, {375, L"Yes"}};
    for (int k = 66; k <= 72; ++k) out[k] = L"\r";
    return out;
}
// LargeText's: the Feats of Natu screen's title (220) and its hint (219), as English(HD)'s.
inline std::map<int, std::wstring> hd_large_texts() {
    return {{219, L" Complete any one of these \r\nto see the expanded ending!"},
            {220, L"Feats of Natu!"}};
}
inline std::map<int, std::wstring> hd_item_texts(const std::wstring& tag = L"") {
    return {{0, L"Energy Tank"}, {36, L"Missile Box" + tag}, {44, L"Data Disc"},
            {46, L"Scrap Metal"}, {47, L"Scrap Energy" + tag}, {48, L"Scrap Chunk" + tag},
            {49, L"Area Map" + tag}, {50, L"Energy Shield"}};
}
// The translation named `name` loaded (its tables new) and on (`on`), as a pick in the menu
// does.
inline void load_translation(const char* name = "English(HD)", bool on = true,
                             const std::wstring& tag = L"") {
    g_small = make_table(448, hd_small_texts(tag));
    g_items = make_table(51, hd_item_texts(tag));
    g_large = make_table(362, hd_large_texts());
    std::memset(g_translation, 0, sizeof(g_translation));
    std::memcpy(g_translation, name, std::strlen(name));
    put<wchar_t*>(g_translation, 0x30, g_small->text.data());
    put<int32_t*>(g_translation, 0x58, g_small->offsets.data());
    put<wchar_t*>(g_translation, 0x48, g_items->text.data());
    put<int32_t*>(g_translation, 0x78, g_items->offsets.data());
    g_translation[0x90] = 1;
    g_translation[0x91] = on ? 1 : 0;
    put<wchar_t*>(g_translation, 0x38, g_large->text.data());
    put<int32_t*>(g_translation, 0x60, g_large->offsets.data());
    g_large_count = 362;
    g_small_count = 448;
    g_item_name_count = 51;
}
// Entry k's text as the table holds it now, up to its 0.
inline std::wstring entry_text(const LoadedTable* t, int k) {
    if (t == nullptr || k < 0 || static_cast<size_t>(k) >= t->offsets.size()) return L"";
    const int32_t at = t->offsets[k];
    return at < 0 ? std::wstring() : std::wstring(t->text.data() + at);
}

struct HdTextCall {
    std::wstring text;
    int entry, kind;
    void* message;
    std::wstring entry_then;  // the entry's text as SmallText held it at the call
};
inline std::vector<HdTextCall> g_hd_texts;
// As 0x1870A0: binds the message's HD object to it, its entry and kind, and marks it.
inline void hd_text_fn(const wchar_t* text, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t,
                       intptr_t, intptr_t entry, intptr_t kind, void* message) {
    auto* m = static_cast<BYTE*>(message);
    const int32_t slot = *reinterpret_cast<int32_t*>(m + 0x1E0);
    BYTE* o = g_hd_messages + 0x238 * slot;
    put<void*>(o, 0x220, m);
    put<int32_t>(o, 0x228, *reinterpret_cast<int32_t*>(m + 0x1D8));
    put<int32_t>(o, 0x20C, static_cast<int16_t>(entry));
    put<int32_t>(o, 0x210, static_cast<int16_t>(kind));
    o[0x22C] = text != nullptr && text[0] != 0 ? 1 : 0;
    g_hd_texts.push_back({text, static_cast<int>(entry), static_cast<int>(kind), message,
                          entry_text(g_small, static_cast<int>(entry))});
}
// The game's std::wstring, in the stand-in: a pointer to one of the test's own.
inline void* wstring_new_fn(void* text, const wchar_t* characters, size_t length) {
    auto* s = new std::wstring(characters, length);
    std::memcpy(text, &s, sizeof(s));
    return text;
}
struct BuildCall {
    float x, y;
    std::wstring text;
    void* font;
    BYTE* label;
    char flag;
};
inline std::vector<BuildCall> g_builds;
// The stand-in font: each character 6 wide (its glyph's corners 0 to 5 about its place), a space
// 3, a line 10 high.
constexpr double kHdAdvance = 6, kHdWidth = 5, kHdSpace = 3, kHdLine = 10;
// As 0x88480: frees the string, keeps the place in the label, marks it built; for a label with
// translated glyphs to build (+0x238, +0x240), lays them out from (x, y) as the stand-in font
// draws them, each glyph's place the pen's and the line's less 1 (0x88CCA to 0x88CFD), shown,
// the rest hidden, and their count at +0x244 and +0x248. With `flag` it wraps at the label's
// columns from its first pixel glyph, else 320 past it (0x885EF to 0x8862B).
inline void hd_build_fn(float x, float y, void* text, void* font, BYTE* label, char flag) {
    std::wstring* s = nullptr;
    std::memcpy(&s, text, sizeof(s));
    g_builds.push_back({x, y, *s, font, label, flag});
    put<double>(label, 0x258, x);
    put<double>(label, 0x260, y);
    label[0x230] = 1;
    auto** list = get<BYTE**>(label, 0x238);
    const int32_t capacity = get<int32_t>(label, 0x240);
    if (list != nullptr && capacity > 0) {
        BYTE* first = get<BYTE**>(label, 0x228) ? get<BYTE**>(label, 0x228)[0] : nullptr;
        const double from = first ? get<double>(first, 0x40) : 0;
        const double limit =
            flag ? from + get<int32_t>(label, 0x1DC) * 8 - 8 : from + 320;  // +0x60 taken as 0
        for (int32_t i = 0; i < capacity; ++i) list[i][0x120] = 0;
        double pen = x, line = y;
        int32_t n = 0;
        // Word by word, as the stand-in's wrap: a word that would end past the limit starts a line.
        size_t at = 0;
        while (at < s->size()) {
            const wchar_t c = (*s)[at];
            if (c == L'\n') {
                pen = x, line += kHdLine, ++at;
                continue;
            }
            if (c == L' ') {
                pen += kHdSpace, ++at;
                continue;
            }
            size_t end = s->find_first_of(L" \n", at);
            if (end == std::wstring::npos) end = s->size();
            const double width = (end - at) * kHdAdvance;
            if (pen > x && pen + width > limit) pen = x, line += kHdLine;
            for (; at < end && n < capacity; ++at, ++n, pen += kHdAdvance) {
                BYTE* g = list[n];
                put<double>(g, 0x40, pen);
                put<double>(g, 0x48, line - 1);
                put<double>(g, 0x98, 0);
                put<double>(g, 0xA8, kHdWidth);
                put<void*>(g, 0x30, *static_cast<void**>(font));
                g[0x120] = 1;
            }
            at = end;
        }
        put<int32_t>(label, 0x244, n);
        put<int32_t>(label, 0x248, n);
    }
    delete s;
}
// A label's translated glyphs for the stand-in builder to lay out: `n` of them, the label's
// capacity (+0x238, +0x240). `glyphs` stay the test's for its life.
template <int N>
struct HdGlyphs {
    BYTE glyphs[N][0x1D8] = {};
    BYTE* list[N] = {};
    explicit HdGlyphs(BYTE* label) {
        for (int i = 0; i < N; ++i) list[i] = glyphs[i];
        put<BYTE**>(label, 0x238, list);
        put<int32_t>(label, 0x240, N);
    }
    // Glyph i's place.
    double x(int i) const { return get<double>(glyphs[i], 0x40); }
    double y(int i) const { return get<double>(glyphs[i], 0x48); }
};
// The translation's keys in `a`, as configure sets them from stand_in_config, for a part's own
// addresses (a stand-in menu's).
inline void with_translation(zap::GameAddresses& a) {
    a.translation = g_translation;
    a.tr_flag = g_translation + 0x91;
    a.small_text = g_translation + 0x30;
    a.small_offsets = g_translation + 0x58;
    a.item_names = g_translation + 0x48;
    a.item_name_offsets = g_translation + 0x78;
    a.small_count = reinterpret_cast<BYTE*>(&g_small_count);
    a.item_name_count = reinterpret_cast<BYTE*>(&g_item_name_count);
    a.large_count = reinterpret_cast<BYTE*>(&g_large_count);
    a.hd_messages = g_hd_messages;
    a.message_slots = g_slots;
    a.hd_text_fn = reinterpret_cast<BYTE*>(&hd_text_fn);
    a.hd_build_fn = reinterpret_cast<BYTE*>(&hd_build_fn);
    a.wstring_new_fn = reinterpret_cast<BYTE*>(&wstring_new_fn);
}
// A label as the game builds it with the flag set (0x8AC30, 0x88480): its translated text
// built at (x, y), three translated glyphs shown, its pixel glyphs hidden. `glyphs` stay the
// test's for its life.
inline void built(BYTE* label, double x = 0, double y = 0) {
    label[0x230] = 1;
    put<double>(label, 0x258, x);
    put<double>(label, 0x260, y);
    // Room for the helper's texts (three per pixel glyph, 0x92511), unless HdGlyphs gave it some.
    if (get<int32_t>(label, 0x240) == 0) put<int32_t>(label, 0x240, 3 * get<int32_t>(label, 0x1D8));
}
struct TranslatedText {
    BYTE glyphs[3][0x1D8] = {};
    BYTE* list[3] = {glyphs[0], glyphs[1], glyphs[2]};
    explicit TranslatedText(BYTE* label) {
        built(label);
        put<BYTE**>(label, 0x238, list);
        put<int32_t>(label, 0x240, 3);
        put<int32_t>(label, 0x248, 3);
        put<int32_t>(label, 0x244, 3);
        put<int32_t>(label, 0x1EC, 0);
        for (BYTE* g : list) g[0x120] = 1;
        BYTE** pixel = get<BYTE**>(label, 0x228);
        for (int32_t i = 0; i < get<int32_t>(label, 0x1D8); ++i) pixel[i][0x120] = 0;
    }
};

// No translation: the flag clear, nothing loaded, no call made.
inline void reset_translation() {
    std::memset(g_translation, 0, sizeof(g_translation));
    std::memset(g_hd_messages, 0, sizeof(g_hd_messages));
    g_small = g_items = g_large = nullptr;
    g_small_count = g_item_name_count = g_large_count = 0;
    g_hd_texts.clear();
    g_builds.clear();
    g_slots_live = false;
    g_message_slot = 0;
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
inline double g_hud_value = 0;
inline int g_hud_numbers = 0;
inline void hud_number_fn(void* object, double value) {
    if (object == g_player + 0xB30) g_hud_value = value;  // the missile number
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

// Offline (offline.h): the map grid, the flags, the save slots' first blocks and their copies
// of the randomizer section.
inline BYTE g_map_grid[54 * 54] = {};
inline BYTE g_flags[0x9C4] = {};
inline BYTE g_slot_array[30 * 0x9408] = {};
inline BYTE g_rando_slots[30 * 0x908] = {};
// Save slot `n` (1 to 30) in the slot array, and its copy of the randomizer section.
inline BYTE* slot_block(int n) { return g_slot_array + (n - 1) * 0x9408; }
inline BYTE* rando_copy(int n) { return g_rando_slots + (n - 1) * 0x908; }
// Every slot empty.
inline void clear_slots() {
    std::memset(g_slot_array, 0, sizeof(g_slot_array));
    std::memset(g_rando_slots, 0, sizeof(g_rando_slots));
    for (int n = 1; n <= 30; ++n) slot_block(n)[0x935E] = 0xFF;
}

// Feats and achievements (feats.h). Two Steamworks accessor contexts ({init, counter,
// interface}), each init a `lea rdx, [rip+name]` then `ret` in code: the user stats' and
// another; the import slot SteamInternal_ContextInit is called through (a plain variable here:
// the feats tests use their own import's); and a small ending run as the game's runs (laid out
// by build_ending), whose call through the slot sets one achievement.
inline const char g_stats_name[] = "STEAMUSERSTATS_INTERFACE_VERSION013";
inline const char g_utils_name[] = "SteamUtils010";
__declspec(allocate(".zapcode")) inline unsigned char g_stats_init[8] = {0x48, 0x8D, 0x15, 0, 0,
                                                                         0,    0,    0xC3};
__declspec(allocate(".zapcode")) inline unsigned char g_utils_init[8] = {0x48, 0x8D, 0x15, 0, 0,
                                                                         0,    0,    0xC3};
inline void* g_stats_context[3] = {g_stats_init, nullptr, nullptr};
inline void* g_utils_context[3] = {g_utils_init, nullptr, nullptr};
inline void* g_context_init_slot = nullptr;
inline const char* g_victory = "14Victory";
// The ending (cf. 0x94A30 on B1), at these offsets: the call through the slot, the join, flag
// 250's store 0x11 past it.
constexpr ptrdiff_t kEndingCall = 0x0B, kEndingJoin = 0x28, kEndingSize = 0x45;
__declspec(allocate(".zapcode")) inline unsigned char g_ending[0x50] = {};

// The init functions' names, and the ending, calling through `slot`:
//   00 sub rsp, 0x28              04 lea rcx, [stats context]
//   0B call [slot]                11 mov rdx, [g_victory]
//   18 mov rcx, [rax]             1B mov rax, [rcx]         1E call [rax+0x30] (SetAchievement)
//   21 mov byte [flag 251], 1     28 movsxd rdx, [flags]    2F lea r8, [flags]
//   36 xorps xmm0, xmm0           39 mov byte [flag 250], 1
//   40 add rsp, 0x28              44 ret
inline void build_ending(void** slot) {
    auto init = [](unsigned char* code, const char* name) {
        unsigned char b[8] = {0x48, 0x8D, 0x15, 0, 0, 0, 0, 0xC3};
        auto r = static_cast<int32_t>(reinterpret_cast<const BYTE*>(name) - (code + 7));
        std::memcpy(b + 3, &r, sizeof(r));
        write_code(code, b, sizeof(b));
    };
    init(g_stats_init, g_stats_name);
    init(g_utils_init, g_utils_name);
    unsigned char e[kEndingSize] = {
        0x48, 0x83, 0xEC, 0x28,                    // 00
        0x48, 0x8D, 0x0D, 0, 0, 0, 0,              // 04
        0xFF, 0x15, 0, 0, 0, 0,                    // 0B
        0x48, 0x8B, 0x15, 0, 0, 0, 0,              // 11
        0x48, 0x8B, 0x08, 0x48, 0x8B, 0x01,        // 18, 1B
        0xFF, 0x50, 0x30,                          // 1E
        0xC6, 0x05, 0, 0, 0, 0, 0x01,              // 21
        0x48, 0x63, 0x15, 0, 0, 0, 0,              // 28
        0x4C, 0x8D, 0x05, 0, 0, 0, 0,              // 2F
        0x0F, 0x57, 0xC0,                          // 36
        0xC6, 0x05, 0, 0, 0, 0, 0x01,              // 39
        0x48, 0x83, 0xC4, 0x28, 0xC3,              // 40, 44
    };
    // Each field relative to where it will be, in g_ending.
    auto rel = [&](size_t field, size_t next, const void* target) {
        auto r = static_cast<int32_t>(static_cast<const BYTE*>(target) - (g_ending + next));
        std::memcpy(e + field, &r, sizeof(r));
    };
    rel(0x07, 0x0B, g_stats_context);
    rel(0x0D, 0x11, slot);
    rel(0x14, 0x18, &g_victory);
    rel(0x23, 0x28, g_flags + 251);
    rel(0x2B, 0x2F, g_flags);
    rel(0x32, 0x36, g_flags);
    rel(0x3B, 0x40, g_flags + 250);
    write_code(g_ending, e, sizeof(e));
}

// Runs the stand-in ending, as the game's transition runs the real one.
inline void run_ending() {
    reinterpret_cast<void (*)()>(static_cast<void*>(g_ending))();
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
        {"assist_menu_fn", rva(reinterpret_cast<void*>(&assist_menu_fn))},
        {"placement_fn", rva(reinterpret_cast<void*>(&placement_fn))},
        {"rando_newgame_fn", rva(reinterpret_cast<void*>(&rando_newgame_fn))},
        {"event_table", rva(&g_event_table)},
        {"event_count", rva(&g_event_count)},
        {"text_table", rva(&g_text_table)},
        {"equipment_fn", rva(reinterpret_cast<void*>(&equipment_fn))},
        {"equip_panel_fn", rva(reinterpret_cast<void*>(&equip_panel_fn))},
        {"widget_move_fn", rva(reinterpret_cast<void*>(&widget_move_fn))},
        {"text_place_fn", rva(reinterpret_cast<void*>(&text_place_fn))},
        {"widget_new_fn", rva(reinterpret_cast<void*>(&widget_new_fn))},
        {"sprite_sheet_fn", rva(reinterpret_cast<void*>(&sprite_sheet_fn))},
        {"panel_link_fn", rva(reinterpret_cast<void*>(&panel_link_fn))},
        {"text_set_fn", rva(reinterpret_cast<void*>(&text_set_fn))},
        {"malloc_slot", rva(&g_malloc_slot)},
        {"free_slot", rva(&g_free_slot)},
        {"routine_table", rva(g_routine_table)},
        {"fade", rva(g_fade)},
        {"fade_level", rva(&g_fade_level)},
        {"keys_branch", rva(g_keys_branch)},
        {"file_panel_fn", rva(reinterpret_cast<void*>(&file_panel_fn))},
        {"file_entry_fn", rva(reinterpret_cast<void*>(&file_entry_fn))},
        {"copy_entry_fn", rva(reinterpret_cast<void*>(&copy_entry_fn))},
        {"delete_entry_fn", rva(reinterpret_cast<void*>(&delete_entry_fn))},
        {"map_grid", rva(g_map_grid)},
        {"flags", rva(g_flags)},
        {"slot_array", rva(g_slot_array)},
        {"rando_slots", rva(g_rando_slots)},
        {"text_new_fn", rva(reinterpret_cast<void*>(&text_new_fn))},
        {"text_link_fn", rva(reinterpret_cast<void*>(&text_link_fn))},
        {"draw_layer", rva(&g_draw_layer)},
        {"map_handler", rva(&g_map_handler)},
        {"seen_marker_code", rva(g_seen_marker_code)},
        {"start_count", rva(&g_start_count)},
        {"start_cells", rva(g_start_cells)},
        {"start_flags", rva(g_start_flags)},
        {"sprite_texture", rva(&g_sprite_holder)},
        {"popup_width_code", rva(g_popup_width_code)},
        {"item_pool", rva(g_item_pool)},
        {"object_pool", rva(g_object_pool)},
        {"palette_table", rva(g_palette_table)},
        {"frame_clock", rva(&g_frame_clock)},
        {"sprite_templates", rva(&g_sprite_templates)},
        {"event_handlers", rva(g_event_handlers)},
        {"death_fn", rva(reinterpret_cast<void*>(&death_fn))},
        {"halt_flag", rva(&g_halt_flag)},
        {"ending_unlocks", rva(g_ending + kEndingCall)},
        {"ending_join", rva(g_ending + kEndingJoin)},
        {"stats_context", rva(g_stats_context)},
        {"context_init_slot", rva(&g_context_init_slot)},
        {"translation", rva(g_translation)},
        {"tr_flag", rva(g_translation + 0x91)},
        {"small_text", rva(g_translation + 0x30)},
        {"small_offsets", rva(g_translation + 0x58)},
        {"item_names", rva(g_translation + 0x48)},
        {"item_name_offsets", rva(g_translation + 0x78)},
        {"small_count", rva(&g_small_count)},
        {"item_name_count", rva(&g_item_name_count)},
        {"large_count", rva(&g_large_count)},
        {"hd_messages", rva(g_hd_messages)},
        {"message_slots", rva(g_slots)},
        {"hd_text_fn", rva(reinterpret_cast<void*>(&hd_text_fn))},
        {"hd_build_fn", rva(reinterpret_cast<void*>(&hd_build_fn))},
        {"wstring_new_fn", rva(reinterpret_cast<void*>(&wstring_new_fn))},
    };
}

// The stand-in game's configuration given to `game` (`active`: a client's, not the offline
// cache's): "" or why not.
inline std::string configure(zap::Game& game, bool active = true) {
    return game.configure(GetModuleHandleW(nullptr), stand_in_config(), active);
}

inline void reset_game() {
    std::memset(g_item_pool, 0, sizeof(g_item_pool));
    std::memset(g_object_pool, 0, sizeof(g_object_pool));
    std::memset(g_event_handlers, 0, sizeof(g_event_handlers));
    g_event_handlers[1] = reinterpret_cast<void*>(&fade_handler);
    g_event_handlers[11] = reinterpret_cast<void*>(&spawn_handler);
    reset_fade();
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
    put<BYTE*>(g_item_template, 0x1A8, g_frames);
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
