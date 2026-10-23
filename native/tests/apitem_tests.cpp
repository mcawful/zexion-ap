// Host tests for other players' items (apitem.h): the stand-in record restyled and named by
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

// --- a stand-in item table: 51 records of 100 bytes, record 47 as the game loads it -------

BYTE g_records[51 * 100];
BYTE* g_records_ptr = g_records;
BYTE* const g_scrap = g_records + 47 * 100;

void put16(BYTE* p, ptrdiff_t o, int16_t v) { std::memcpy(p + o, &v, 2); }
int16_t get16(const BYTE* p, ptrdiff_t o) {
    int16_t v;
    std::memcpy(&v, p + o, 2);
    return v;
}

void reset_records() {
    std::memset(g_records, 0, sizeof(g_records));
    put16(g_scrap, 0x00, 34);
    put16(g_scrap, 0x02, 134);
    put16(g_scrap, 0x04, 6);
    put16(g_scrap, 0x06, 2);
    g_scrap[0x08] = 0;
    g_scrap[0x09] = 8;
    std::memcpy(g_scrap + 0x0A, "SCRAP ENERGY", 13);
    put16(g_scrap, 0x3C, 46);
    put16(g_scrap, 0x3E, 45);
    put16(g_scrap, 0x40, 3549);
    put16(g_scrap, 0x58, 1);
    put16(g_scrap, 0x5A, 13);
    put16(g_scrap, 0x60, 244);
}

std::string name() { return std::string(reinterpret_cast<char*>(g_scrap + 0x0A)); }

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

zap::GlFunctions fake_gl() {
    zap::GlFunctions gl;
    gl.BindTexture = fake_bind;
    gl.GetIntegerv = fake_get_integer;
    gl.GetTexLevelParameteriv = fake_level_parameter;
    gl.PixelStorei = fake_pixel_store;
    gl.TexSubImage2D = fake_sub_image;
    gl.GetError = fake_get_error;
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
}

BYTE* holder() { return reinterpret_cast<BYTE*>(&g_records_ptr); }
BYTE* sprites() { return reinterpret_cast<BYTE*>(&g_sprite_holder); }

// --- tests -----------------------------------------------------------------------------------

void test_restyle_and_names() {
    reset_records();
    zap::ApItems items;
    BYTE before[100];
    std::memcpy(before, g_scrap, 100);
    items.watch(holder(), 5);
    check(!items.restyled() && std::memcmp(before, g_scrap, 100) == 0,
          "nothing happens before the labels arrive");

    items.set_labels({{2 * 54 + 5, "MASTER SWORD FOR ALICE"}});
    items.watch(holder(), 2 * 54 + 5);
    check(items.restyled(), "restyled");
    check(get16(g_scrap, 0x02) == 63 && get16(g_scrap, 0x06) == 1 && g_scrap[0x08] == 0xEE,
          "the logo frame, one frame, the logo glyph");
    check(get16(g_scrap, 0x40) == -1, "no refill drop at pickup");
    check(get16(g_scrap, 0x60) == 244 && get16(g_scrap, 0x3C) == 46, "the rest left alone");
    check(name() == "MASTER SWORD FOR ALICE", "named for the player's cell");
    items.watch(holder(), 7);
    check(name() == "ARCHIPELAGO ITEM", "elsewhere, the general name");
    std::string long_name(60, 'A');
    items.set_labels({{7, long_name}});
    items.watch(holder(), 7);
    check(name() == std::string(49, 'A') && g_scrap[0x0A + 49] == 0,
          "a name is cut to 49 bytes and stays terminated");

    items.restore(holder(), sprites(), zap::GlFunctions{});
    check(std::memcmp(before, g_scrap, 100) == 0, "restore puts the whole record back");
    check(!items.restyled() && !items.active(), "and forgets it");
}

void test_records_not_as_expected_are_left_alone() {
    reset_records();
    put16(g_scrap, 0x40, 3550);  // another build's event, say
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
    items.paint(sprites(), gl);
    check(g_uploads.empty(), "nothing painted before the labels arrive");
    items.set_labels({{1, "X"}});
    items.paint(sprites(), gl);
    check(g_uploads.size() == 2, "the frame and the glyph");
    if (g_uploads.size() != 2) return;
    check(items.painted_texture() == 7, "into the sprite texture");
    check(g_uploads[0].x == 240 && g_uploads[0].y == 752 && g_uploads[0].w == 16,
          "frame 63 of the item sprites");
    check(g_uploads[1].x == 112 && g_uploads[1].y == 1088 && g_uploads[1].w == 8,
          "glyph 0xEE of the font");
    bool any_opaque = false;
    for (size_t i = 3; i < g_uploads[0].pixels.size(); i += 4) any_opaque |= g_uploads[0].pixels[i] != 0;
    check(any_opaque, "the logo has pixels");
    check(g_bound == 3 && g_row_length == 5, "the game's binding and unpack state put back");

    g_uploads.clear();
    items.paint(sprites(), gl);
    check(g_uploads.empty(), "not every frame");

    // Another texture size (not the sprite sheet): nothing uploaded.
    reset_gl();
    g_width = 1024;
    zap::ApItems other;
    other.set_labels({{1, "X"}});
    other.paint(sprites(), gl);
    check(g_uploads.empty() && other.painted_texture() == 0, "a texture of another size is left alone");

    // An unpack buffer bound: pixels would be read from it, so nothing is uploaded.
    reset_gl();
    g_unpack_buffer = 9;
    zap::ApItems buffered;
    buffered.set_labels({{1, "X"}});
    buffered.paint(sprites(), gl);
    check(g_uploads.empty(), "an unpack buffer bound: nothing uploaded");

    // Restore clears the painted pixels.
    reset_gl();
    zap::ApItems painted;
    painted.set_labels({{1, "X"}});
    painted.paint(sprites(), gl);
    g_uploads.clear();
    painted.restore(holder(), sprites(), gl);
    bool cleared = g_uploads.size() == 2;
    for (const Upload& u : g_uploads) {
        for (uint8_t b : u.pixels) cleared &= b == 0;
    }
    check(cleared, "restore clears the frame and the glyph");
}

void test_label_parser() {
    std::map<int, std::string> out;
    check(zap::parse_labels("ap_labels\nlabel 5 2 4d617374657220537764", out).empty(), "parses");
    check(out.size() == 1 && out[2 * 54 + 5] == "MASTER SWD", "upper-cased, by cell");
    out.clear();
    zap::parse_labels("ap_labels\nlabel 1 1 c3a97e", out);
    check(out[55] == "ARCHIPELAGO ITEM", "nothing the font draws: the general name");
    std::map<int, std::string> bad;
    check(!zap::parse_labels("ap_labels\nlabel 54 0 41", bad).empty(), "a cell off the map");
    check(!zap::parse_labels("ap_labels\nlabel 1 1 4", bad).empty(), "odd hex");
    check(!zap::parse_labels("ap_labels\nname 1 1 41", bad).empty(), "unknown lines");
}

}  // namespace

int main() {
    test_restyle_and_names();
    test_records_not_as_expected_are_left_alone();
    test_a_reload_is_restyled_again();
    test_painting();
    test_label_parser();
    if (g_failures == 0) std::printf("apitem tests: all passed%c", 10);
    return g_failures == 0 ? 0 : 1;
}
