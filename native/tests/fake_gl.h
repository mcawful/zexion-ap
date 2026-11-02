// A fake OpenGL for the host tests that paint into the sprite texture (apitem.h GlFunctions):
// every upload kept in g_uploads, the game's binding and unpack state put back checked, and a
// readback of the known art the texture check looks for.
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "apitem.h"
#include "stand_in.h"

namespace gl_fake {

struct Upload {
    int x, y, w, h;
    std::vector<uint8_t> pixels;
};
inline unsigned g_bound = 3;  // the game's own binding, to be put back
inline int g_width = 2048, g_unpack_buffer = 0, g_row_length = 5;
inline std::vector<Upload> g_uploads;
inline std::vector<unsigned> g_binds;

inline void WINAPI fake_bind(unsigned, unsigned texture) {
    g_binds.push_back(texture);
    g_bound = texture;
}
inline void WINAPI fake_get_integer(unsigned name, int* value) {
    if (name == 0x8069) *value = static_cast<int>(g_bound);
    if (name == 0x88EF) *value = g_unpack_buffer;
    if (name == 0x0CF2) *value = g_row_length;
    if (name == 0x0CF3 || name == 0x0CF4) *value = 0;
}
// The icon sheets' textures (apitem.h IconSheets): 11 the HD one, 12 the pixel one.
constexpr unsigned kHdSheet = 11, kPixelSheet = 12;
inline int sheet_width(unsigned t) { return t == kHdSheet ? 1024 : t == kPixelSheet ? 160 : 0; }
inline int sheet_height(unsigned t) { return t == kHdSheet ? 1024 : t == kPixelSheet ? 144 : 0; }
inline void WINAPI fake_level_parameter(unsigned, int, unsigned name, int* value) {
    if (sheet_width(g_bound) != 0) {
        *value = name == 0x1000 ? sheet_width(g_bound) : sheet_height(g_bound);
        return;
    }
    *value = g_width;
}
inline void WINAPI fake_pixel_store(unsigned name, int value) {
    if (name == 0x0CF2) g_row_length = value;
}
inline void WINAPI fake_sub_image(unsigned, int, int x, int y, int w, int h, unsigned, unsigned,
                           const void* pixels) {
    auto* p = static_cast<const uint8_t*>(pixels);
    g_uploads.push_back({x, y, w, h, std::vector<uint8_t>(p, p + w * h * 4)});
}
inline unsigned WINAPI fake_get_error() { return 0; }
// The readback: the known art apitem.cpp checks (Scrap Energy's frame 134 at (6, 0), glyph 0xC0
// at (2, 1)) opaque unless g_known_art is off; the logo places blank unless g_frame95_used.
inline bool g_frame95_used = false;
inline bool g_disc_place_used = false;  // a pixel of the pause disc's place, where its art is clear
inline bool g_known_art = true;
inline int g_readbacks = 0;
// A sheet's cell for the helper's first icon used by the game's art (frame 0x96).
inline bool g_icon_cell_used = false;
inline void WINAPI fake_get_tex_image(unsigned, int, unsigned, unsigned, void* pixels) {
    ++g_readbacks;
    auto* p = static_cast<uint8_t*>(pixels);
    if (const int w = sheet_width(g_bound); w != 0) {
        const int h = sheet_height(g_bound), cw = w / 16, ch = h / 16;
        std::memset(p, 0, static_cast<size_t>(w) * h * 4);
        auto opaque = [&](int x, int y) { p[(static_cast<size_t>(y) * w + x) * 4 + 3] = 0xFF; };
        if (g_known_art) opaque((0x80 % 16) * cw + 2, (0x80 / 16) * ch + 2);  // an item icon
        if (g_icon_cell_used) opaque((0x96 % 16) * cw, (0x96 / 16) * ch);
        return;
    }
    std::memset(p, 0, static_cast<size_t>(2048) * 2048 * 4);
    if (!g_known_art) return;
    auto opaque = [&](int x, int y) { p[(static_cast<size_t>(y) * 2048 + x) * 4 + 3] = 0xFF; };
    opaque(128 + (134 % 8) * 16 + 6, 640 + (134 / 8) * 16);
    opaque((0xC0 % 16) * 8 + 2, 976 + (0xC0 / 16) * 8 + 1);
    if (g_frame95_used) opaque(128 + (95 % 8) * 16, 640 + (95 / 8) * 16);  // where the logo is not
    if (g_disc_place_used) opaque(128, 1008);  // the disc's top left, transparent in its rows
}
inline unsigned char WINAPI fake_is_texture(unsigned texture) { return texture != 0; }

inline zap::GlFunctions fake_gl() {
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

// Whether every upload is all zeros (pixels cleared).
inline bool all_blank(const std::vector<Upload>& uploads) {
    for (const Upload& u : uploads) {
        for (uint8_t b : u.pixels) {
            if (b != 0) return false;
        }
    }
    return true;
}

inline void reset_gl() {
    stand_in::reset_sprites();
    g_bound = 3;
    g_width = 2048;
    g_unpack_buffer = 0;
    g_row_length = 5;
    g_uploads.clear();
    g_binds.clear();
    g_known_art = true;
    g_frame95_used = false;
    g_disc_place_used = false;
    g_icon_cell_used = false;
    g_readbacks = 0;
}

}  // namespace gl_fake
