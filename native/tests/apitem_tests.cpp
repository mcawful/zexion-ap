// Host tests for other players' items (apitem.h): the stand-in records restyled and named by
// cell, the logo painted through a fake OpenGL, and everything put back; and the label parser.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "apitem.h"
#include "game_calls.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

// --- a stand-in item table: 51 records of 100 bytes, records 47 and 48 as the game loads them

BYTE g_records[51 * 100];
BYTE* g_records_ptr = g_records;
BYTE* const g_scrap = g_records + 47 * 100;
BYTE* const g_chunk = g_records + 48 * 100;

void put16(BYTE* p, ptrdiff_t o, int16_t v) { std::memcpy(p + o, &v, 2); }
int16_t get16(const BYTE* p, ptrdiff_t o) {
    int16_t v;
    std::memcpy(&v, p + o, 2);
    return v;
}

void own_record(BYTE* r, int16_t frame, const char* name, int16_t event) {
    put16(r, 0x00, 34);
    put16(r, 0x02, frame);
    put16(r, 0x04, 6);
    put16(r, 0x06, 2);
    r[0x08] = 0;
    r[0x09] = 8;
    std::memcpy(r + 0x0A, name, std::strlen(name) + 1);
    put16(r, 0x3C, 46);
    put16(r, 0x3E, 45);
    put16(r, 0x40, event);
    put16(r, 0x58, 1);
    put16(r, 0x5A, 13);
    put16(r, 0x60, 244);
}

void reset_records() {
    std::memset(g_records, 0, sizeof(g_records));
    own_record(g_scrap, 134, "SCRAP ENERGY", 3549);
    own_record(g_chunk, 142, "SCRAP CHUNK", 3550);
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
// The texture as read back: Sprites.png's known art (Scrap Energy's frame at (6, 0), glyph 0xC0
// at (2, 1)) opaque, unless a test turns it off; the logos' places blank, unless a test puts
// something in frame 95.
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
    BYTE before[100], before_chunk[100];
    std::memcpy(before, g_scrap, 100);
    std::memcpy(before_chunk, g_chunk, 100);
    items.watch(holder(), 5);
    check(!items.restyled() && std::memcmp(before, g_scrap, 100) == 0,
          "nothing happens before the labels arrive");

    items.set_labels({{2 * 54 + 5, "MASTER SWORD FOR ALICE"}});
    items.watch(holder(), 2 * 54 + 5);
    check(items.restyled(), "restyled");
    check(get16(g_scrap, 0x02) == 63 && get16(g_scrap, 0x06) == 1 && g_scrap[0x08] == 0xEE,
          "the logo frame, one frame, the logo glyph");
    check(get16(g_scrap, 0x40) == -1, "no refill drop at pickup");
    check(g_scrap[0x09] == 8, "Scrap Energy's own palette, whose colors the logo uses");
    check(get16(g_scrap, 0x60) == 244 && get16(g_scrap, 0x3C) == 46, "the rest left alone");
    check(name() == "MASTER SWORD FOR ALICE", "named for the player's cell");
    check(get16(g_chunk, 0x02) == 95 && get16(g_chunk, 0x06) == 1 && g_chunk[0x08] == 0xEE &&
              get16(g_chunk, 0x40) == -1 && name(g_chunk) == "MASTER SWORD FOR ALICE",
          "Scrap Chunk too: the arrow logo's frame, the same glyph, no refill, the same name");
    items.watch(holder(), 7);
    check(name() == "ARCHIPELAGO ITEM", "elsewhere, the general name");
    std::string long_name(60, 'A');
    items.set_labels({{7, long_name}});
    items.watch(holder(), 7);
    check(name() == std::string(49, 'A') && g_scrap[0x0A + 49] == 0,
          "a name is cut to 49 bytes and stays terminated");

    items.restore(holder(), sprites(), zap::GlFunctions{});
    check(std::memcmp(before, g_scrap, 100) == 0, "restore puts the whole record back");
    check(std::memcmp(before_chunk, g_chunk, 100) == 0, "and Scrap Chunk's");
    check(!items.restyled() && !items.active(), "and forgets them");
}

void test_one_record_not_as_expected_leaves_the_other_restyled() {
    reset_records();
    put16(g_chunk, 0x40, 3551);  // not as traced
    BYTE before_chunk[100];
    std::memcpy(before_chunk, g_chunk, 100);
    zap::ApItems items;
    items.set_labels({{1, "X"}});
    items.watch(holder(), 1);
    check(items.restyled() && get16(g_scrap, 0x02) == 63, "Scrap Energy is restyled");
    check(std::memcmp(before_chunk, g_chunk, 100) == 0, "Scrap Chunk, not as traced, is not");
    items.restore(holder(), sprites(), zap::GlFunctions{});
    check(std::memcmp(before_chunk, g_chunk, 100) == 0, "and restore leaves it alone too");
}

void test_records_not_as_expected_are_left_alone() {
    reset_records();
    put16(g_scrap, 0x40, 3550);  // another build's event, say
    put16(g_chunk, 0x40, 3551);
    BYTE before[100];
    std::memcpy(before, g_scrap, 100);
    zap::ApItems items;
    items.set_labels({{1, "X"}});
    items.watch(holder(), 1);
    check(!items.restyled() && std::memcmp(before, g_scrap, 100) == 0,
          "a record not as traced is not written");

    BYTE* none = nullptr;
    zap::ApItems no_table;
    no_table.set_labels({{1, "X"}});
    no_table.watch(reinterpret_cast<BYTE*>(&none), 1);
    check(!no_table.restyled(), "no records loaded: nothing");
}

void test_a_record_left_restyled_is_adopted_and_put_back() {
    reset_records();
    BYTE before[100];
    std::memcpy(before, g_scrap, 100);
    zap::ApItems earlier;
    earlier.set_labels({{1, "OLD NAME"}});
    earlier.watch(holder(), 1);  // and then unloaded with no frame to restore it
    zap::ApItems later;
    later.set_labels({{1, "NEW NAME"}});
    later.watch(holder(), 1);
    check(later.restyled() && name() == "NEW NAME", "a later helper takes it over and renames it");
    later.restore(holder(), sprites(), zap::GlFunctions{});
    check(std::memcmp(before, g_scrap, 100) == 0, "and puts the game's own values back");
}

void test_a_reload_is_restyled_again() {
    reset_records();
    zap::ApItems items;
    items.set_labels({{1, "X"}});
    items.watch(holder(), 1);
    reset_records();  // the game reloaded its records
    items.watch(holder(), 1);
    check(get16(g_scrap, 0x02) == 63 && name() == "X", "restyled again after a reload");
}

void test_painting() {
    reset_records();
    reset_gl();
    zap::GlFunctions gl = fake_gl();
    zap::ApItems items;
    items.paint(sprites(), gl, 0);
    check(g_uploads.empty(), "nothing painted before the labels arrive");
    items.set_labels({{1, "X"}});
    items.paint(sprites(), gl, 0);
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
    items.paint(sprites(), gl, 0);
    check(g_uploads.empty(), "not every frame");
    items.paint(sprites(), gl, 299);
    check(g_uploads.empty(), "the arrow keeps its color for 300 ms");
    items.paint(sprites(), gl, 300);
    check(g_uploads.size() == 1 && g_uploads[0].x == 240 && g_uploads[0].y == 816,
          "then only the arrow's frame is repainted");
    std::vector<uint8_t> second = g_uploads.empty() ? std::vector<uint8_t>() : g_uploads[0].pixels;
    g_uploads.clear();
    items.paint(sprites(), gl, 450);
    check(g_uploads.empty(), "not again until the color changes");
    items.paint(sprites(), gl, 600);
    check(g_uploads.size() == 1 && g_uploads[0].pixels != second, "and back: the arrow flashes");
    check(g_bound == 3 && g_row_length == 5, "the game's state put back after an arrow repaint");
    g_uploads.clear();

    // A texture whose known art is not where the PNG has it (flipped, say): never painted.
    reset_gl();
    g_known_art = false;
    zap::ApItems flipped;
    flipped.set_labels({{1, "X"}});
    flipped.paint(sprites(), gl, 0);
    check(g_uploads.empty() && flipped.painted_texture() == 0, "unexpected art: nothing painted");

    // Something already where the arrow logo goes (another build's art, say): never painted.
    reset_gl();
    g_frame95_used = true;
    zap::ApItems used;
    used.set_labels({{1, "X"}});
    used.paint(sprites(), gl, 0);
    check(g_uploads.empty() && used.painted_texture() == 0, "frame 95 not empty: nothing painted");

    // Another texture size (not the sprite sheet): nothing uploaded.
    reset_gl();
    g_width = 1024;
    zap::ApItems other;
    other.set_labels({{1, "X"}});
    other.paint(sprites(), gl, 0);
    check(g_uploads.empty() && other.painted_texture() == 0, "a texture of another size is left alone");

    // An unpack buffer bound: pixels would be read from it, so nothing is uploaded.
    reset_gl();
    g_unpack_buffer = 9;
    zap::ApItems buffered;
    buffered.set_labels({{1, "X"}});
    buffered.paint(sprites(), gl, 0);
    check(g_uploads.empty(), "an unpack buffer bound: nothing uploaded");

    // Restore clears the painted pixels.
    reset_gl();
    zap::ApItems painted;
    painted.set_labels({{1, "X"}});
    painted.paint(sprites(), gl, 0);
    g_uploads.clear();
    painted.restore(holder(), sprites(), gl);
    bool cleared = g_uploads.size() == 3;
    for (const Upload& u : g_uploads) {
        for (uint8_t b : u.pixels) cleared &= b == 0;
    }
    check(cleared, "restore clears both frames and the glyph");
}

void test_label_parser() {
    std::map<int, std::string> out;
    check(zap::parse_labels("ap_labels\nlabel 5 2 4d617374657220537764", out).empty(), "parses");
    check(out.size() == 1 && out[2 * 54 + 5] == "MASTER SWD", "upper-cased, by cell");
    out.clear();
    zap::parse_labels("ap_labels\nlabel 1 1 c3a97e", out);
    check(out[55] == "ARCHIPELAGO ITEM", "nothing the font draws: the general name");
    zap::parse_labels("ap_labels\nlabel 2 1 41004221", out);
    check(out[56] == "AB!", "a NUL byte is dropped, not kept");
    std::map<int, std::string> bad;
    check(!zap::parse_labels("ap_labels\nlabel 54 0 41", bad).empty(), "a cell off the map");
    check(!zap::parse_labels("ap_labels\nlabel 1 1 4", bad).empty(), "odd hex");
    check(!zap::parse_labels("ap_labels\nname 1 1 41", bad).empty(), "unknown lines");
}

}  // namespace

int main() {
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
