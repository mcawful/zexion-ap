// Host tests for apitem.h: the stand-in records restyled and named by cell, the logos and font
// glyphs painted through a fake OpenGL (one readback shared), all put back; and the label parser.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "apitem.h"
#include "files.h"
#include "game_calls.h"
#include "pause.h"  // the pause row's disc
#include "stand_in.h"  // the item records and the sprite data
#include "fake_gl.h"

using namespace gl_fake;

namespace {

// --- the stand-in game's item table, records 47 and 48 as the game loads them

BYTE* const g_scrap = stand_in::g_records + 47 * 100;
BYTE* const g_chunk = stand_in::g_records + 48 * 100;

void reset_records() {
    std::memset(stand_in::g_records, 0, sizeof(stand_in::g_records));
    own_stand_ins(stand_in::g_records);
}

BYTE* holder() { return reinterpret_cast<BYTE*>(&stand_in::g_records_ptr); }
BYTE* sprites() { return reinterpret_cast<BYTE*>(&stand_in::g_sprite_holder); }

// Both stand-ins named for one cell.
void watch(zap::ApItems& items, BYTE* records_holder, int cell) {
    const int cells[2] = {cell, cell};
    items.watch(records_holder, cells);
}

// --- tests -----------------------------------------------------------------------------------

void test_restyle_and_names() {
    reset_records();
    zap::ApItems items;
    zap::TextureCheck items_check;
    BYTE before[100], before_chunk[100];
    std::memcpy(before, g_scrap, 100);
    std::memcpy(before_chunk, g_chunk, 100);
    watch(items, holder(), 5);
    check(!items.restyled() && std::memcmp(before, g_scrap, 100) == 0,
          "nothing happens before the labels arrive");

    items.set_labels({{2 * 54 + 5, "MASTER SWORD FOR ALICE"}});
    watch(items, holder(), 2 * 54 + 5);
    check(items.restyled(), "restyled");
    check(get<int16_t>(g_scrap, 0x02) == 63 && get<int16_t>(g_scrap, 0x06) == 1 &&
              g_scrap[0x08] == 0,
          "the logo frame, one frame, no icon of its own (the name starts with the glyph)");
    check(get<int16_t>(g_scrap, 0x40) == -1, "no refill drop at pickup");
    check(g_scrap[0x09] == 8, "Scrap Energy's own palette, whose colors the logo uses");
    check(get<int16_t>(g_scrap, 0x60) == 244 && get<int16_t>(g_scrap, 0x3C) == 46,
          "the rest left alone");
    check(stand_in::record_name(47) == "MASTER SWORD FOR ALICE", "named for the player's cell");
    check(get<int16_t>(g_chunk, 0x02) == 95 && get<int16_t>(g_chunk, 0x06) == 1 &&
              g_chunk[0x08] == 0 && get<int16_t>(g_chunk, 0x40) == -1 &&
              stand_in::record_name(48) == "MASTER SWORD FOR ALICE",
          "Scrap Chunk too: the arrow logo's frame, the same glyph, no refill, the same name");
    watch(items, holder(), 7);
    check(stand_in::record_name(47) == "\xEE" "AP ITEM",
          "elsewhere, the glyph and the general name");
    std::string long_name(60, 'A');
    items.set_labels({{7, long_name}});
    watch(items, holder(), 7);
    check(stand_in::record_name(47) == std::string(49, 'A') && g_scrap[0x0A + 49] == 0,
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
    watch(items, holder(), 1);
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
    items.set_labels({{1, "X"}});
    watch(items, holder(), 1);
    check(!items.restyled() && std::memcmp(before, g_scrap, 100) == 0,
          "a record not as traced is not written");

    BYTE* none = nullptr;
    zap::ApItems no_table;
    no_table.set_labels({{1, "X"}});
    watch(no_table, reinterpret_cast<BYTE*>(&none), 1);
    check(!no_table.restyled(), "no records loaded: nothing");
}

void test_a_record_left_restyled_is_adopted_and_put_back() {
    reset_records();
    BYTE before[100];
    std::memcpy(before, g_scrap, 100);
    zap::ApItems earlier;
    earlier.set_labels({{1, "OLD NAME"}});
    watch(earlier, holder(), 1);  // and then unloaded with no frame to restore it
    zap::ApItems later;
    zap::TextureCheck later_check;
    later.set_labels({{1, "NEW NAME"}});
    watch(later, holder(), 1);
    check(later.restyled() && stand_in::record_name(47) == "NEW NAME",
          "a later helper takes it over and renames it");
    later.restore(holder(), sprites(), zap::GlFunctions{}, later_check);
    check(std::memcmp(before, g_scrap, 100) == 0, "and puts the game's own values back");
}

void test_a_reload_is_restyled_again() {
    reset_records();
    zap::ApItems items;
    items.set_labels({{1, "X"}});
    watch(items, holder(), 1);
    reset_records();  // the game reloaded its records
    watch(items, holder(), 1);
    check(get<int16_t>(g_scrap, 0x02) == 63 && stand_in::record_name(47) == "X",
          "restyled again after a reload");
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
    // Each of the logo's letters in its entry of Scrap Energy's palette: k 0, g 1, b 2, r 3, p 5,
    // o 8, y 9.
    const std::map<char, std::vector<uint8_t>> colors = {
        {'k', {0, 0, 0}},     {'g', {128, 0, 0}},   {'b', {212, 0, 0}},   {'r', {255, 21, 0}},
        {'p', {255, 106, 0}}, {'o', {255, 234, 0}}, {'y', {255, 255, 14}}};
    bool in_colors = true;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uint8_t* px = &g_uploads[0].pixels[(y * 16 + x) * 4];
            auto color = colors.find(zap::kApLogoRows[y][x]);
            in_colors &= color == colors.end()
                             ? px[3] == 0
                             : std::equal(px, px + 3, color->second.begin()) && px[3] == 0xFF;
        }
    }
    check(in_colors, "in its colors, the rest clear");
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

    // Never painted: a texture whose known art is not where the PNG has it (flipped, say),
    // something already where the arrow logo goes (another build's art, say), a texture of
    // another size (not the sprite sheet), an unpack buffer bound (pixels would be read from it).
    struct Case {
        const char* what;
        void (*spoil)();
    };
    const Case cases[] = {
        {"unexpected art: nothing painted", [] { g_known_art = false; }},
        {"frame 95 not empty: nothing painted", [] { g_frame95_used = true; }},
        {"a texture of another size is left alone", [] { g_width = 1024; }},
        {"an unpack buffer bound: nothing uploaded", [] { g_unpack_buffer = 9; }},
    };
    for (const Case& c : cases) {
        reset_gl();
        c.spoil();
        zap::ApItems spoiled;
        zap::TextureCheck spoiled_check;
        spoiled.set_labels({{1, "X"}});
        spoiled.paint(sprites(), gl, spoiled_check, 0);
        check(g_uploads.empty() && spoiled.painted_texture() == 0, c.what);
    }

    // A menu open: the logos come off (menus draw item frames too); back in the world, they go
    // back on at once.
    reset_gl();
    zap::ApItems menu;
    zap::TextureCheck menu_check;
    menu.set_labels({{1, "X"}});
    menu.paint(sprites(), gl, menu_check, 0);
    g_uploads.clear();
    menu.paint(sprites(), gl, menu_check, 10, false);
    check(g_uploads.size() == 3 && all_blank(g_uploads) && menu.painted_texture() == 0,
          "a menu clears both frames and the glyph");
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
    check(g_uploads.size() == 3 && all_blank(g_uploads),
          "restore clears both frames and the glyph");
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
    check(!lock.painted() && g_uploads.size() == 2 && all_blank({g_uploads.back()}), "cleared");

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

// An image of the helper's own at a place of its own: the pause row's 24 x 24 disc.
void test_an_image_of_its_own() {
    reset_gl();
    zap::GlFunctions gl = fake_gl();
    zap::FontGlyph disc(zap::kDiscX, zap::kDiscY, zap::kDiscRows);
    zap::TextureCheck check_disc;
    disc.paint(sprites(), gl, check_disc);
    check(disc.painted() && g_uploads.size() == 1 && g_uploads[0].x == 128 &&
              g_uploads[0].y == 1008 && g_uploads[0].w == 24 && g_uploads[0].h == 24,
          "the disc goes to (128, 1008), 24 x 24");
    auto pixel = [&](int px, int py) {
        const uint8_t* p = &g_uploads[0].pixels[(py * 24 + px) * 4];
        return p[0] == 0xFF && p[1] == 0xFF && p[2] == 0xFF && p[3] == 0xFF ? '#'
               : p[3] == 0                                                ? '.'
                                                                          : '?';
    };
    bool same = true;
    for (int py = 0; py < 24; ++py) {
        for (int px = 0; px < 24; ++px) same = same && pixel(px, py) == zap::kDiscRows[py][px];
    }
    check(same, "white where its rows have '#', transparent elsewhere");
    disc.clear(sprites(), gl, check_disc);
    check(!disc.painted() && g_uploads.size() == 2 && g_uploads[1].w == 24 &&
              all_blank({g_uploads.back()}),
          "cleared");

    // Its place not blank in the texture: not painted.
    reset_gl();
    g_disc_place_used = true;
    zap::FontGlyph blocked(zap::kDiscX, zap::kDiscY, zap::kDiscRows);
    zap::TextureCheck taken;
    blocked.paint(sprites(), gl, taken);
    check(!blocked.painted() && g_uploads.empty(), "a place not blank is left alone");
}

// An image in colors over four font glyphs: the file select's AP logo (files.h).
void test_an_image_in_colors() {
    reset_gl();
    zap::GlFunctions gl = fake_gl();
    zap::FontGlyph logo(zap::kApLogoX, zap::kApLogoY, zap::kApLogoRows);
    zap::TextureCheck check_logo;
    logo.paint(sprites(), gl, check_logo);
    check(logo.painted() && g_uploads.size() == 1 && g_uploads[0].x == 96 &&
              g_uploads[0].y == 1040 && g_uploads[0].w == 16 && g_uploads[0].h == 16,
          "the logo goes to glyphs 0x8C, 0x8D, 0x9C and 0x9D: (96, 1040), 16 x 16");
    bool same = true;
    for (int py = 0; py < 16; ++py) {
        for (int px = 0; px < 16; ++px) {
            const uint8_t* p = &g_uploads[0].pixels[(py * 16 + px) * 4];
            uint8_t rgb[3] = {};
            const bool opaque = zap::glyph_rgb(zap::kApLogoRows[py][px], rgb);
            same = same && (opaque ? p[3] == 0xFF && std::memcmp(p, rgb, 3) == 0 : p[3] == 0);
        }
    }
    check(same, "each pixel in its color, opaque; transparent where '.'");
    uint8_t red[3];
    zap::glyph_rgb('r', red);
    check(red[0] == 246 && red[1] == 26 && red[2] == 46,
          "the logo's red as the world's items show it");
    // Through a new game's fade to white: repainted at once, whiter, then its colors again.
    logo.paint(sprites(), gl, check_logo);
    check(g_uploads.size() == 1, "not painted again every frame");
    auto pixel_at = [&](int px, int py) { return &g_uploads.back().pixels[(py * 16 + px) * 4]; };
    int red_x = static_cast<int>(std::strchr(zap::kApLogoRows[1], 'r') - zap::kApLogoRows[1]);
    logo.set_white(128);
    logo.paint(sprites(), gl, check_logo);
    check(g_uploads.size() == 2 && pixel_at(red_x, 1)[0] == 246 + (255 - 246) * 128 / 255 &&
              pixel_at(red_x, 1)[1] == 26 + (255 - 26) * 128 / 255 && pixel_at(red_x, 1)[3] == 0xFF,
          "half way to white, repainted at once");
    check(pixel_at(0, 0)[3] == 0, "transparent stays transparent");
    logo.set_white(255);
    logo.paint(sprites(), gl, check_logo);
    check(g_uploads.size() == 3 && pixel_at(red_x, 1)[0] == 255 && pixel_at(red_x, 1)[1] == 255 &&
              pixel_at(red_x, 1)[2] == 255,
          "all white");
    logo.set_white(0);
    logo.paint(sprites(), gl, check_logo);
    check(g_uploads.size() == 4 && pixel_at(red_x, 1)[0] == 246 && pixel_at(red_x, 1)[1] == 26,
          "its colors again");
    // A repaint that fails (a pixel buffer bound) leaves the image there: still cleared after.
    g_unpack_buffer = 9;
    logo.set_white(64);
    logo.paint(sprites(), gl, check_logo);
    check(g_uploads.size() == 4 && logo.painted(), "a failed repaint: the image kept as painted");
    g_unpack_buffer = 0;
    logo.clear(sprites(), gl, check_logo);
    check(!logo.painted() && g_uploads.size() == 5 && all_blank({g_uploads.back()}), "cleared");
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
    stand_in::g_texture_object[0] = 8;  // the game made the texture again
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
    test_an_image_of_its_own();
    test_an_image_in_colors();
    test_one_readback_for_every_painter();
    test_restyle_and_names();
    test_one_record_not_as_expected_leaves_the_other_restyled();
    test_records_not_as_expected_are_left_alone();
    test_a_record_left_restyled_is_adopted_and_put_back();
    test_a_reload_is_restyled_again();
    test_painting();
    test_label_parser();
    return report("apitem tests");
}
