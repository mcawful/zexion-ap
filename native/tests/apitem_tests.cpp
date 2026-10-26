// Host tests for apitem.h: the stand-in records restyled and named by cell, the logos and font
// glyphs painted through a fake OpenGL (one readback shared), all put back; and the label parser.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "apitem.h"
#include "game_calls.h"

namespace {

// --- a stand-in item table: 51 records of 100 bytes, records 47 and 48 as the game loads them

BYTE g_records[51 * 100];
BYTE* g_records_ptr = g_records;
BYTE* const g_scrap = g_records + 47 * 100;
BYTE* const g_chunk = g_records + 48 * 100;


void reset_records() {
    std::memset(g_records, 0, sizeof(g_records));
    own_stand_in(g_scrap, 134, "SCRAP ENERGY", 3549);
    own_stand_in(g_chunk, 142, "SCRAP CHUNK", 3550);
}

std::string name(const BYTE* r = g_scrap) {
    return std::string(reinterpret_cast<const char*>(r + 0x0A));
}

// --- a fake OpenGL ---------------------------------------------------------------------------

struct Upload {
    int x, y, w, h;
    std::vector<uint8_t> pixels;
};
unsigned g_bound = 3;  // the game's own binding, to be put back
int g_width = 2048, g_unpack_buffer = 0, g_row_length = 5;
std::vector<Upload> g_uploads;
std::vector<unsigned> g_binds;

void WINAPI fake_bind(unsigned, unsigned texture) {
    g_binds.push_back(texture);
    g_bound = texture;
}
void WINAPI fake_get_integer(unsigned name, int* value) {
    if (name == 0x8069) *value = static_cast<int>(g_bound);
    if (name == 0x88EF) *value = g_unpack_buffer;
    if (name == 0x0CF2) *value = g_row_length;
    if (name == 0x0CF3 || name == 0x0CF4) *value = 0;
}
void WINAPI fake_level_parameter(unsigned, int, unsigned, int* value) { *value = g_width; }
void WINAPI fake_pixel_store(unsigned name, int value) {
    if (name == 0x0CF2) g_row_length = value;
}
void WINAPI fake_sub_image(unsigned, int, int x, int y, int w, int h, unsigned, unsigned,
                           const void* pixels) {
    auto* p = static_cast<const uint8_t*>(pixels);
    g_uploads.push_back({x, y, w, h, std::vector<uint8_t>(p, p + w * h * 4)});
}
unsigned WINAPI fake_get_error() { return 0; }
// The readback: the known art apitem.cpp checks (Scrap Energy's frame 134 at (6, 0), glyph 0xC0
// at (2, 1)) opaque unless g_known_art is off; the logo places blank unless g_frame95_used.
bool g_frame95_used = false;
bool g_known_art = true;
int g_readbacks = 0;
void WINAPI fake_get_tex_image(unsigned, int, unsigned, unsigned, void* pixels) {
    ++g_readbacks;
    auto* p = static_cast<uint8_t*>(pixels);
    std::memset(p, 0, static_cast<size_t>(2048) * 2048 * 4);
    if (!g_known_art) return;
    auto opaque = [&](int x, int y) { p[(static_cast<size_t>(y) * 2048 + x) * 4 + 3] = 0xFF; };
    opaque(128 + (134 % 8) * 16 + 6, 640 + (134 / 8) * 16);
    opaque((0xC0 % 16) * 8 + 2, 976 + (0xC0 / 16) * 8 + 1);
    if (g_frame95_used) opaque(128 + (95 % 8) * 16, 640 + (95 / 8) * 16);  // where the logo is not
}
unsigned char WINAPI fake_is_texture(unsigned texture) { return texture != 0; }

zap::GlFunctions fake_gl() {
    zap::GlFunctions gl;
    gl.BindTexture = fake_bind;
    gl.GetIntegerv = fake_get_integer;
    gl.GetTexLevelParameteriv = fake_level_parameter;
    gl.PixelStorei = fake_pixel_store;
    gl.TexSubImage2D = fake_sub_image;
    gl.GetError = fake_get_error;
    gl.GetTexImage = fake_get_tex_image;
    gl.IsTexture = fake_is_texture;
    return gl;
}

uint32_t g_texture_object[2] = {7, 0};
BYTE g_sprite_data[0x80];
BYTE* g_sprite_holder = g_sprite_data;

void reset_gl() {
    std::memset(g_sprite_data, 0, sizeof(g_sprite_data));
    *reinterpret_cast<uint32_t**>(g_sprite_data + 0x40) = g_texture_object;
    g_texture_object[0] = 7;
    g_bound = 3;
    g_width = 2048;
    g_unpack_buffer = 0;
    g_row_length = 5;
    g_uploads.clear();
    g_binds.clear();
    g_known_art = true;
    g_frame95_used = false;
    g_readbacks = 0;
}

BYTE* holder() { return reinterpret_cast<BYTE*>(&g_records_ptr); }
BYTE* sprites() { return reinterpret_cast<BYTE*>(&g_sprite_holder); }

// --- tests -----------------------------------------------------------------------------------

void test_restyle_and_names() {
    reset_records();
    zap::ApItems items;
    zap::TextureCheck items_check;
    BYTE before[100], before_chunk[100];
    std::memcpy(before, g_scrap, 100);
    std::memcpy(before_chunk, g_chunk, 100);
    items.watch(holder(), 5);
    check(!items.restyled() && std::memcmp(before, g_scrap, 100) == 0,
          "nothing happens before the labels arrive");

    items.set_labels({{2 * 54 + 5, "MASTER SWORD FOR ALICE"}});
    items.watch(holder(), 2 * 54 + 5);
    check(items.restyled(), "restyled");
    check(get<int16_t>(g_scrap, 0x02) == 63 && get<int16_t>(g_scrap, 0x06) == 1 &&
              g_scrap[0x08] == 0,
          "the logo frame, one frame, no icon of its own (the name starts with the glyph)");
    check(get<int16_t>(g_scrap, 0x40) == -1, "no refill drop at pickup");
    check(g_scrap[0x09] == 8, "Scrap Energy's own palette, whose colors the logo uses");
    check(get<int16_t>(g_scrap, 0x60) == 244 && get<int16_t>(g_scrap, 0x3C) == 46,
          "the rest left alone");
    check(name() == "MASTER SWORD FOR ALICE", "named for the player's cell");
    check(get<int16_t>(g_chunk, 0x02) == 95 && get<int16_t>(g_chunk, 0x06) == 1 &&
              g_chunk[0x08] == 0 &&
              get<int16_t>(g_chunk, 0x40) == -1 && name(g_chunk) == "MASTER SWORD FOR ALICE",
          "Scrap Chunk too: the arrow logo's frame, the same glyph, no refill, the same name");
    items.watch(holder(), 7);
    check(name() == "\xEE" "AP ITEM", "elsewhere, the glyph and the general name");
    std::string long_name(60, 'A');
    items.set_labels({{7, long_name}});
    items.watch(holder(), 7);
    check(name() == std::string(49, 'A') && g_scrap[0x0A + 49] == 0,
          "a name is cut to 49 bytes and stays terminated");

    items.restore(holder(), sprites(), zap::GlFunctions{}, items_check);
    check(std::memcmp(before, g_scrap, 100) == 0, "restore puts the whole record back");
    check(std::memcmp(before_chunk, g_chunk, 100) == 0, "and Scrap Chunk's");
    check(!items.restyled() && !items.active(), "and forgets them");
}

void test_one_record_not_as_expected_leaves_the_other_restyled() {
    reset_records();
    put<int16_t>(g_chunk, 0x40, 3551);  // not as traced
    BYTE before_chunk[100];
    std::memcpy(before_chunk, g_chunk, 100);
    zap::ApItems items;
    zap::TextureCheck items_check;
    items.set_labels({{1, "X"}});
    items.watch(holder(), 1);
    check(items.restyled() && get<int16_t>(g_scrap, 0x02) == 63, "Scrap Energy is restyled");
    check(std::memcmp(before_chunk, g_chunk, 100) == 0, "Scrap Chunk, not as traced, is not");
    items.restore(holder(), sprites(), zap::GlFunctions{}, items_check);
    check(std::memcmp(before_chunk, g_chunk, 100) == 0, "and restore leaves it alone too");
}

void test_records_not_as_expected_are_left_alone() {
    reset_records();
    put<int16_t>(g_scrap, 0x40, 3550);  // another build's event, say
    put<int16_t>(g_chunk, 0x40, 3551);
    BYTE before[100];
    std::memcpy(before, g_scrap, 100);
    zap::ApItems items;
    zap::TextureCheck items_check;
    items.set_labels({{1, "X"}});
    items.watch(holder(), 1);
    check(!items.restyled() && std::memcmp(before, g_scrap, 100) == 0,
          "a record not as traced is not written");

    BYTE* none = nullptr;
    zap::ApItems no_table;
    zap::TextureCheck no_table_check;
    no_table.set_labels({{1, "X"}});
    no_table.watch(reinterpret_cast<BYTE*>(&none), 1);
    check(!no_table.restyled(), "no records loaded: nothing");
}

void test_a_record_left_restyled_is_adopted_and_put_back() {
    reset_records();
    BYTE before[100];
    std::memcpy(before, g_scrap, 100);
    zap::ApItems earlier;
    zap::TextureCheck earlier_check;
    earlier.set_labels({{1, "OLD NAME"}});
    earlier.watch(holder(), 1);  // and then unloaded with no frame to restore it
    zap::ApItems later;
    zap::TextureCheck later_check;
    later.set_labels({{1, "NEW NAME"}});
    later.watch(holder(), 1);
    check(later.restyled() && name() == "NEW NAME", "a later helper takes it over and renames it");
    later.restore(holder(), sprites(), zap::GlFunctions{}, later_check);
    check(std::memcmp(before, g_scrap, 100) == 0, "and puts the game's own values back");
}

void test_a_reload_is_restyled_again() {
    reset_records();
    zap::ApItems items;
    zap::TextureCheck items_check;
    items.set_labels({{1, "X"}});
    items.watch(holder(), 1);
    reset_records();  // the game reloaded its records
    items.watch(holder(), 1);
    check(get<int16_t>(g_scrap, 0x02) == 63 && name() == "X", "restyled again after a reload");
}

void test_painting() {
    reset_records();
    reset_gl();
    zap::GlFunctions gl = fake_gl();
    zap::ApItems items;
    zap::TextureCheck items_check;
    items.paint(sprites(), gl, items_check, 0);
    check(g_uploads.empty(), "nothing painted before the labels arrive");
    items.set_labels({{1, "X"}});
    items.paint(sprites(), gl, items_check, 0);
    check(g_uploads.size() == 3, "two frames and the glyph");
    if (g_uploads.size() != 3) return;
    check(items.painted_texture() == 7, "into the sprite texture");
    check(g_uploads[0].x == 240 && g_uploads[0].y == 752 && g_uploads[0].w == 16,
          "frame 63 of the item sprites");
    check(g_uploads[1].x == 240 && g_uploads[1].y == 816 && g_uploads[1].w == 16,
          "frame 95, the arrow logo");
    check(g_uploads[0].pixels != g_uploads[1].pixels, "the two logos differ");
    check(g_uploads[2].x == 112 && g_uploads[2].y == 1088 && g_uploads[2].w == 8,
          "glyph 0xEE of the font");
    bool any_opaque = false;
    for (size_t i = 3; i < g_uploads[0].pixels.size(); i += 4) any_opaque |= g_uploads[0].pixels[i] != 0;
    check(any_opaque, "the logo has pixels");
    check(g_bound == 3 && g_row_length == 5, "the game's binding and unpack state put back");

    check(g_readbacks == 1, "the texture read back before the first paint");
    g_uploads.clear();
    items.paint(sprites(), gl, items_check, 0);
    check(g_uploads.empty(), "not every frame");
    items.paint(sprites(), gl, items_check, 299);
    check(g_uploads.empty(), "the arrow keeps its color for 300 ms");
    items.paint(sprites(), gl, items_check, 300);
    check(g_uploads.size() == 1 && g_uploads[0].x == 240 && g_uploads[0].y == 816,
          "then only the arrow's frame is repainted");
    std::vector<uint8_t> second = g_uploads.empty() ? std::vector<uint8_t>() : g_uploads[0].pixels;
    g_uploads.clear();
    items.paint(sprites(), gl, items_check, 450);
    check(g_uploads.empty(), "not again until the color changes");
    items.paint(sprites(), gl, items_check, 600);
    check(g_uploads.size() == 1 && g_uploads[0].pixels != second, "and back: the arrow flashes");
    check(g_bound == 3 && g_row_length == 5, "the game's state put back after an arrow repaint");
    g_uploads.clear();

    // A texture whose known art is not where the PNG has it (flipped, say): never painted.
    reset_gl();
    g_known_art = false;
    zap::ApItems flipped;
    zap::TextureCheck flipped_check;
    flipped.set_labels({{1, "X"}});
    flipped.paint(sprites(), gl, flipped_check, 0);
    check(g_uploads.empty() && flipped.painted_texture() == 0, "unexpected art: nothing painted");

    // Something already where the arrow logo goes (another build's art, say): never painted.
    reset_gl();
    g_frame95_used = true;
    zap::ApItems used;
    zap::TextureCheck used_check;
    used.set_labels({{1, "X"}});
    used.paint(sprites(), gl, used_check, 0);
    check(g_uploads.empty() && used.painted_texture() == 0, "frame 95 not empty: nothing painted");

    // Another texture size (not the sprite sheet): nothing uploaded.
    reset_gl();
    g_width = 1024;
    zap::ApItems other;
    zap::TextureCheck other_check;
    other.set_labels({{1, "X"}});
    other.paint(sprites(), gl, other_check, 0);
    check(g_uploads.empty() && other.painted_texture() == 0, "a texture of another size is left alone");

    // An unpack buffer bound: pixels would be read from it, so nothing is uploaded.
    reset_gl();
    g_unpack_buffer = 9;
    zap::ApItems buffered;
    zap::TextureCheck buffered_check;
    buffered.set_labels({{1, "X"}});
    buffered.paint(sprites(), gl, buffered_check, 0);
    check(g_uploads.empty(), "an unpack buffer bound: nothing uploaded");

    // A menu open: the logos come off (menus draw item frames too); back in the world, they go
    // back on at once.
    reset_gl();
    zap::ApItems menu;
    zap::TextureCheck menu_check;
    menu.set_labels({{1, "X"}});
    menu.paint(sprites(), gl, menu_check, 0);
    g_uploads.clear();
    menu.paint(sprites(), gl, menu_check, 10, false);
    bool blank = g_uploads.size() == 3;
    for (const Upload& u : g_uploads) {
        for (uint8_t b : u.pixels) blank &= b == 0;
    }
    check(blank && menu.painted_texture() == 0, "a menu clears both frames and the glyph");
    g_uploads.clear();
    menu.paint(sprites(), gl, menu_check, 20, false);
    check(g_uploads.empty(), "and leaves them clear while it stays open");
    menu.paint(sprites(), gl, menu_check, 30);
    check(g_uploads.size() == 3 && menu.painted_texture() == 7, "the world paints them again");

    // Restore clears the painted pixels.
    reset_gl();
    zap::ApItems painted;
    zap::TextureCheck painted_check;
    painted.set_labels({{1, "X"}});
    painted.paint(sprites(), gl, painted_check, 0);
    g_uploads.clear();
    painted.restore(holder(), sprites(), gl, painted_check);
    bool cleared = g_uploads.size() == 3;
    for (const Upload& u : g_uploads) {
        for (uint8_t b : u.pixels) cleared &= b == 0;
    }
    check(cleared, "restore clears both frames and the glyph");
}

void test_label_parser() {
    std::map<int, zap::Label> out;
    check(zap::parse_labels("ap_labels\nlabel 5 2 4d617374657220537764", out).empty(), "parses");
    check(out.size() == 1 && out[2 * 54 + 5].name == "\xEE" "MASTER SWD" &&
              out[2 * 54 + 5].width == 11,
          "upper-cased, by cell, after the glyph; one line as it is");
    out.clear();
    zap::parse_labels("ap_labels\nlabel 5 2 " + std::string("4d61702057696474682626") + "0a" +
                          "466f722046696e646572",
                      out);
    check(out[2 * 54 + 5].name == "\xEE" "MAP WIDTH&&" " FOR FINDER" && out[2 * 54 + 5].width == 12,
          "two lines, the shorter centered in the longer, whose width is the popup's");
    out.clear();
    zap::parse_labels("ap_labels\nlabel 1 1 c3a97e", out);
    check(out[55].name == "\xEE" "AP ITEM", "nothing the font draws: the general name");
    zap::parse_labels("ap_labels\nlabel 2 1 41004221", out);
    check(out[56].name == "\xEE" "AB!", "a NUL byte is dropped, not kept");
    std::map<int, zap::Label> bad;
    check(!zap::parse_labels("ap_labels\nlabel 54 0 41", bad).empty(), "a cell off the map");
    check(!zap::parse_labels("ap_labels\nlabel 1 1 4", bad).empty(), "odd hex");
    check(!zap::parse_labels("ap_labels\nname 1 1 41", bad).empty(), "unknown lines");
}

}  // namespace

void test_font_glyph() {
    reset_gl();
    zap::GlFunctions gl = fake_gl();
    zap::FontGlyph lock(zap::kLockGlyph, zap::kLockRows);
    zap::TextureCheck lock_check;
    lock.paint(sprites(), gl, lock_check);
    const int x = (zap::kLockGlyph % 16) * 8, y = 976 + (zap::kLockGlyph / 16) * 8;
    check(lock.painted() && g_uploads.size() == 1 && g_uploads[0].x == x && g_uploads[0].y == y &&
              g_uploads[0].w == 8 && g_uploads[0].h == 8,
          "the padlock goes into its font slot");
    auto alpha = [&](const Upload& u, int px, int py) { return u.pixels[(py * 8 + px) * 4 + 3]; };
    check(alpha(g_uploads[0], 2, 0) == 0xFF && alpha(g_uploads[0], 0, 0) == 0 &&
              alpha(g_uploads[0], 3, 4) == 0 && alpha(g_uploads[0], 0, 6) == 0xFF,
          "drawn from its rows");
    check(g_bound == 3 && g_row_length == 5, "the game's binding and pixel store are put back");
    lock.paint(sprites(), gl, lock_check);
    check(g_uploads.size() == 1, "not painted again every frame");
    lock.clear(sprites(), gl, lock_check);
    bool blank = true;
    for (uint8_t v : g_uploads.back().pixels) blank = blank && v == 0;
    check(!lock.painted() && g_uploads.size() == 2 && blank, "cleared");

    reset_gl();
    g_known_art = false;
    zap::FontGlyph other(zap::kLockGlyph, zap::kLockRows);
    zap::TextureCheck other_check;
    other.paint(sprites(), gl, other_check);
    check(!other.painted() && g_uploads.empty(), "a texture not as expected is left alone");
    int reads = g_readbacks;
    for (int i = 0; i < 10; ++i) other.paint(sprites(), gl, other_check);
    check(g_readbacks == reads, "and not read back again every frame");
}

// Everything painted into the sprite texture shares one read of it (16 MB), until the game
// makes the texture again.
void test_one_readback_for_every_painter() {
    reset_records();
    reset_gl();
    zap::GlFunctions gl = fake_gl();
    zap::TextureCheck shared;
    zap::ApItems items;
    items.set_labels({{1, "X"}});
    zap::FontGlyph lock(zap::kLockGlyph, zap::kLockRows);
    zap::FontGlyph disk(zap::kDiskGlyph, zap::kDiskRows);
    items.paint(sprites(), gl, shared, 0);
    lock.paint(sprites(), gl, shared);
    disk.paint(sprites(), gl, shared);
    check(g_readbacks == 1 && items.painted_texture() == 7 && lock.painted() && disk.painted(),
          "the logos and two glyphs painted after one readback");
    g_texture_object[0] = 8;  // the game made the texture again
    lock.paint(sprites(), gl, shared);
    disk.paint(sprites(), gl, shared);
    check(g_readbacks == 2 && lock.painted() && disk.painted(), "a new texture is read once more");

    // One slot taken (another build's art in frame 95): the logos are not painted, the glyphs
    // still are.
    reset_gl();
    g_frame95_used = true;
    zap::TextureCheck taken;
    zap::ApItems blocked;
    blocked.set_labels({{1, "X"}});
    zap::FontGlyph padlock(zap::kLockGlyph, zap::kLockRows);
    blocked.paint(sprites(), gl, taken, 0);
    padlock.paint(sprites(), gl, taken);
    check(blocked.painted_texture() == 0 && padlock.painted() && g_readbacks == 1,
          "a slot not blank stops only what paints there");
}

int main() {
    test_font_glyph();
    test_one_readback_for_every_painter();
    test_restyle_and_names();
    test_one_record_not_as_expected_leaves_the_other_restyled();
    test_records_not_as_expected_are_left_alone();
    test_a_record_left_restyled_is_adopted_and_put_back();
    test_a_reload_is_restyled_again();
    test_painting();
    test_label_parser();
    if (g_failures == 0) std::printf("apitem tests: all passed%c", 10);
    return g_failures == 0 ? 0 : 1;
}
