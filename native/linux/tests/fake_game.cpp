// The stand-in game for tests/test_native_linux.py: it prints where its marker and block are,
// then calls SDL_GL_SwapWindow every 5 ms until killed, from two call sites (every frame from
// draw_frame, every fourth from draw_editor, as the game swaps a second window), and on SIGTERM the stand-in SDL's count of the calls that
// reached it.
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>

extern "C" void SDL_GL_SwapWindow(void*);
extern "C" unsigned long zap_stub_swaps();

volatile char g_marker[] = "zexion-ap test marker";
volatile unsigned char g_block[40000];  // byte i is i % 251, for reads of more than one piece
volatile std::sig_atomic_t g_stop = 0;

// The new-game state, as the game keeps it in its .bss (native/src/newgame.h), for
// tests/test_native_linux.py's new games: found by these names in this program's symbols.
extern "C" {
unsigned char zap_section[0x908];
unsigned char zap_menu[0x114];
void* zap_transition;
int zap_timer;
unsigned short zap_marker;
int zap_slot;
short zap_start_count;
unsigned char zap_start_cells[4 * 64];
unsigned char zap_start_flags[64];
__attribute__((noinline, used)) void zap_new_game() { asm volatile(""); }
// The stand-in's placement: the item table's first location, as the game's placement writes it.
__attribute__((noinline, used)) void zap_placement() { zap_section[0x26C] = 0x5A; }
// The menus the ARCHIPELAGO choice reads (no menu is ever up in the stand-in: it is not offered).
void* zap_menu_top;
// The translation object (gCurrentTranslation's size; its flag at +0x91, off), its counts, and
// its two builders: the message's records its ints, entry and kind and its text (4-byte wchar_t,
// as the Linux build's), the label's the string it was handed (a std::wstring by reference).
alignas(16) unsigned char zap_translation[0x40150];
short zap_small_count;
short zap_sprite_def_count = 96;  // as the game's (gSpriteDefCount): the icon sheets 101, 104
short zap_item_name_count;
int zap_hd_ints[8];
wchar_t zap_hd_shown[128];
wchar_t zap_built[128];
unsigned char zap_hd_messages[4 * 0x238];
// As createTranslationGameMessage binds the message's HD object (docs/re/linux.md,
// `hd_messages`): its message, serial, entry, kind and has-text byte.
__attribute__((noinline, used)) void zap_hd_text(const wchar_t* text, int a, int b, int c, int d,
                                                 int e, int f, short entry, short kind,
                                                 void* message) {
    const int ints[8] = {a, b, c, d, e, f, entry, kind};
    int slot = 0, serial = 0;
    std::memcpy(&slot, static_cast<unsigned char*>(message) + 0x1E0, 4);
    std::memcpy(&serial, static_cast<unsigned char*>(message) + 0x1D8, 4);
    unsigned char* object = zap_hd_messages + 0x238 * slot;
    const int entry32 = entry, kind32 = kind;
    std::memcpy(object + 0x220, &message, sizeof message);
    std::memcpy(object + 0x228, &serial, 4);
    std::memcpy(object + 0x20C, &entry32, 4);
    std::memcpy(object + 0x210, &kind32, 4);
    object[0x22C] = text[0] != L'\0';
    std::wcsncpy(zap_hd_shown, text, 127);
    std::memcpy(zap_hd_ints, ints, sizeof ints);  // last: the test waits for these
}
__attribute__((noinline, used)) void zap_build_text(float, float, std::wstring* text, void*,
                                                    void*, bool) {
    std::wcsncpy(zap_built, text->c_str(), 127);
}
// The translation's tables as the Linux build loads them (4-byte characters): SmallText's 80
// entries, 66 to 72 empty in a run with room after them (as in every translation), and
// ItemNames' 51, item 3 Machine Gun.
wchar_t zap_small[600];
int zap_small_offsets[80];
wchar_t zap_items[200];
int zap_item_offsets[51];
__attribute__((noinline, used)) void zap_rando_menu() { asm volatile(""); }
__attribute__((noinline, used)) void zap_widget_handler(void*, int) { asm volatile(""); }
__attribute__((noinline, used)) void zap_panel_handler(void*, int) { asm volatile(""); }
__attribute__((noinline, used)) void zap_force_seed() { asm volatile(""); }

// Grants and their popups (Game::apply, pump_popups), in the game's layouts: the player state
// with its inventory at +0x108E, the item records, the last message (its pointer, its sequence at
// +8) and the message slots. The stand-in functions keep what they were given, for the test to
// read: the last popup's text, its sound, the HUD numbers.
unsigned char zap_player[0x1110];
unsigned char zap_save_loaded;  // 0: a save loaded (0xFF: none)
unsigned char zap_cursor;       // 0: no menu up
void* zap_records;
unsigned char zap_record_table[51 * 100];
unsigned char zap_last_message[16];
unsigned char zap_strip_gate;
unsigned char zap_sounds[8];
unsigned char zap_message_slots[4 * 0x1628];
char zap_shown[256];  // the last message's text
int zap_shown_count;
int zap_sound_id = -1;
double zap_hud_values[2];  // the last two numbers (missiles, then bombs)
int zap_hud_calls;
__attribute__((noinline, used)) void* zap_message(const char* text, int, int, int, int, int,
                                                  int) {
    // Slot 0, with a new serial and no frames left (so the next popup may follow).
    unsigned char* slot = zap_message_slots;
    int serial = ++zap_shown_count;
    std::memcpy(slot + 0x1D8, &serial, 4);
    std::memset(slot + 0x1E0, 0, 4);
    std::memset(slot + 0x1EC, 0, 4);
    void* last = slot;
    std::memcpy(zap_last_message, &last, sizeof last);
    std::memcpy(zap_last_message + 8, &serial, 4);
    std::strncpy(zap_shown, text, sizeof zap_shown - 1);
    return slot;
}
__attribute__((noinline, used)) void zap_sound(void*, short id, float, float, float, float, short,
                                               short) {
    zap_sound_id = id;
}
__attribute__((noinline, used)) void zap_hud_number(void*, double value, bool) {
    zap_hud_values[zap_hud_calls++ % 2] = value;
}
__attribute__((noinline, used)) void zap_hud_layout(void*) { asm volatile(""); }
__attribute__((noinline, used)) void zap_energy_hud(void*, bool) { asm volatile(""); }
__attribute__((noinline, used)) void zap_weapon_strip(void*) { asm volatile(""); }
__attribute__((noinline, used)) void zap_suit_color(unsigned char, short) { asm volatile(""); }

// Other players' items (Game::watch_ap_items): the sprite world (its texture, 7: the stand-in
// OpenGL's below), the pools (empty), the palettes, the templates, the flags, the map, the area
// events (the scraps' alone, below). The pickup's popup width is zap_pickup's `mov esi, 24`
// (BE 18 00 00 00), as the game's (docs/re/linux.md, `popup_width_code`), patched in place.
void* zap_sprite_world;
unsigned char zap_item_pool[128 * 0x3B0];
void* zap_palettes[65];
long long zap_frame_clock;
void* zap_templates;
unsigned char zap_object_pool[1000 * 0x5C8];
unsigned char zap_flags[0x9C4];
unsigned char zap_map_grid[54 * 54];
void* zap_events;
short zap_event_count;
void* zap_event_procs[48];
// Received scrap's drops (Game::pump_drops) and Energy Shields (spawn_shield): the table holds
// events 3548 to 3550 (Scrap Metal's, Scrap Energy's, Scrap Chunk's spawns), 3552 (Scrap
// Chunk's fade) and 3553 (the Energy Shield's spawn) as AreaEvents has them
// (game_calls.cpp, kSpawnRecord, kChunkFadeRecord); RETURN HOME's 3667 is past its count. The
// spawn handler (type 11) and the fade's (type 1) note each call: the cell and the event.
unsigned char zap_event_records[3554 * 0x28];
int zap_spawn_count;
int zap_spawn_cell;  // row * 256 + column
void* zap_spawn_event;
__attribute__((noinline, used)) void zap_spawn(char row, char column, unsigned char* event) {
    ++zap_spawn_count;
    zap_spawn_cell = static_cast<unsigned char>(row) * 256 + static_cast<unsigned char>(column);
    zap_spawn_event = event;
}
__attribute__((noinline, used)) void zap_fade_event(char, char, unsigned char*) {
    asm volatile("");
}
void zap_pickup();
// The OS Install Disks' count: the Text table (32-byte entries, the scanner's 12th as the game
// has it), the menu functions (none called here: no menu opens in the stand-in), and the pause
// screen's KEYS branch (zap_pause_game: `jne +0x3C` first, as PauseGame's at 0x618A7E).
char zap_texts[16][32];
void* zap_text_table;
__attribute__((noinline, used)) void zap_equipment_menu() { asm volatile(""); }
__attribute__((noinline, used)) void zap_equipment_frame(void*, int) { asm volatile(""); }
__attribute__((noinline, used)) void zap_widget_move(void*, int, int, bool) { asm volatile(""); }
__attribute__((noinline, used)) void zap_text_place(void*, int, int) { asm volatile(""); }
__attribute__((noinline, used)) void* zap_widget_new(const char*, int, void*, void*, void*, void*,
                                                     void*, int, void*) {
    return nullptr;
}
__attribute__((noinline, used)) void zap_sprite_sheet(void*, void*) { asm volatile(""); }
__attribute__((noinline, used)) void zap_panel_link(void*) { asm volatile(""); }
__attribute__((noinline, used)) void zap_options_back(void*) { asm volatile(""); }
void zap_pause_game();
// RETURN HOME WITH THE DISKS?: the fade's block and level, and the routines (none set: the
// stand-in never opens the prompt, which needs event 3667 as the game loads it).
unsigned char zap_fade[0xAC];
unsigned char zap_fade_level;
void* zap_routines[16];
// Traps: AILMENTS (0: NORMAL), the halt byte, and zap_weapon_cool, the game's two AILMENTS tests
// as the Linux build has them (trap.cpp's kHitGame then kWaveGame, each load reading
// zap_ailments), patched in place while a trap's timer runs.
short zap_ailments;
unsigned char zap_halt;
// DeathLink: the player's death (PlayerDestruction's arguments), noted: the player and kind.
int zap_death_count;
void* zap_death_player;
int zap_death_kind = -1;
// The ASSIST lock: ASSIST's callback (none called here: no options menu opens in the stand-in).
__attribute__((noinline, used)) void zap_assist_menu() { asm volatile(""); }
__attribute__((noinline, used)) void zap_death(void* player, char kind) {
    ++zap_death_count;
    zap_death_player = player;
    zap_death_kind = kind;
}
void zap_weapon_cool();
// The map: the item update's two tests of the ID (zap_power_up: PowerUpMoveProc's at 0 and
// 0x112, the code between as filler), the move procedures (entry 120 the map's: none open here)
// and the function setting a text's glyphs.
void zap_power_up();
void* zap_move_procs[128];
__attribute__((noinline, used)) void zap_text_set(void*, const char*, int, int) {
    asm volatile("");
}

// A stand-in OpenGL, found as the game's libGL is (dlsym, the executable's own symbols exported):
// one 2048 x 2048 texture, 7, blank but for the two pixels of known art the helper's check reads
// (native/src/apitem.cpp, TextureCheck::read); each upload counted.
unsigned zap_texture_name = 7;
unsigned* zap_texture_record = &zap_texture_name;
unsigned char zap_world[0x48];
int zap_gl_uploads;
unsigned zap_bound;
unsigned char zap_no_texture;  // set: texture 7 is gone (glIsTexture false), nothing painted
#pragma GCC visibility push(default)  // exported: the helper's dlsym finds them
void glBindTexture(unsigned, unsigned texture) { zap_bound = texture; }
void glGetIntegerv(unsigned name, int* value) {
    *value = name == 0x8069 ? static_cast<int>(zap_bound) : 0;
}
void glGetTexLevelParameteriv(unsigned, int, unsigned, int* value) { *value = 2048; }
void glPixelStorei(unsigned, int) {}
void glTexSubImage2D(unsigned, int, int, int, int, int, unsigned, unsigned, const void*) {
    ++zap_gl_uploads;
}
void glGetTexImage(unsigned, int, unsigned, unsigned, void* pixels) {
    auto* p = static_cast<unsigned char*>(pixels);
    std::memset(p, 0, 2048u * 2048u * 4u);
    auto opaque = [p](int x, int y) { p[(static_cast<size_t>(y) * 2048 + x) * 4 + 3] = 0xFF; };
    opaque(128 + (134 % 8) * 16 + 6, 640 + (134 / 8) * 16);  // Scrap Energy's frame 134
    opaque((0xC0 % 16) * 8 + 2, 976 + (0xC0 / 16) * 8 + 1);  // the font's glyph 0xC0
}
unsigned char glIsTexture(unsigned texture) { return texture == 7 && !zap_no_texture; }
unsigned glGetError() { return 0; }
#pragma GCC visibility pop
}

asm(".text\n.globl zap_pickup\n.type zap_pickup, @function\nzap_pickup:\n"
    "  movl $24, %esi\n  ret\n.size zap_pickup, .-zap_pickup\n");

asm(".text\n.globl zap_pause_game\n.type zap_pause_game, @function\nzap_pause_game:\n"
    ".byte 0x75, 0x3C\n  ret\n.size zap_pause_game, .-zap_pause_game\n");

// PowerUpMoveProc's two tests of the item's ID, byte for byte, 0x112 apart, never run.
#define ZAP_ID_TEST(minor, past)                                                          \
    ".byte 0x66, 0x83, 0xF8, 0x02, 0x7E, " minor ", 0x48, 0x8B, 0x45, 0xE8, 0x0F, 0xB7\n" \
    ".byte 0x80, 0xE8, 0x01, 0x00, 0x00, 0x66, 0x83, 0xF8, 0x23, 0x7F, " past "\n"
asm(".text\n.globl zap_power_up\n.type zap_power_up, @function\nzap_power_up:\n"
    ZAP_ID_TEST("0x60", "0x4F") ".fill 0xFB, 1, 0x90\n" ZAP_ID_TEST("0x5D", "0x4C")
    "  ret\n.size zap_power_up, .-zap_power_up\n");

// weaponCool's two tests, byte for byte (but where the loads point), never run.
#define ZAP_LOAD ".byte 0x0F, 0xB7, 0x05\n.long zap_ailments - (. + 4)\n"
asm(".text\n.globl zap_weapon_cool\n.type zap_weapon_cool, @function\nzap_weapon_cool:\n"
    ZAP_LOAD
    ".byte 0x66, 0x83, 0xF8, 0x01, 0x75, 0x1B, 0x48, 0x8B, 0x45, 0xE8, 0x0F, 0xB7, 0x80, 0xC2\n"
    ".byte 0x0D, 0x00, 0x00, 0x83, 0xE8, 0x02, 0x89, 0xC2, 0x48, 0x8B, 0x45, 0xE8, 0x66, 0x89\n"
    ".byte 0x90, 0xC2, 0x0D, 0x00, 0x00\n"
    ZAP_LOAD
    ".byte 0x66, 0x83, 0xF8, 0x02, 0x75, 0x0D\n"
    ZAP_LOAD
    ".byte 0x66, 0x83, 0xF8, 0x01, 0x75, 0x1E, 0x48, 0x8B, 0x45, 0xE8, 0x0F, 0xB7, 0x80, 0xC4\n"
    ".byte 0x0D, 0x00, 0x00, 0x66, 0x83, 0xF8, 0x3C, 0x7E, 0x0D, 0x48, 0x8B, 0x45, 0xE8, 0x66\n"
    ".byte 0xC7, 0x80, 0xC4, 0x0D, 0x00, 0x00, 0x3C, 0x00\n"
    ZAP_LOAD
    ".byte 0x66, 0x83, 0xF8, 0x02, 0x75, 0x0D\n"
    "  ret\n.size zap_weapon_cool, .-zap_weapon_cool\n");

extern "C" __attribute__((noinline)) void draw_frame() {
    SDL_GL_SwapWindow(nullptr);
    asm volatile("");  // not a tail call: the call returns here
}

extern "C" __attribute__((noinline)) void draw_editor() {
    SDL_GL_SwapWindow(nullptr);
    asm volatile("");
}

int main() {
    std::signal(SIGTERM, [](int) { g_stop = 1; });
    for (unsigned i = 0; i < sizeof g_block; ++i) g_block[i] = static_cast<unsigned char>(i % 251);
    // In play: 16 energy; item 3's record named MACHINE GUN, with sound 7.
    short energy = 16;
    std::memcpy(zap_player + 0xCEA, &energy, 2);
    zap_records = zap_record_table;
    unsigned char* gun = zap_record_table + 3 * 100;
    std::strcpy(reinterpret_cast<char*>(gun + 0x0A), "MACHINE GUN");
    short sound = 7;
    float volume = 1.0f;
    std::memcpy(gun + 0x5A, &sound, 2);
    std::memcpy(gun + 0x5C, &volume, 4);
    // Records 47 and 48, the stand-ins, as the game loads them (native/src/apitem.cpp, kOwnShape),
    // and the player at map row 2, column 3 (a label's (x, y): (3, 2)).
    const char* names[2] = {"SCRAP ENERGY", "SCRAP CHUNK"};
    const short frames[2] = {134, 142}, events[2] = {3549, 3550};
    for (int i = 0; i < 2; ++i) {
        unsigned char* r = zap_record_table + (47 + i) * 100;
        const short fields[][2] = {{0x00, 34},  {0x02, frames[i]}, {0x04, 6}, {0x06, 2},
                                   {0x3C, 46},  {0x40, events[i]}, {0x54, 0}, {0x58, 1}};
        for (const auto& f : fields) std::memcpy(r + f[0], &f[1], 2);
        r[0x08] = 0;
        r[0x09] = 8;
        std::strcpy(reinterpret_cast<char*>(r + 0x0A), names[i]);
    }
    // Record 49, the Area Map, as the game loads it (native/src/intel.cpp, kAreaMap): Intel Chips.
    {
        unsigned char* r = zap_record_table + 49 * 100;
        const short fields[][2] = {{0x00, 34}, {0x02, 25}, {0x04, 6},  {0x06, 2},
                                   {0x3C, 46}, {0x40, 3551}, {0x54, 0}, {0x58, 1}};
        for (const auto& f : fields) std::memcpy(r + f[0], &f[1], 2);
        r[0x08] = 0;
        r[0x09] = 3;
        std::strcpy(reinterpret_cast<char*>(r + 0x0A), "AREA MAP");
    }
    void* record = zap_texture_record;
    std::memcpy(zap_world + 0x40, &record, sizeof record);
    zap_sprite_world = zap_world;
    for (int k = 0; k < 80; ++k) zap_small_offsets[k] = k < 66 ? 2 * k : 200 + 40 * (k - 66);
    for (int k = 0; k < 51; ++k) zap_item_offsets[k] = 2 * k;
    zap_item_offsets[3] = 120;
    std::wcscpy(zap_items + 120, L"Machine Gun");
    zap_small_count = 80;
    zap_item_name_count = 51;
    const void* tables[4][2] = {{zap_small, nullptr}, {zap_small_offsets, nullptr},
                                {zap_items, nullptr}, {zap_item_offsets, nullptr}};
    const int at[4] = {0x30, 0x58, 0x48, 0x78};
    for (int i = 0; i < 4; ++i) std::memcpy(zap_translation + at[i], &tables[i][0], sizeof(void*));
    std::strcpy(zap_texts[12], "ENERGY KEYS DETECTED!");
    zap_text_table = zap_texts;
    zap_player[0x107A] = 2;
    zap_player[0x107B] = 3;
    // The scraps' events: type 11, subtype 1, the object, F3 03 FF FF 01, then (5, 8) at +0x12,
    // sound -1 at +0x1A, 1.0f at +0x1C, the next event at +0x20 and the event's own at +0x24.
    const unsigned char spawn[0x28] = {
        0x0B, 0x00, 0x01, 0x00, 0x00, 0x00, 0xF3, 0x03, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x02, 0x00, 0x05, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
        0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const short scraps[4][3] = {
        {3548, 2010, -1}, {3549, 2016, -1}, {3550, 2017, 3552}, {3553, 2018, -1}};
    for (const auto& s : scraps) {
        unsigned char* ev = zap_event_records + s[0] * 0x28;
        std::memcpy(ev, spawn, sizeof spawn);
        std::memcpy(ev + 4, &s[1], 2);
        std::memcpy(ev + 0x20, &s[2], 2);
        std::memcpy(ev + 0x24, &s[0], 2);
    }
    const unsigned char fade[0x28] = {
        0x01, 0x00, 0x07, 0x00, 0xF6, 0xFF, 0x08, 0x00, 0x03, 0x00, 0x07, 0x00, 0x00, 0x00,
        0xE9, 0xFF, 0xFD, 0xFF, 0xFC, 0xFF, 0xFC, 0xFF, 0x04, 0x00, 0x04, 0x00, 0xFF, 0xFF,
        0x00, 0x00, 0x80, 0x3F, 0xFF, 0xFF, 0x00, 0x00, 0xE0, 0x0D, 0x00, 0x00};
    std::memcpy(zap_event_records + 3552 * 0x28, fade, sizeof fade);
    // Scrap Metal's and the Energy Shield's records name their events, as the game's (their
    // other fields unread here).
    const short scrap_metal_event = 3548, shield_event = 3553;
    std::memcpy(zap_record_table + 46 * 100 + 0x40, &scrap_metal_event, 2);
    std::memcpy(zap_record_table + 50 * 100 + 0x40, &shield_event, 2);
    zap_events = zap_event_records;
    zap_event_count = 3554;
    zap_event_procs[1] = reinterpret_cast<void*>(&zap_fade_event);
    zap_event_procs[11] = reinterpret_cast<void*>(&zap_spawn);
    std::printf("marker %p block %p\n", static_cast<void*>(const_cast<char*>(g_marker)),
                static_cast<void*>(const_cast<unsigned char*>(g_block)));
    std::fflush(stdout);
    timespec pause{0, 5 * 1000 * 1000};
    for (unsigned long frame = 0; !g_stop; ++frame) {
        draw_frame();
        if (frame % 4 == 0) draw_editor();
        nanosleep(&pause, nullptr);
    }
    std::printf("swaps %lu\n", zap_stub_swaps());
    return 0;
}
