#include "apitem.h"

#include <cstring>
#include <vector>

#include "game_mem.h"

namespace zap {

// The popup icon: 8 x 8, white like the game's own icon glyphs: a ring of one-pixel dots, seven
// pixels wide so that, like the font's letters, it leaves its last column empty and does not
// touch the text after it.
const char kApItemRows[8][9] = {
    "...#....", ".#...#..", "........", "#.....#.",
    "........", ".#...#..", "...#....", "........",
};
const char kApLogoRows[16][17] = {
    ".....krrrrk.....", ".....rrrrrr.....", ".kyyk.rrrr.kgkk.", "kyyyyy.rr.gggggk",
    "yyyyyykrrkgggggg", "yyyyyykrrkgggggg", "yy...yk..kg....g", ".kbbkk....kpppk.",
    "kbbbbb....pppppk", "bbbbbb.kk.pppppp", "bbbbbkooookppppp", "bbbb.oooooo.pppp",
    "kbbbkooooookpppk", ".kkkkooooookkkk.", ".....oooooo.....", ".....kooook.....",
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
constexpr int8_t kArrow = 19;  // in a logo: the arrow, in the color of the moment
constexpr int8_t kArrowColors[2] = {9, 6};  // palette 8's yellow and white

// GL enums (GL 1.1, and the buffer bindings from 2.1).
constexpr unsigned kTexture2D = 0x0DE1, kTextureBinding2D = 0x8069;
constexpr unsigned kTextureWidth = 0x1000, kTextureHeight = 0x1001;
constexpr unsigned kRgba = 0x1908, kUnsignedByte = 0x1401;
constexpr unsigned kUnpackRowLength = 0x0CF2, kUnpackSkipRows = 0x0CF3, kUnpackSkipPixels = 0x0CF4;
constexpr unsigned kUnpackBufferBinding = 0x88EF, kPackBufferBinding = 0x88ED;
constexpr unsigned kPackRowLength = 0x0D02, kPackAlignment = 0x0D05;  // then skip rows, pixels

// The logo for a progression item: kApLogoRows with Archipelago's up arrow over the lower right,
// as in its own icons (Factorio's ap.png in Archipelago); '^' the arrow, which the helper
// flashes itself (palette entries 13 to 15 did not animate when watched).
constexpr char kLogoArrow[16][17] = {
    ".....krrrrk.....",
    ".....rrrrrr.....",
    ".kyyk.rrrr.kgkk.",
    "kyyyyy.rr.gggggk",
    "yyyyyykrrkgggggg",
    "yyyyyykrrkgggggg",
    "yy...yk..kg....g",
    ".kbbkk....kpppk.",
    "kbbbbb....pkkkpk",
    "bbbbbb.kk.kk^kkp",
    "bbbbbkoookk^^^kk",
    "bbbb.oookk^^^^^k",
    "kbbbkoook^^^^^^^",
    ".kkkkoookkk^^^kk",
    ".....oooook^^^k.",
    ".....kooook^^^k.",
};
// A logo pixel as an entry of Scrap Energy's palette (kEntryLetters[i] is entry i's letter), or
// -1 (transparent): items are drawn with Files/tileFragment.glfs, which shows entry int(red * 3 +
// green * 6 + blue * 9) of the item's 19-color palette (docs/re/memory.md, "Other players'
// items").
constexpr char kEntryLetters[] = "kgbr-p--oy";
int8_t logo_entry(char c) {
    if (c == '^') return kArrow;
    const char* at = c == '-' ? nullptr : std::strchr(kEntryLetters, c);
    return at == nullptr ? -1 : static_cast<int8_t>(at - kEntryLetters);
}

// The two stand-ins: Scrap Energy for other players' items, Scrap Chunk for their progression
// items. Both are filler the game counts as Scrap Metal, and both draw in palette 8.
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

// A color for each palette entry, in the middle of its range of int(r * 3 + g * 6 + b * 9).
constexpr uint8_t kEntryRgb[19][3] = {
    {0, 0, 0},       {128, 0, 0},     {212, 0, 0},     {255, 21, 0},    {255, 64, 0},
    {255, 106, 0},   {255, 149, 0},   {255, 191, 0},   {255, 234, 0},   {255, 255, 14},
    {255, 255, 42},  {255, 255, 71},  {255, 255, 99},  {255, 255, 128}, {255, 255, 156},
    {255, 255, 184}, {255, 255, 212}, {255, 255, 241}, {255, 255, 255},
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
               int player_cell, const std::map<int, std::string>& labels, int (&cells)[2]) {
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
bool on_texture(unsigned texture, const GlFunctions& gl, Check&& check, Work&& work) {
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
    bool ok = width == kTextureSize && height == kTextureSize;
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
            int8_t entry = logo_entry(s.logo[y][x]);
            if (entry < 0) continue;
            if (entry == kArrow) entry = kArrowColors[arrow_phase];
            std::memcpy(rgba[y][x], kEntryRgb[entry], 3);
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
    // The logo's colors as the world's AP items show them (palette 8's entries in the logos: 1, 2,
    // 3, 5, 8, 9, from the Palettes file; docs/re/memory.md, "Other players' items"), and its
    // outline.
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
                                [&](int px, int py) { return logo_entry(s.logo[py][px]) >= 0; })) {
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
    int phase = static_cast<int>((now_ms / kFlashMs) % 2);
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

}  // namespace zap
