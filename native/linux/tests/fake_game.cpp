// The stand-in game for tests/test_native_linux.py: it prints where its marker and block are,
// then calls SDL_GL_SwapWindow every 5 ms until killed, from two call sites (every frame from
// draw_frame, every fourth from draw_editor, as the game swaps a second window), and on SIGTERM the stand-in SDL's count of the calls that
// reached it.
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>

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
unsigned char zap_tr_flag;
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
unsigned char zap_hd_messages[4 * 0x238];
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
// events (none). The pickup's popup width is zap_pickup's `mov esi, 24`
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
void zap_pickup();
// Traps: AILMENTS (0: NORMAL), the halt byte, and zap_weapon_cool, the game's two AILMENTS tests
// as the Linux build has them (trap.cpp's kHitGame then kWaveGame, each load reading
// zap_ailments), patched in place while a trap's timer runs.
short zap_ailments;
unsigned char zap_halt;
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
    void* record = zap_texture_record;
    std::memcpy(zap_world + 0x40, &record, sizeof record);
    zap_sprite_world = zap_world;
    zap_player[0x107A] = 2;
    zap_player[0x107B] = 3;
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
