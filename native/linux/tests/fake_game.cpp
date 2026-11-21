// The stand-in game for tests/test_native_linux.py: it prints where its marker and block are,
// then calls SDL_GL_SwapWindow every 5 ms until killed, from two call sites (every frame from
// draw_frame, every fourth from draw_editor, as the game swaps a second window; with
// ZAP_FAKE_NO_FRAME set, only from draw_editor, every time), and on SIGTERM the stand-in SDL's
// count of the calls that reached it.
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>

extern "C" void SDL_GL_SwapWindow(void*);
extern "C" unsigned long zap_stub_swaps();

volatile char g_marker[] = "zexion-ap test marker";
volatile unsigned char g_block[40000];  // byte i is i % 251, for reads of more than one piece
volatile std::sig_atomic_t g_stop = 0;

// The game's own names, as the helper looks each up in this program's symbols (native/linux/src/
// helper.cpp, kConfigKeys and the rest): each stand-in object and function below carries one (an
// asm label, the mangled C++ names the game's own), at the offset the helper expects, so the one
// table there serves the release build and the test build. docs/re/linux.md has what each is.
extern "C" {
extern unsigned char zap_section[0x908] asm("gCurrentRando");
extern unsigned char zap_menu[0x114] asm("gRandoPreferences");
extern void* zap_transition asm("gExitActionP");
extern int zap_timer asm("gExitDelay");
extern int zap_slot asm("gCurrentSaveGame");
extern short zap_start_count asm("rStartingLocationCount");
extern unsigned char zap_start_cells[4 * 64] asm("rStartingLocations");
extern unsigned char zap_start_flags[64] asm("rStartingLocationCage");
extern void* zap_menu_top asm("gLastGameMenuP");
extern unsigned char zap_translation[0x40150] asm("gCurrentTranslation");
extern short zap_small_count asm("gSmallTextCount");
extern short zap_item_name_count asm("gPowerUpDefCount");
extern unsigned char zap_hd_messages[4 * 0x238] asm("gTranslationGameMessages");
extern unsigned char zap_cursor asm("gCursorMode");
extern void* zap_records asm("gPowerUpDefs");
extern unsigned char zap_sounds[8] asm("gGameSoundManager");
extern void* zap_sprite_world asm("gSpriteWorldP");
extern void* zap_palettes[65] asm("gSpriteDrawProcs");
extern void* zap_templates asm("gMasterSpritesP");
extern void* zap_events asm("gAreaEvents");
extern short zap_event_count asm("gAreaEventCount");
extern void* zap_event_procs[48] asm("gAreaEventProcs");
extern unsigned char zap_halt asm("gEditorMode");
extern void* zap_text_table asm("gSmallTexts");
extern void* zap_routines[16] asm("gExitActions");
extern void* zap_draw_layer asm("gStatsSpriteLayerP");
extern unsigned char zap_save_games[30 * 0x9408] asm("gSaveGames");
extern unsigned char zap_saved_randos[30 * 0x908] asm("gSavedRandos");
extern short zap_large_count asm("gLargeTextCount");
extern void* zap_move_procs[128] asm("gMoveProcs");
extern void* zap_main_character asm("gMainCharacterSpriteP");
extern short zap_sprite_def_count asm("gSpriteDefCount");
extern void* zap_stats_context[3] asm("_ZZ14SteamUserStatsvE27s_CallbackCounterAndContext");

void zap_new_game() asm("_Z16StartNewGameExitv");
void zap_placement() asm("_Z13generateRandov");
void zap_rando_menu() asm("_Z14RandomizerMenuv");
void zap_widget_handler(void*, int) asm("_Z18StandardMenuActionP12GameMenuItemi");
void zap_panel_handler(void*, int) asm("_Z16FrameWrapOrArrowP12GameMenuItemi");
void zap_force_seed() asm("_Z9forceSeedv");
void zap_hd_text(const wchar_t*, int, int, int, int, int, int, short, short,
                 void*) asm("_Z28createTranslationGameMessagePKwiiiiiissP9SpriteRec");
void zap_build_text(float, float, std::wstring*, void*, void*,
                    bool) asm("_Z12generateTextffNSt7__cxx1112basic_stringIwSt11char_traitsIwESaIwEEE"
                              "P8GameFontP12SpriteStringb");
void* zap_message(const char*, int, int, int, int, int,
                  int) asm("_Z17createGameMessagePKciiiiii");
void zap_sound(void*, short, float, float, float, float, short,
               short) asm("_Z13PlayGameSoundP16GameSoundManagersffffss");
void zap_hud_number(void*, double, bool) asm("_Z19updateNumberDisplayP13NumberDisplaydb");
void zap_hud_layout(void*) asm("_Z29updateNumberDisplayVisibilityP12PlayerStruct");
void zap_energy_hud(void*, bool) asm("_Z15updateEnergyBarP12PlayerStructb");
void zap_weapon_strip(void*) asm("_Z19updateWeaponDisplayP12PlayerStruct");
void zap_suit_color(unsigned char, short) asm("_Z16setSpritePalettehs");
void zap_text_set(void*, const char*, int, int) asm("_Z17alterSpriteStringP9SpriteRecPKcii");
void zap_equipment_menu() asm("_Z13EquipmentMenuv");
void zap_equipment_frame(void*, int) asm("_Z14EquipmentFrameP12GameMenuItemi");
void zap_widget_move(void*, int, int, bool) asm("_Z16moveGameMenuItemP9SpriteReciib");
void zap_text_place(void*, int, int) asm("_Z16moveSpriteStringP9SpriteRecii");
void* zap_widget_new(const char*, int, void*, void*, void*, void*, void*, int,
                     void*) asm("_Z18createGameMenuItemPKciP8GameMenuPFvP12GameMenuItemiEPFvvEPvS9_"
                                "iP5sRect");
void zap_sprite_sheet(void*, void*) asm("_Z20SWChangeMasterSpriteP9SpriteRecS0_");
void zap_panel_link(void*) asm("_Z11addGameMenuP8GameMenu");
void zap_options_back(void*) asm("_Z22refreshPauseFromOptionP14GameMenuScreen");
void zap_death(void*, char) asm("_Z17PlayerDestructionP9SpriteRecc");
void zap_assist_menu() asm("_Z10AssistMenuv");
void* zap_text_new(const char*, int, int) asm("_Z15newSpriteStringPKcii");
void zap_text_link(void*) asm("_Z15addSpriteStringP9SpriteRec");
void zap_file_panel(void*, int) asm("_Z20SaveGamePlayerCursorP12GameMenuItemi");
void zap_file_entry(void*, int) asm("_Z18SaveGameMenuActionP12GameMenuItemi");
void zap_copy_entry(void*, int) asm("_Z18CopyGameMenuActionP12GameMenuItemi");
void zap_delete_entry(void*, int) asm("_Z19ClearGameMenuActionP12GameMenuItemi");
void zap_set_the_achievement(int) asm("_Z17setTheAchievementi");
void draw_frame() asm("_Z13SWHardwareEndP14SpriteWorldRec");

// The game's state as one object (gGameState, 0x8D0000 bytes here): the parts the helper reads
// lie in it at the game's own offsets (docs/re/linux.md, "Lookup: targets"), among them the flags
// at +0xED, the player state at +0x1CD50, the item pool at +0x79E990 and the message slots at
// +0x8C6B48. Zero but for what main() sets. gPreferences: the weapon strip's gate at +0x1DD.
unsigned char gGameState[0x8D0000];
unsigned char gPreferences[0x200];
}

// What the stand-in itself reads and writes of gGameState: the player state, the last message
// and the message slots (the others are the helper's alone).
template <class T, std::size_t N>
T (&in_state(std::size_t offset))[N] {
    return *reinterpret_cast<T(*)[N]>(gGameState + offset);
}
unsigned char (&zap_player)[0x1110] = in_state<unsigned char, 0x1110>(0x1CD50);
unsigned char (&zap_last_message)[16] = in_state<unsigned char, 16>(0x13350);
unsigned char (&zap_message_slots)[4 * 0x1628] = in_state<unsigned char, 4 * 0x1628>(0x8C6B48);

// The new-game state, as the game keeps it in its .bss (native/src/newgame.h), for
// tests/test_native_linux.py's new games.
extern "C" {
unsigned char zap_section[0x908];
unsigned char zap_menu[0x114];
void* zap_transition;
int zap_timer;
int zap_slot;
short zap_start_count;
unsigned char zap_start_cells[4 * 64];
unsigned char zap_start_flags[64];
__attribute__((noinline, used)) void zap_new_game() { asm volatile(""); }
// Set through the socket: the player's START GAME, which the game answers by writing its exit
// action itself. The helper's `write` is not for the game's own tables, so the stand-in's frame
// loop makes that write.
unsigned char zap_press;
// The one place in the stand-in's .bss the helper's `write` refuses (helper.cpp, kWriteBase; the
// game's own is everything but what the client writes).
unsigned char zap_no_write[16];
// The stand-in's placement: the item table's first location, as the game's placement writes it.
__attribute__((noinline, used)) void zap_placement() { zap_section[0x26C] = 0x5A; }
// The menus the ARCHIPELAGO choice reads (no menu is ever up in the stand-in: it is not offered).
void* zap_menu_top;
// The translation object (gCurrentTranslation's size; its flag at +0x91, off), its counts, and
// its two builders: the message's records its ints, entry and kind and its text (4-byte wchar_t,
// as the Linux build's), the label's the string it was handed (a std::wstring by reference).
unsigned char zap_translation[0x40150];
short zap_small_count;
short zap_large_count;  // LargeText's (none here: the Feats screen's title stays in pixels)
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
// (above) with its inventory at +0x108E, the item records, the last message (its pointer, its
// sequence at +8) and the message slots (above). The save loaded byte (gGameState + 0xACA) is 0: a
// save loaded. The stand-in functions keep what they were given, for the test to read: the last
// popup's text, its sound, the HUD numbers.
// The game's pointer to the player in play (gMainCharacterSpriteP): at zap_player as in play,
// from main(), unless ZAP_FAKE_NO_PLAYER is set (the helper then configures itself only once a
// test writes the pointer: as a game at its title screen, before any save, would).
void* zap_main_character;
unsigned char zap_cursor;  // 0: no menu up
void* zap_records;
unsigned char zap_record_table[51 * 100];
unsigned char zap_sounds[8];
char zap_shown[256];  // the last message's text
int zap_shown_count;
int zap_sound_id = -1;
double zap_hud_values[2];  // the last two numbers (missiles, then bombs)
int zap_hud_calls;
int zap_message_steps[2];  // the last message's 5th and 6th arguments: its open and close steps
__attribute__((noinline, used)) void* zap_message(const char* text, int, int, int, int step_x,
                                                  int step_y, int) {
    // Slot 0, with a new serial and no frames left (so the next popup may follow).
    zap_message_steps[0] = step_x;
    zap_message_steps[1] = step_y;
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
// OpenGL's below), the pools, flags, map and frame clock (in gGameState, empty), the palettes, the
// templates, the area events (the scraps' alone, below). The pickup's popup width is
// playerCollideProc's `mov esi, 24` (BE 18 00 00 00) at +0x251E, as the game's (docs/re/linux.md,
// `popup_width_code`), patched in place.
void* zap_sprite_world;
void* zap_palettes[65];
void* zap_templates;
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
// The OS Install Disks' count: the Text table (32-byte entries, the scanner's 12th as the game
// has it), the menu functions (none called here: no menu opens in the stand-in), and the pause
// screen's KEYS branch (PauseGame's at +0x9C: `jne +0x3C` first, as at 0x618A7E).
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
// RETURN HOME WITH THE DISKS?: the fade's block and level (in gGameState), and the routines (none
// set: the stand-in never opens the prompt, which needs event 3667 as the game loads it).
void* zap_routines[16];
// Traps: AILMENTS (gGameState + 0x13724, 0: NORMAL), the halt byte, and weaponCool, the game's two
// AILMENTS tests as the Linux build has them (trap.cpp's kHitGame at +0xAD5 then kWaveGame at
// +0xB65, each load reading AILMENTS), patched in place while a trap's timer runs.
unsigned char zap_halt;
// DeathLink: the player's death (PlayerDestruction's arguments), noted: the player and kind.
int zap_death_count;
void* zap_death_player;
int zap_death_kind = -1;
// The title's lines: the layer the title draws on (no title here: null) and the text functions.
void* zap_draw_layer;
__attribute__((noinline, used)) void* zap_text_new(const char*, int, int) { return nullptr; }
__attribute__((noinline, used)) void zap_text_link(void*) { asm volatile(""); }
// The Feats guard: the save slots (empty here), and Steam's stats as the game reaches them, its
// SteamUserStats() context (init, cookie, interface) whose interface's SetAchievement (vtable
// +0x30) counts; zap_achieve set makes the next frame set one, as setTheAchievement does.
unsigned char zap_save_games[30 * 0x9408];
unsigned char zap_saved_randos[30 * 0x908];
extern "C" void* SteamInternal_ContextInit(void* context);  // the stub library's (or the helper's)
int zap_achievements;
unsigned char zap_achieve;
bool zap_set_achievement(void*, const char*) {
    ++zap_achievements;
    return true;
}
void* zap_stats_vtable[16];
void* zap_stats_object = zap_stats_vtable;
void zap_stats_init(void** interface) { *interface = &zap_stats_object; }
void* zap_stats_context[3] = {reinterpret_cast<void*>(&zap_stats_init), nullptr, nullptr};
void zap_steam_achievement() {
    void* interface = *static_cast<void**>(SteamInternal_ContextInit(zap_stats_context));
    void** vtable = *static_cast<void***>(interface);
    reinterpret_cast<bool (*)(void*, const char*)>(vtable[0x30 / 8])(interface, "14Victory");
}
// The ASSIST lock: ASSIST's callback (none called here: no options menu opens in the stand-in).
__attribute__((noinline, used)) void zap_assist_menu() { asm volatile(""); }
// The file select's handlers (never called: the stand-in has no menus).
__attribute__((noinline, used)) void zap_file_panel(void*, int) { asm volatile(""); }
__attribute__((noinline, used)) void zap_file_entry(void*, int) { asm volatile(""); }
__attribute__((noinline, used)) void zap_copy_entry(void*, int) { asm volatile(""); }
__attribute__((noinline, used)) void zap_delete_entry(void*, int) { asm volatile(""); }
__attribute__((noinline, used)) void zap_death(void* player, char kind) {
    ++zap_death_count;
    zap_death_player = player;
    zap_death_kind = kind;
}
// The map: the item update's two tests of the ID (PowerUpMoveProc's, at +0x788 and 0x112 after,
// the code between as filler), the move procedures (entry 120 the map's: none open here) and the
// function setting a text's glyphs.
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

// The game's code the helper reads or patches, under the game's own names with the filler the
// helper's offsets into each call for (docs/re/linux.md, "Lookup: targets"): the bytes at those
// offsets are the game's own, as the helper checks them before it writes; the rest is never run.
// The pickup's popup width (`mov esi, 24`) at playerCollideProc + 0x251E.
asm(".text\n.globl _Z17playerCollideProcP10CollideRecS0_P6dPointc\n"
    ".type _Z17playerCollideProcP10CollideRecS0_P6dPointc, @function\n"
    "_Z17playerCollideProcP10CollideRecS0_P6dPointc:\n"
    ".fill 0x251E, 1, 0x90\n"
    "  movl $24, %esi\n  ret\n"
    ".size _Z17playerCollideProcP10CollideRecS0_P6dPointc, "
    ".-_Z17playerCollideProcP10CollideRecS0_P6dPointc\n");

// The pause screen's KEYS branch at PauseGame + 0x9C.
asm(".text\n.globl _Z9PauseGamev\n.type _Z9PauseGamev, @function\n_Z9PauseGamev:\n"
    ".fill 0x9C, 1, 0x90\n"
    ".byte 0x75, 0x3C\n  ret\n.size _Z9PauseGamev, .-_Z9PauseGamev\n");

// EndingCutscene's first achievement call (at +0x3B) and its join (at +0x39C), as the Linux build
// has them: `mov edi, 13; call setTheAchievement; mov word [rbp-0x26], 0`, then, before the join,
// flag 251's store, and at the join flag 250's (the helper's patch jumps from the one to the
// other). Flags are gGameState + 0xED + the flag.
extern "C" void zap_set_the_achievement(int) {}
asm(".text\n.globl _Z14EndingCutscenev\n.type _Z14EndingCutscenev, @function\n_Z14EndingCutscenev:\n"
    ".fill 0x3B, 1, 0x90\n"
    "  movl $13, %edi\n  call _Z17setTheAchievementi\n  movw $0, -0x26(%rbp)\n"
    ".fill 0x39C - 7 - 0x4B, 1, 0x90\n"
    "  movb $1, gGameState+0xED+251(%rip)\n"
    ".globl zap_ending_join\nzap_ending_join:\n"
    "  movb $1, gGameState+0xED+250(%rip)\n  ret\n.size _Z14EndingCutscenev, .-_Z14EndingCutscenev\n");

// setGameMessageStringClipping's top cut at +0x661, byte for byte, never run.
asm(".text\n.globl _Z28setGameMessageStringClippingP9SpriteRecii\n"
    ".type _Z28setGameMessageStringClippingP9SpriteRecii, @function\n"
    "_Z28setGameMessageStringClippingP9SpriteRecii:\n"
    ".fill 0x661, 1, 0x90\n"
    ".byte 0x8B, 0x45, 0xE4, 0x99, 0xC1, 0xEA, 0x1D, 0x01, 0xD0, 0x83, 0xE0, 0x07, 0x29, 0xD0\n"
    "  ret\n.size _Z28setGameMessageStringClippingP9SpriteRecii, "
    ".-_Z28setGameMessageStringClippingP9SpriteRecii\n");

// PowerUpMoveProc's two tests of the item's ID, byte for byte, 0x112 apart, the first at +0x788,
// never run.
#define ZAP_ID_TEST(minor, past)                                                          \
    ".byte 0x66, 0x83, 0xF8, 0x02, 0x7E, " minor ", 0x48, 0x8B, 0x45, 0xE8, 0x0F, 0xB7\n" \
    ".byte 0x80, 0xE8, 0x01, 0x00, 0x00, 0x66, 0x83, 0xF8, 0x23, 0x7F, " past "\n"
asm(".text\n.globl _Z15PowerUpMoveProcP9SpriteRec\n.type _Z15PowerUpMoveProcP9SpriteRec, @function\n"
    "_Z15PowerUpMoveProcP9SpriteRec:\n"
    ".fill 0x788, 1, 0x90\n" ZAP_ID_TEST("0x60", "0x4F") ".fill 0xFB, 1, 0x90\n"
    ZAP_ID_TEST("0x5D", "0x4C")
    "  ret\n.size _Z15PowerUpMoveProcP9SpriteRec, .-_Z15PowerUpMoveProcP9SpriteRec\n");

// weaponCool's two tests, byte for byte (but where the loads point: AILMENTS, gGameState +
// 0x13724), the first at +0xAD5 and the second at +0xB65, never run.
#define ZAP_LOAD ".byte 0x0F, 0xB7, 0x05\n.long gGameState+0x13724 - (. + 4)\n"
asm(".text\n.globl _Z10weaponCoolP12PlayerStruct\n.type _Z10weaponCoolP12PlayerStruct, @function\n"
    "_Z10weaponCoolP12PlayerStruct:\n"
    ".fill 0xAD5, 1, 0x90\n"
    ZAP_LOAD
    ".byte 0x66, 0x83, 0xF8, 0x01, 0x75, 0x1B, 0x48, 0x8B, 0x45, 0xE8, 0x0F, 0xB7, 0x80, 0xC2\n"
    ".byte 0x0D, 0x00, 0x00, 0x83, 0xE8, 0x02, 0x89, 0xC2, 0x48, 0x8B, 0x45, 0xE8, 0x66, 0x89\n"
    ".byte 0x90, 0xC2, 0x0D, 0x00, 0x00\n"
    ZAP_LOAD
    ".byte 0x66, 0x83, 0xF8, 0x02, 0x75, 0x0D\n"
    ".fill 0xB65 - 0xAD5 - 0x35, 1, 0x90\n"  // the second test lies 0x90 after the first
    ZAP_LOAD
    ".byte 0x66, 0x83, 0xF8, 0x01, 0x75, 0x1E, 0x48, 0x8B, 0x45, 0xE8, 0x0F, 0xB7, 0x80, 0xC4\n"
    ".byte 0x0D, 0x00, 0x00, 0x66, 0x83, 0xF8, 0x3C, 0x7E, 0x0D, 0x48, 0x8B, 0x45, 0xE8, 0x66\n"
    ".byte 0xC7, 0x80, 0xC4, 0x0D, 0x00, 0x00, 0x3C, 0x00\n"
    ZAP_LOAD
    ".byte 0x66, 0x83, 0xF8, 0x02, 0x75, 0x0D\n"
    "  ret\n.size _Z10weaponCoolP12PlayerStruct, .-_Z10weaponCoolP12PlayerStruct\n");

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
    if (std::getenv("ZAP_FAKE_NO_PLAYER") == nullptr) zap_main_character = zap_player;
    zap_stats_vtable[0x30 / 8] = reinterpret_cast<void*>(&zap_set_achievement);
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
    // ZAP_FAKE_NO_FRAME: the game never swaps from its frame function, only (every frame) from the
    // other window's: no frame work may run for any call a wrapper passes on.
    const bool editor_only = std::getenv("ZAP_FAKE_NO_FRAME") != nullptr;
    for (unsigned long frame = 0; !g_stop; ++frame) {
        if (!editor_only) draw_frame();
        if (editor_only || frame % 4 == 0) draw_editor();
        if (zap_press != 0) {  // the game's own write of its exit action
            zap_press = 0;
            zap_transition = reinterpret_cast<void*>(&zap_new_game);
        }
        if (zap_achieve != 0) {  // the game thread, between frames, as the game's own
            zap_achieve = 0;
            zap_steam_achievement();
        }
        nanosleep(&pause, nullptr);
    }
    std::printf("swaps %lu\n", zap_stub_swaps());
    return 0;
}
