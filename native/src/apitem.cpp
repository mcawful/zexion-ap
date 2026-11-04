#include "apitem.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "equip.h"  // kDiskRows
#include "game_mem.h"
#include "menu.h"  // kLockRows

namespace zap {

// The popup icon: 8 x 8, white like the game's own icon glyphs: a ring of one-pixel dots, seven
// pixels wide so that, like the font's letters, it leaves its last column empty and does not
// touch the text after it.
const char kApItemRows[8][9] = {
    "...#....", ".#...#..", "........", "#.....#.",
    "........", ".#...#..", "...#....", "........",
};
const char kApLogoRows[16][17] = {
    ".....krrrrk.....", "..kkkrrrrrrkk...", ".kyykkrrrrkkgkk.", "kyyyyykrrkgggggk",
    "yyyyyykrrkgggggg", "yyyyyykrrkgggggg", "yykkkykkkkgkkkkg", "kkbbkk....kpppkk",
    "kbbbbbk..kpppppk", "bbbbbbkkkkpppppp", "bbbbbkooookppppp", "bbbbkooooookpppp",
    "kbbbkooooookpppk", ".kkkkooooookkkk.", "....kooooook....", ".....kooook.....",
};

namespace {

// What Scrap Energy and Scrap Chunk share as the game loads them (B1); the frame is each one's
// own (StandIn).
constexpr RecordShape kOwnShape = {.kind = 34, .ticks = 6, .frames = 2, .icon = 0, .palette = 8,
                                   .inventory = 46, .redirect = 0, .pickup_class = 1};
// What they become: one frame each and no area event at pickup; their names start with the logo
// glyph, their icons stay 0.
constexpr int16_t kLogoFrames = 1, kNoEvent = -1;
constexpr char kDefaultName[] = "\xEE" "AP ITEM";
static_assert(sizeof(kDefaultName) == sizeof(kApItemName) + 1);

// The sprite texture: frame k of the item sprites at (128 + (k % 8) * 16, 640 + (k / 8) * 16),
// glyph c of the 8 x 8 font at ((c % 16) * 8, 976 + (c / 16) * 8); kTextureSize square.
constexpr int frame_x(int k) { return 128 + (k % 8) * 16; }
constexpr int frame_y(int k) { return 640 + (k / 8) * 16; }
constexpr ptrdiff_t kSpritesTexture = 0x40;  // [holder] + 0x40: the texture object
constexpr int kRepaintFrames = 300;
// The progression arrow flashes: the helper repaints it in one color, then the other.
constexpr uint64_t kFlashMs = 300;
constexpr int kArrowPhases = 2;  // the arrow's colors, by phase: yellow, then white

// GL enums (GL 1.1, and the buffer bindings from 2.1).
constexpr unsigned kTexture2D = 0x0DE1, kTextureBinding2D = 0x8069;
constexpr unsigned kTextureWidth = 0x1000, kTextureHeight = 0x1001;
constexpr unsigned kRgba = 0x1908, kUnsignedByte = 0x1401;
constexpr unsigned kUnpackRowLength = 0x0CF2, kUnpackSkipRows = 0x0CF3, kUnpackSkipPixels = 0x0CF4;
constexpr unsigned kUnpackBufferBinding = 0x88EF, kPackBufferBinding = 0x88ED;
constexpr unsigned kPackRowLength = 0x0D02, kPackAlignment = 0x0D05;  // then skip rows, pixels

// The logo for a progression item: kApLogoRows with Archipelago's up arrow over the lower right,
// as in its own icons (Factorio's ap.png in Archipelago); '^' the arrow, which the helper
// flashes itself between yellow and white. Outlined like kApLogoRows, the arrow counted as a
// circle, and the one pixel left of the arrow's tip, shut in by black, black too (the owner's
// pick).
constexpr char kLogoArrow[16][17] = {
    ".....krrrrk.....",
    "..kkkrrrrrrkk...",
    ".kyykkrrrrkkgkk.",
    "kyyyyykrrkgggggk",
    "yyyyyykrrkgggggg",
    "yyyyyykrrkgggggg",
    "yykkkykkkkgkkkkg",
    "kkbbkk....kpppkk",
    "kbbbbbk..kpkkkpk",
    "bbbbbbkkkkkk^kkp",
    "bbbbbkoookk^^^kk",
    "bbbbkoookk^^^^^k",
    "kbbbkoook^^^^^^^",
    ".kkkkoookkk^^^kk",
    "....koooook^^^k.",
    ".....kooook^^^k.",
};
// A logo pixel's color (glyph_rgb's, the arrow's by `arrow_phase`), or false (transparent). The
// stand-ins draw in palette 26, which shows the texture's own colors (ItemLooks::own_colors), so
// the logo is painted in the colors it shows.
bool logo_rgb(char c, int arrow_phase, uint8_t (&rgb)[3]) {
    if (c == '^') return glyph_rgb(arrow_phase == 0 ? 'y' : '#', rgb);
    return glyph_rgb(c, rgb);
}

// The two stand-ins: Scrap Energy for other players' items, Scrap Chunk for their progression
// items. Both are filler the game counts as Scrap Metal; the game draws both in palette 8, the
// helper's look in palette 26 (looks.h).
struct StandIn {
    int item;
    int16_t own_frame, own_event;
    const char* own_name;
    int16_t frame;  // the empty item frame its logo is painted into
    const char (*logo)[17];
};
constexpr StandIn kStandIns[kStandInCount] = {
    {kStandInItem, 134, 3549, kStandInNames[0], kStandInFrame, kApLogoRows},
    {kStandInProgression, 142, 3550, kStandInNames[1], kStandInProgressionFrame, kLogoArrow},
};

bool is_own(const BYTE* r, const StandIn& s) {
    RecordShape own = kOwnShape;
    own.frame = s.own_frame;
    return matches(r, own) && record_named(r, s.own_name) &&
           word(r, record::kEvent) == s.own_event;
}

bool is_ours(const BYTE* r, const StandIn& s) {
    return word(r, record::kKind) == kOwnShape.kind && word(r, record::kFrame) == s.frame &&
           word(r, record::kFrames) == kLogoFrames &&
           r[record::kIcon] == 0 && word(r, record::kEvent) == kNoEvent;
}

}  // namespace

GlFunctions load_gl() {
    GlFunctions gl;
    HMODULE m = GetModuleHandleW(L"opengl32.dll");
    if (m == nullptr) return gl;
    auto get = [m]<typename Fn>(Fn& fn, const char* name) {
        fn = reinterpret_cast<Fn>(GetProcAddress(m, name));
    };
    get(gl.BindTexture, "glBindTexture");
    get(gl.GetIntegerv, "glGetIntegerv");
    get(gl.GetTexLevelParameteriv, "glGetTexLevelParameteriv");
    get(gl.PixelStorei, "glPixelStorei");
    get(gl.TexSubImage2D, "glTexSubImage2D");
    get(gl.GetTexImage, "glGetTexImage");
    get(gl.IsTexture, "glIsTexture");
    get(gl.GetError, "glGetError");
    return gl;
}

int name_cells(const std::vector<StandInSeen>& seen, double player_x, double player_y,
               int player_cell, const std::map<int, std::string>& labels, int (&cells)[2],
               bool* in_reach_out) {
    const bool player_labeled = labels.count(player_cell) != 0;
    int touching = -1;
    double touching_d = 0;
    for (int i = 0; i < kStandInCount; ++i) {
        const int item = kStandInRecords[i];
        double best = 0;
        int best_cell = -1;
        for (const StandInSeen& s : seen) {
            if (s.item != item || labels.count(s.cell) == 0) continue;
            const double dx = s.x - player_x, dy = s.y - player_y;
            const double d = dx * dx + dy * dy;
            if (best_cell < 0 || d < best) {
                best = d;
                best_cell = s.cell;
            }
        }
        const bool in_reach = best_cell >= 0 && best <= kStandInReach * kStandInReach;
        if (in_reach_out != nullptr) in_reach_out[i] = in_reach;
        if (in_reach) {
            cells[i] = best_cell;
            if (touching < 0 || best < touching_d) {
                touching = i;
                touching_d = best;
            }
        } else if (player_labeled || best_cell < 0) {
            cells[i] = player_cell;
        } else {
            cells[i] = best_cell;
        }
    }
    return touching;
}

void ApItems::watch(BYTE* holder, const int (&cells)[2]) {
    if (!active()) return;
    for (int i = 0; i < kStandInCount; ++i) {
        auto it = labels_.find(cells[i]);
        const std::string& name = it != labels_.end() ? it->second : std::string(kDefaultName);
        const StandIn& s = kStandIns[i];
        BYTE copy[record::kSize];
        BYTE* r = writable_record(holder, s.item, copy);
        records_[i] = nullptr;
        if (r == nullptr) continue;
        if (!is_ours(copy, s)) {
            if (!is_own(copy, s)) continue;  // neither the game's own nor ours: leave it alone
            at<int16_t>(r, record::kFrame) = s.frame;
            at<int16_t>(r, record::kFrames) = kLogoFrames;
            at<int16_t>(r, record::kEvent) = kNoEvent;
        }
        // Ours now, or already (an earlier watch, or a helper unloaded without a frame).
        records_[i] = r;
        set_record_name(r, name);
    }
}

namespace {

constexpr int kMaxErrors = 16;  // a lost context can report an error on every call
// The texture check (TextureCheck): how often a texture not as expected is read, how far apart,
// and the rectangle whose alpha is kept, the item frames (x 128 to 256) and the font (x 0 to
// 128, from y 976) together.
constexpr int kReadTries = 3;
constexpr uint64_t kRetryMs = 5000;
constexpr int kKeptX = 0, kKeptY = 640, kKeptWidth = 256, kKeptHeight = 512;
static_assert(kKeptY + kKeptHeight >= 976 + 16 * 8 && frame_y(255) + 16 <= kKeptY + kKeptHeight);

void drain(const GlFunctions& gl) {
    for (int i = 0; i < kMaxErrors && gl.GetError() != 0; ++i) {
    }
}

// Binds `texture` with plain pixel stores, runs `work` if `check` passes there, and puts the
// game's GL state back. False if the texture was not the sprites' as expected, or if pixels
// would go through a buffer.
template <typename Check, typename Work>
bool on_texture(unsigned texture, const GlFunctions& gl, int want_width, int want_height,
                Check&& check, Work&& work) {
    drain(gl);  // the game's own pending errors are not ours to read
    if (!gl.IsTexture(texture)) return false;
    int unpack_buffer = 0, pack_buffer = 0;
    gl.GetIntegerv(kUnpackBufferBinding, &unpack_buffer);
    gl.GetIntegerv(kPackBufferBinding, &pack_buffer);
    if (gl.GetError() != 0) unpack_buffer = pack_buffer = 0;  // GL before 2.1: no such buffers
    if (unpack_buffer != 0 || pack_buffer != 0) return false;  // pixels would go through a buffer
    int bound = 0;
    int unpack[3] = {}, pack[4] = {};
    gl.GetIntegerv(kTextureBinding2D, &bound);
    for (int i = 0; i < 3; ++i) gl.GetIntegerv(kUnpackRowLength + i, &unpack[i]);
    for (int i = 0; i < 4; ++i) gl.GetIntegerv(kPackRowLength + i, &pack[i]);
    gl.BindTexture(kTexture2D, texture);
    int width = 0, height = 0;
    gl.GetTexLevelParameteriv(kTexture2D, 0, kTextureWidth, &width);
    gl.GetTexLevelParameteriv(kTexture2D, 0, kTextureHeight, &height);
    bool ok = width == want_width && height == want_height;
    if (ok) {
        for (int i = 0; i < 3; ++i) gl.PixelStorei(kUnpackRowLength + i, 0);
        for (int i = 0; i < 3; ++i) gl.PixelStorei(kPackRowLength + i, 0);
        gl.PixelStorei(kPackAlignment, 4);
        ok = check();
    }
    if (ok) work();
    for (int i = 0; i < 3; ++i) gl.PixelStorei(kUnpackRowLength + i, unpack[i]);
    for (int i = 0; i < 4; ++i) gl.PixelStorei(kPackRowLength + i, pack[i]);
    gl.BindTexture(kTexture2D, static_cast<unsigned>(bound));
    drain(gl);  // leave no error of ours for the game to find
    return ok;
}

// The sprite texture's: kTextureSize square.
template <typename Check, typename Work>
bool on_texture(unsigned texture, const GlFunctions& gl, Check&& check, Work&& work) {
    return on_texture(texture, gl, kTextureSize, kTextureSize, check, work);
}

// The texture behind the sprite data's holder, or 0.
unsigned sprite_texture(BYTE* holder) {
    unsigned texture = 0;
    read(pointer_at(pointer_at(holder), kSpritesTexture), 0, texture);
    return texture;
}

// A logo's frame, `rgba` 16 x 16 (blank when clearing).
void logo_pixels(const StandIn& s, int arrow_phase, bool clear, uint8_t (&rgba)[16][16][4]) {
    std::memset(rgba, 0, sizeof(rgba));
    for (int y = 0; y < 16 && !clear; ++y) {
        for (int x = 0; x < 16; ++x) {
            uint8_t rgb[3];
            if (!logo_rgb(s.logo[y][x], arrow_phase, rgb)) continue;
            std::memcpy(rgba[y][x], rgb, 3);
            rgba[y][x][3] = 0xFF;
        }
    }
}

}  // namespace

bool TextureCheck::kept(int x, int y, int w, int h) {
    return x >= kKeptX && y >= kKeptY && x + w <= kKeptX + kKeptWidth &&
           y + h <= kKeptY + kKeptHeight;
}

uint8_t TextureCheck::alpha(int x, int y) const {
    return alpha_[static_cast<size_t>(y - kKeptY) * kKeptWidth + (x - kKeptX)];
}

bool TextureCheck::ready(unsigned texture, const GlFunctions& gl) {
    if (texture != texture_) {
        texture_ = texture;
        tries_ = 0;
        read(gl);
    } else if (!good_ && tries_ < kReadTries && GetTickCount64() - tried_ms_ >= kRetryMs) {
        read(gl);
    }
    return good_;
}

void TextureCheck::read(const GlFunctions& gl) {
    ++tries_;
    tried_ms_ = GetTickCount64();
    good_ = false;
    alpha_.clear();
    std::vector<uint8_t> image(static_cast<size_t>(kTextureSize) * kTextureSize * 4);
    gl.GetTexImage(kTexture2D, 0, kRgba, kUnsignedByte, image.data());
    if (gl.GetError() != 0) return;
    auto alpha_at = [&](int x, int y) {
        return image[(static_cast<size_t>(y) * kTextureSize + x) * 4 + 3];
    };
    // Known art, from Sprites.png: Scrap Energy's frame 134 is opaque at (6, 0), and the font's
    // glyph 0xC0 at (2, 1); a flipped or other image would not be.
    constexpr int kIconX = font_x(0xC0), kIconY = font_y(0xC0);
    if (alpha_at(frame_x(134) + 6, frame_y(134)) != 0xFF ||
        alpha_at(kIconX + 2, kIconY + 1) != 0xFF) {
        return;
    }
    alpha_.resize(static_cast<size_t>(kKeptWidth) * kKeptHeight);
    for (int y = 0; y < kKeptHeight; ++y) {
        for (int x = 0; x < kKeptWidth; ++x) {
            alpha_[static_cast<size_t>(y) * kKeptWidth + x] = alpha_at(kKeptX + x, kKeptY + y);
        }
    }
    good_ = true;
}

bool FontGlyph::fits(TextureCheck& check, unsigned texture, const GlFunctions& gl) const {
    // The place blank in the game's art, or already this image.
    return check.place_ok(texture, gl, x_, y_, w_, h_,
                          [&](int px, int py) { return opaque(px, py); });
}

bool glyph_rgb(char c, uint8_t (&rgb)[3]) {
    // The logo's colors, and its outline: Kresezoa's colors for palette 8's entries 1, 2, 3, 5, 8
    // and 9 (the Palettes file; docs/re/memory.md, "Other players' items"), which the world's AP
    // items showed before they drew in palette 26.
    struct Color {
        char c;
        uint8_t rgb[3];
    };
    static constexpr Color kColors[] = {
        {'#', {255, 255, 255}}, {'k', {0, 0, 0}},     {'g', {22, 113, 41}},
        {'b', {53, 181, 255}},  {'r', {246, 26, 46}}, {'p', {249, 47, 237}},
        {'o', {212, 82, 19}},   {'y', {251, 213, 127}},
    };
    for (const Color& k : kColors) {
        if (k.c == c) {
            std::memcpy(rgb, k.rgb, sizeof(rgb));
            return true;
        }
    }
    return false;
}

void FontGlyph::upload_pixels(const GlFunctions& gl, bool clear) const {
    std::vector<uint8_t> rgba(static_cast<size_t>(w_) * h_ * 4, 0);
    for (int py = 0; py < h_ && !clear; ++py) {
        for (int px = 0; px < w_; ++px) {
            uint8_t rgb[3];
            if (!glyph_rgb(pixel(px, py), rgb)) continue;
            uint8_t* out = &rgba[(static_cast<size_t>(py) * w_ + px) * 4];
            for (int c = 0; c < 3; ++c) {
                out[c] = static_cast<uint8_t>(rgb[c] + (255 - rgb[c]) * white_ / 255);
            }
            out[3] = 0xFF;
        }
    }
    gl.TexSubImage2D(kTexture2D, 0, x_, y_, w_, h_, kRgba, kUnsignedByte, rgba.data());
}

bool FontGlyph::upload(unsigned texture, const GlFunctions& gl, TextureCheck& check, bool clear) {
    return on_texture(
        texture, gl, [&] { return fits(check, texture, gl); },
        [&] { upload_pixels(gl, clear); });
}

void FontGlyph::paint(BYTE* holder, const GlFunctions& gl, TextureCheck& check) {
    if (!gl.loaded()) return;
    unsigned texture = sprite_texture(holder);
    if (texture == 0) return;
    // Painted (or found not as expected) at most once every kRepaintFrames frames for the same
    // texture.
    if ((texture == painted_ || texture == failed_) && white_ == painted_white_ &&
        frames_to_paint_-- > 0) {
        return;
    }
    frames_to_paint_ = kRepaintFrames;
    bool ok = upload(texture, gl, check, false);
    if (ok) {
        painted_ = texture;
        painted_white_ = white_;
        failed_ = 0;
    } else {
        // A repaint that failed leaves the earlier image in that texture, still to be cleared.
        if (texture != painted_) painted_ = 0;
        failed_ = texture;
    }
}

void FontGlyph::clear(BYTE* holder, const GlFunctions& gl, TextureCheck& check) {
    if (painted_ != 0 && gl.loaded() && sprite_texture(holder) == painted_) {
        upload(painted_, gl, check, true);
    }
    painted_ = 0;
    failed_ = 0;
    frames_to_paint_ = 0;
}

bool ApItems::upload(unsigned texture, const GlFunctions& gl, TextureCheck& check, bool clear,
                     bool arrow_only) {
    auto fits = [&] {
        for (const StandIn& s : kStandIns) {
            if (!check.place_ok(texture, gl, frame_x(s.frame), frame_y(s.frame), 16, 16,
                                [&](int px, int py) {
                                    uint8_t rgb[3];
                                    return logo_rgb(s.logo[py][px], 0, rgb);
                                })) {
                return false;
            }
        }
        return glyph_.fits(check, texture, gl);
    };
    return on_texture(texture, gl, fits, [&] {
        for (const StandIn& s : kStandIns) {
            if (arrow_only && s.item != kStandInProgression) continue;
            uint8_t frame[16][16][4];
            logo_pixels(s, arrow_phase_, clear, frame);
            gl.TexSubImage2D(kTexture2D, 0, frame_x(s.frame), frame_y(s.frame), 16, 16, kRgba,
                             kUnsignedByte, frame);
        }
        if (!arrow_only) glyph_.upload_pixels(gl, clear);
    });
}

void ApItems::paint(BYTE* holder, const GlFunctions& gl, TextureCheck& check, uint64_t now_ms,
                    bool world) {
    if (!active() || !gl.loaded()) return;
    if (!world) {
        // Menus draw item frames too (the equipment screen draws frame 95, empty in the game's
        // art, by the missile count): the logos come off while one is open, and go back on the
        // first frame after.
        clear(holder, gl, check);
        return;
    }
    int phase = static_cast<int>((now_ms / kFlashMs) % kArrowPhases);
    if (frames_to_paint_-- <= 0) {
        frames_to_paint_ = kRepaintFrames;
        arrow_phase_ = phase;
        unsigned texture = sprite_texture(holder);
        if (texture != 0 && upload(texture, gl, check, false, false)) painted_ = texture;
        return;
    }
    // Between full paints, only the arrow, when its color changes, into a texture already
    // checked and painted.
    if (phase == arrow_phase_ || painted_ == 0 || sprite_texture(holder) != painted_) return;
    arrow_phase_ = phase;
    upload(painted_, gl, check, false, true);
}

void ApItems::restore(BYTE* records_holder, BYTE* texture_holder, const GlFunctions& gl,
                      TextureCheck& check) {
    release(records_holder);
    clear(texture_holder, gl, check);
    labels_.clear();
}

void ApItems::clear(BYTE* holder, const GlFunctions& gl, TextureCheck& check) {
    if (painted_ != 0 && gl.loaded() && sprite_texture(holder) == painted_) {
        upload(painted_, gl, check, true, false);
    }
    painted_ = 0;
    frames_to_paint_ = 0;
}

void ApItems::release(BYTE* records_holder) {
    for (int i = 0; i < kStandInCount; ++i) {
        const StandIn& s = kStandIns[i];
        BYTE copy[record::kSize];
        BYTE* r = writable_record(records_holder, s.item, copy);
        if (r != nullptr && is_ours(copy, s)) {
            at<int16_t>(r, record::kFrame) = s.own_frame;
            at<int16_t>(r, record::kFrames) = kOwnShape.frames;
            set_record_name(r, s.own_name);
            at<int16_t>(r, record::kEvent) = s.own_event;
        }
        records_[i] = nullptr;
    }
}

namespace {

// The icon sheets (IconSheets): the frames the helper paints, and a frame of the game's art that
// is never blank (the first item icon's).
constexpr int kFirstIconFrame = 0x96, kKnownFrame = 0x80;
constexpr int32_t kSheetFrames = 256;
// A definition's template keeps its texture's GL name where a sprite keeps its style (0x1DF2D5 to
// 0x1DF2DC: [the texture object] copied to +0x1D0).
constexpr ptrdiff_t kTemplateTexture = sprite::kStyle;

// The texture of definition `definition`'s sheet, or 0 (no template, or not a sheet of 256).
unsigned sheet_texture(BYTE* holder, int definition) {
    BYTE* t = icon_sheet(holder, definition);
    unsigned texture = 0;
    return t != nullptr && read(t, kTemplateTexture, texture) ? texture : 0;
}

}  // namespace

BYTE* icon_sheet(BYTE* holder, int definition) {
    BYTE* t = pointer_at(pointer_at(holder), definition * static_cast<ptrdiff_t>(8));
    int32_t frames = 0;
    return read(t, sprite::kFrameCount, frames) && frames == kSheetFrames ? t : nullptr;
}

namespace {

bool in_circle(double x, double y, double cx, double cy, double r) {
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

// Where the HD padlock sits in its 64 x 64 cell (left, top, right, bottom): the text it follows
// spans y 6 to 46 there, from the cap height to the baseline (measured on the retest's shot of
// ASSIST in English(HD), 2026-11-02: the cell drawn 1.74 units a screen pixel at 3x).
constexpr double kLockBox[4] = {16, 6, 50, 46};
// Where the disk and the AP logo sit, which lead their text: the game's own HD icons' box (their
// boxes' medians are x 9 to 53, y 7 to 50 in BigFontIcons.png), from the cap height to a
// little under the baseline, the owner's picks (D2, L1 of the 2026-11-02 mockups).
constexpr double kLeadBox[4] = {10, 6, 54, 50};

// (x, y) in `box` as the point of a shape drawn in `drawn` (both left, top, right, bottom), the
// same scale both ways.
void fit(double& x, double& y, const double (&box)[4], const double (&drawn)[4]) {
    const double s = (box[3] - box[1]) / (drawn[3] - drawn[1]);
    x = drawn[0] + (x - box[0]) / s;
    y = drawn[1] + (y - box[1]) / s;
}

// The HD icons' shapes in a 64 x 64 cell, each drawn in its own box and fitted into its place.
bool hd_shape(int k, double x, double y) {
    switch (k) {
        case 0: {  // a solid disc: its hub hole, a thin groove and two shine cuts, drawn in
                   // x 14 to 54, y 6 to 46
            fit(x, y, kLeadBox, {14, 6, 54, 46});
            const double d = std::hypot(x - 34, y - 26);
            if (d > 20 || d < 3.5 || (d >= 6.5 && d <= 8)) return false;
            // The angle counterclockwise from the right, in degrees (y runs down).
            double a = std::atan2(-(y - 26), x - 34) * 180.0 / 3.14159265358979;
            if (a < 0) a += 360;
            const bool shine = d >= 11.5 && d <= 17.5 && ((a >= 40 && a <= 52) ||
                                                           (a >= 220 && a <= 232));
            return !shine;
        }
        case 1: {  // a padlock: its shackle, its body, the keyhole out of it
            // Drawn in x 12 to 52, y 11 to 58 below, and fitted into kLockBox: from the font's
            // cap height to its baseline, 16 units (about 3 pixels of the game's 320) from its
            // left as its gap (ASSIST's padlock has no space before it in English(HD)).
            fit(x, y, kLockBox, {12, 11, 52, 58});
            const double d = std::hypot(x - 32, y - 26);
            if (y <= 30 && d >= 9 && d <= 15) return true;
            const bool body = x >= 12 && x <= 52 && y >= 28 && y <= 58 &&
                              !(x < 17 && y > 53 && !in_circle(x, y, 17, 53, 5)) &&
                              !(x > 47 && y > 53 && !in_circle(x, y, 47, 53, 5));
            const bool hole = in_circle(x, y, 32, 40, 5) ||
                              (x >= 29.5 && x <= 34.5 && y >= 40 && y <= 50);
            return body && !hole;
        }
        default: {  // the logo: six dots on a ring, as the pixel glyph's (kApItemRows), drawn
                    // in x 3 to 61, y 3 to 61
            fit(x, y, kLeadBox, {3, 3, 61, 61});
            for (int i = 0; i < 6; ++i) {
                const double a = (90.0 + 60.0 * i) * 3.14159265358979 / 180.0;
                if (in_circle(x, y, 32 + 21 * std::cos(a), 32 - 21 * std::sin(a), 8)) return true;
            }
            return false;
        }
    }
}

}  // namespace

std::vector<uint8_t> IconSheets::art(int k, int w, int h) {
    ++arts_made_;
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4, 0);
    auto put_alpha = [&](int x, int y, int alpha) {
        if (alpha <= 0) return;
        uint8_t* p = &out[(static_cast<size_t>(y) * w + x) * 4];
        p[0] = p[1] = p[2] = 0xFF;
        p[3] = static_cast<uint8_t>(alpha > 255 ? 255 : alpha);
    };
    if (w >= 32) {
        // HD: each pixel's coverage of the shape, scaled to the cell, from 4 x 4 samples.
        constexpr int kSamples = 4;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int inside = 0;
                for (int j = 0; j < kSamples; ++j) {
                    for (int i = 0; i < kSamples; ++i) {
                        const double sx = (x + (i + 0.5) / kSamples) * 64.0 / w;
                        const double sy = (y + (j + 0.5) / kSamples) * 64.0 / h;
                        inside += hd_shape(k, sx, sy) ? 1 : 0;
                    }
                }
                put_alpha(x, y, (inside * 255 + kSamples * kSamples / 2) / (kSamples * kSamples));
            }
        }
        return out;
    }
    // Pixel icons: the 8 x 8 glyph's rows, centered across the cell, from its top.
    const char (*rows)[9] = k == 0 ? kDiskRows : k == 1 ? kLockRows : kApItemRows;
    const int ox = (w - 8) / 2;
    for (int y = 0; y < 8 && y < h; ++y) {
        for (int x = 0; x < 8 && ox + x < w; ++x) {
            if (rows[y][x] == '#') put_alpha(ox + x, y, 255);
        }
    }
    return out;
}

const std::vector<uint8_t>& IconSheets::icon(Sheet& s, int k) {
    if (s.icons[k].empty()) s.icons[k] = art(k, s.cell_w, s.cell_h);
    return s.icons[k];
}

bool IconSheets::upload(Sheet& s, unsigned texture, const GlFunctions& gl, bool clear) {
    auto cell_x = [&](int frame) { return (frame % 16) * s.cell_w; };
    auto cell_y = [&](int frame) { return (frame / 16) * s.cell_h; };
    auto check = [&] {
        if (s.checked == texture) return true;  // read once a texture: blank or ours since
        std::vector<uint8_t> image(static_cast<size_t>(s.width) * s.height * 4);
        gl.GetTexImage(kTexture2D, 0, kRgba, kUnsignedByte, image.data());
        if (gl.GetError() != 0) return false;
        auto alpha = [&](int x, int y) {
            return image[(static_cast<size_t>(y) * s.width + x) * 4 + 3];
        };
        // The game's art where the PNG has it: the first item icon's cell is not blank.
        bool art_there = false;
        for (int y = 0; y < s.cell_h && !art_there; ++y) {
            for (int x = 0; x < s.cell_w && !art_there; ++x) {
                art_there = alpha(cell_x(kKnownFrame) + x, cell_y(kKnownFrame) + y) != 0;
            }
        }
        if (!art_there) return false;
        // Each icon's cell blank, or already the helper's icon.
        for (int k = 0; k < kHdIconCount; ++k) {
            const std::vector<uint8_t>& ours = icon(s, k);
            for (int y = 0; y < s.cell_h; ++y) {
                for (int x = 0; x < s.cell_w; ++x) {
                    const uint8_t a = alpha(cell_x(kFirstIconFrame + k) + x,
                                            cell_y(kFirstIconFrame + k) + y);
                    if (a != 0 && a != ours[(static_cast<size_t>(y) * s.cell_w + x) * 4 + 3]) {
                        return false;
                    }
                }
            }
        }
        s.checked = texture;
        return true;
    };
    auto work = [&] {
        for (int k = 0; k < kHdIconCount; ++k) {
            const size_t cell = static_cast<size_t>(s.cell_w) * s.cell_h * 4;
            const std::vector<uint8_t> blank(clear ? cell : 0);
            const std::vector<uint8_t>& pixels = clear ? blank : icon(s, k);
            gl.TexSubImage2D(kTexture2D, 0, cell_x(kFirstIconFrame + k),
                             cell_y(kFirstIconFrame + k), s.cell_w, s.cell_h, kRgba,
                             kUnsignedByte, pixels.data());
        }
    };
    return on_texture(texture, gl, s.width, s.height, check, work);
}

void IconSheets::paint(BYTE* holder, const GlFunctions& gl) {
    if (!gl.loaded()) return;
    for (Sheet& s : sheets_) {
        const unsigned texture = sheet_texture(holder, s.definition);
        if (texture == 0) continue;
        // Painted (or found not as expected) at most once every kRepaintFrames frames; a
        // texture not as traced read only kReadTries times (each read is the whole sheet).
        if (texture == s.failed && s.tries >= kReadTries) continue;
        if ((texture == s.painted || texture == s.failed) && s.frames_to_paint-- > 0) continue;
        s.frames_to_paint = kRepaintFrames;
        if (upload(s, texture, gl, false)) {
            s.painted = texture;
            s.failed = 0;
            s.tries = 0;
        } else {
            if (texture != s.painted) s.painted = 0;
            s.tries = texture == s.failed ? s.tries + 1 : 1;
            s.failed = texture;
        }
    }
}

void IconSheets::clear(BYTE* holder, const GlFunctions& gl) {
    for (Sheet& s : sheets_) {
        if (s.painted != 0 && gl.loaded() && sheet_texture(holder, s.definition) == s.painted) {
            upload(s, s.painted, gl, true);
        }
        s.painted = s.failed = s.checked = 0;
        s.frames_to_paint = s.tries = 0;
    }
}

}  // namespace zap
