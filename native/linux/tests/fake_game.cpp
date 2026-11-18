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
}

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
