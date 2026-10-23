#include "apitem.h"

#include <cstring>
#include <vector>

#include "menu.h"  // safe_read, writable

namespace zap {

namespace {

// Record fields (docs/re/memory.md, "Other players' items").
constexpr ptrdiff_t kRecordSize = 100, kRecordCount = 51;
constexpr ptrdiff_t kKind = 0x00, kFrame = 0x02, kTicks = 0x04, kFrames = 0x06, kIcon = 0x08;
constexpr ptrdiff_t kPalette = 0x09, kName = 0x0A, kInventory = 0x3C, kEvent = 0x40;
constexpr ptrdiff_t kRedirect = 0x54, kPickupClass = 0x58;
// Scrap Energy as the game loads it (B1).
constexpr int16_t kOwnKind = 34, kOwnFrame = 134, kOwnTicks = 6, kOwnFrames = 2;
constexpr uint8_t kOwnPalette = 8;
constexpr int16_t kOwnInventory = 46, kOwnEvent = 3549, kOwnClass = 1;
constexpr char kOwnName[] = "SCRAP ENERGY";
// What it becomes.
constexpr int16_t kLogoFrame = 63, kLogoFrames = 1, kNoEvent = -1;
constexpr uint8_t kLogoGlyph = 0xEE;
constexpr char kDefaultName[] = "ARCHIPELAGO ITEM";

// The sprite texture: frame k of the item sprites at (128 + (k % 8) * 16, 640 + (k / 8) * 16),
// glyph c of the 8 x 8 font at ((c % 16) * 8, 976 + (c / 16) * 8); 2048 x 2048 RGBA.
constexpr int kTextureSize = 2048;
constexpr int kFrameX = 128 + (kLogoFrame % 8) * 16, kFrameY = 640 + (kLogoFrame / 8) * 16;
constexpr int kGlyphX = (kLogoGlyph % 16) * 8, kGlyphY = 976 + (kLogoGlyph / 16) * 8;
constexpr ptrdiff_t kSpritesTexture = 0x40;  // [holder] + 0x40: the texture object
constexpr int kRepaintFrames = 300;

// GL enums (GL 1.1, and the buffer bindings from 2.1).
constexpr unsigned kTexture2D = 0x0DE1, kTextureBinding2D = 0x8069;
constexpr unsigned kTextureWidth = 0x1000, kTextureHeight = 0x1001;
constexpr unsigned kRgba = 0x1908, kUnsignedByte = 0x1401;
constexpr unsigned kUnpackRowLength = 0x0CF2, kUnpackSkipRows = 0x0CF3, kUnpackSkipPixels = 0x0CF4;
constexpr unsigned kUnpackBufferBinding = 0x88EF, kPackBufferBinding = 0x88ED;
constexpr unsigned kPackRowLength = 0x0D02, kPackAlignment = 0x0D05;  // then skip rows, pixels

// The logo: Archipelago's six circles, each cutting a gap into the one drawn before, as in its
// logo (data/icon.png in Archipelago), at 16 x 16, each circle in its own color of Scrap
// Energy's palette. Each value is a palette entry, -1 transparent: items are drawn with
// Files/tileFragment.glfs, which shows entry int(red * 3 + green * 6 + blue * 9) of the item's
// 19-color palette (docs/re/memory.md, "Other players' items").
constexpr int8_t kLogo[16][16] = {
    {-1, -1, -1, -1, -1,  0,  3,  3,  3,  3,  0, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1,  3,  3,  3,  3,  3,  3, -1, -1, -1, -1, -1},
    {-1,  0,  9,  9,  0, -1,  3,  3,  3,  3, -1,  0,  1,  0,  0, -1},
    { 0,  9,  9,  9,  9,  9, -1,  3,  3, -1,  1,  1,  1,  1,  1,  0},
    { 9,  9,  9,  9,  9,  9,  0,  3,  3,  0,  1,  1,  1,  1,  1,  1},
    { 9,  9,  9,  9,  9,  9,  0,  3,  3,  0,  1,  1,  1,  1,  1,  1},
    { 9,  9, -1, -1, -1,  9,  0, -1, -1,  0,  1, -1, -1, -1, -1,  1},
    {-1,  0, 10, 10,  0,  0, -1, -1, -1, -1,  0,  5,  5,  5,  0, -1},
    { 0, 10, 10, 10, 10, 10, -1, -1, -1, -1,  5,  5,  5,  5,  5,  0},
    {10, 10, 10, 10, 10, 10, -1,  0,  0, -1,  5,  5,  5,  5,  5,  5},
    {10, 10, 10, 10, 10,  0,  8,  8,  8,  8,  0,  5,  5,  5,  5,  5},
    {10, 10, 10, 10, -1,  8,  8,  8,  8,  8,  8, -1,  5,  5,  5,  5},
    { 0, 10, 10, 10,  0,  8,  8,  8,  8,  8,  8,  0,  5,  5,  5,  0},
    {-1,  0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  0,  0,  0,  0, -1},
    {-1, -1, -1, -1, -1,  8,  8,  8,  8,  8,  8, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1,  0,  8,  8,  8,  8,  0, -1, -1, -1, -1, -1},
};
// The popup icon: 8 x 8, white like the game's own icon glyphs: Archipelago's ring of six dots,
// one at the top and one at the bottom as in its logo.
constexpr char kGlyph[8][9] = {
    "...##...", "##.##.##", "##....##", "........",
    "........", "##....##", "##.##.##", "...##...",
};
// A color for each palette entry, in the middle of its range of int(r * 3 + g * 6 + b * 9).
constexpr uint8_t kEntryRgb[19][3] = {
    {0, 0, 0},       {128, 0, 0},     {212, 0, 0},     {255, 21, 0},    {255, 64, 0},
    {255, 106, 0},   {255, 149, 0},   {255, 191, 0},   {255, 234, 0},   {255, 255, 14},
    {255, 255, 42},  {255, 255, 71},  {255, 255, 99},  {255, 255, 128}, {255, 255, 156},
    {255, 255, 184}, {255, 255, 212}, {255, 255, 241}, {255, 255, 255},
};

template <typename T>
T& at(BYTE* p, ptrdiff_t offset) {
    return *reinterpret_cast<T*>(p + offset);
}

bool is_own(const BYTE* r) {
    BYTE copy[kRecordSize];
    std::memcpy(copy, r, sizeof(copy));
    int16_t v;
    auto word = [&](ptrdiff_t o) { std::memcpy(&v, copy + o, 2); return v; };
    return word(kKind) == kOwnKind && word(kFrame) == kOwnFrame && word(kTicks) == kOwnTicks &&
           word(kFrames) == kOwnFrames && copy[kIcon] == 0 && copy[kPalette] == kOwnPalette &&
           std::memcmp(copy + kName, kOwnName, sizeof(kOwnName)) == 0 &&
           word(kInventory) == kOwnInventory && word(kEvent) == kOwnEvent &&
           word(kRedirect) == 0 && word(kPickupClass) == kOwnClass;
}

bool is_ours(const BYTE* r) {
    int16_t frame, frames, event, kind;
    std::memcpy(&kind, r + kKind, 2);
    std::memcpy(&frame, r + kFrame, 2);
    std::memcpy(&frames, r + kFrames, 2);
    std::memcpy(&event, r + kEvent, 2);
    return kind == kOwnKind && frame == kLogoFrame && frames == kLogoFrames &&
           r[kIcon] == kLogoGlyph && event == kNoEvent;
}

}  // namespace

GlFunctions load_gl() {
    GlFunctions gl;
    HMODULE m = GetModuleHandleW(L"opengl32.dll");
    if (m == nullptr) return gl;
    gl.BindTexture = reinterpret_cast<decltype(gl.BindTexture)>(GetProcAddress(m, "glBindTexture"));
    gl.GetIntegerv = reinterpret_cast<decltype(gl.GetIntegerv)>(GetProcAddress(m, "glGetIntegerv"));
    gl.GetTexLevelParameteriv = reinterpret_cast<decltype(gl.GetTexLevelParameteriv)>(
        GetProcAddress(m, "glGetTexLevelParameteriv"));
    gl.PixelStorei = reinterpret_cast<decltype(gl.PixelStorei)>(GetProcAddress(m, "glPixelStorei"));
    gl.TexSubImage2D =
        reinterpret_cast<decltype(gl.TexSubImage2D)>(GetProcAddress(m, "glTexSubImage2D"));
    gl.GetTexImage = reinterpret_cast<decltype(gl.GetTexImage)>(GetProcAddress(m, "glGetTexImage"));
    gl.IsTexture = reinterpret_cast<decltype(gl.IsTexture)>(GetProcAddress(m, "glIsTexture"));
    gl.GetError = reinterpret_cast<decltype(gl.GetError)>(GetProcAddress(m, "glGetError"));
    return gl;
}

void ApItems::set_labels(std::map<int, std::string> labels) { labels_ = std::move(labels); }

BYTE* ApItems::record(BYTE* holder) const {
    BYTE* records = nullptr;
    if (!safe_read(holder, &records, sizeof(records)) || records == nullptr) return nullptr;
    BYTE* r = records + kStandInItem * kRecordSize;
    BYTE probe[kRecordSize];
    if (!safe_read(records, probe, 1) || !safe_read(r, probe, sizeof(probe)) ||
        !writable(r, kRecordSize)) {
        return nullptr;
    }
    static_assert(kStandInItem < kRecordCount);
    return r;
}

void ApItems::watch(BYTE* holder, int cell) {
    if (!active()) return;
    BYTE* r = record(holder);
    if (r == nullptr) {
        record_ = nullptr;
        return;
    }
    if (!is_ours(r)) {
        if (!is_own(r)) {
            record_ = nullptr;  // neither the game's own nor ours: leave it alone
            return;
        }
        at<int16_t>(r, kFrame) = kLogoFrame;
        at<int16_t>(r, kFrames) = kLogoFrames;
        r[kIcon] = kLogoGlyph;
        at<int16_t>(r, kEvent) = kNoEvent;
    }
    record_ = r;  // ours now, or already (left by a helper that unloaded without a frame)
    auto it = labels_.find(cell);
    const std::string& name = it != labels_.end() ? it->second : std::string(kDefaultName);
    char want[kItemNameMax + 1] = {};
    std::memcpy(want, name.data(), (std::min)(name.size(), kItemNameMax));
    if (std::memcmp(r + kName, want, sizeof(want)) != 0) {
        std::memcpy(r + kName, want, sizeof(want));
    }
}

namespace {

constexpr int kMaxErrors = 16;  // a lost context can report an error on every call

void drain(const GlFunctions& gl) {
    for (int i = 0; i < kMaxErrors && gl.GetError() != 0; ++i) {
    }
}

// The texture as the PNG was read, the right way up, with the two places to paint blank (or
// already the logo): read back once per texture.
bool texture_as_expected(const GlFunctions& gl) {
    std::vector<uint8_t> image(static_cast<size_t>(kTextureSize) * kTextureSize * 4);
    gl.GetTexImage(kTexture2D, 0, kRgba, kUnsignedByte, image.data());
    if (gl.GetError() != 0) return false;
    auto alpha = [&](int x, int y) { return image[(static_cast<size_t>(y) * kTextureSize + x) * 4 + 3]; };
    // Known art, from Sprites.png: Scrap Energy's frame 134 is opaque at (6, 0), and the font's
    // glyph 0xC0 at (2, 1); a flipped or other image would not be.
    constexpr int kScrapX = 128 + (134 % 8) * 16, kScrapY = 640 + (134 / 8) * 16;
    constexpr int kIconX = (0xC0 % 16) * 8, kIconY = 976 + (0xC0 / 16) * 8;
    if (alpha(kScrapX + 6, kScrapY) != 0xFF || alpha(kIconX + 2, kIconY + 1) != 0xFF) return false;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            uint8_t a = alpha(kFrameX + x, kFrameY + y);
            if (a != 0 && a != (kLogo[y][x] < 0 ? 0 : 0xFF)) return false;
        }
    }
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            uint8_t a = alpha(kGlyphX + x, kGlyphY + y);
            if (a != 0 && a != (kGlyph[y][x] == '#' ? 0xFF : 0)) return false;
        }
    }
    return true;
}

}  // namespace

bool ApItems::upload(unsigned texture, const GlFunctions& gl, bool clear) {
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
        if (texture != verified_) {
            ok = texture_as_expected(gl);
            if (ok) verified_ = texture;
        }
    }
    if (ok) {
        uint8_t frame[16][16][4] = {};
        uint8_t glyph[8][8][4] = {};
        if (!clear) {
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    if (kLogo[y][x] < 0) continue;
                    const uint8_t* rgb = kEntryRgb[kLogo[y][x]];
                    frame[y][x][0] = rgb[0];
                    frame[y][x][1] = rgb[1];
                    frame[y][x][2] = rgb[2];
                    frame[y][x][3] = 0xFF;
                }
            }
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    if (kGlyph[y][x] != '#') continue;
                    glyph[y][x][0] = glyph[y][x][1] = glyph[y][x][2] = glyph[y][x][3] = 0xFF;
                }
            }
        }
        gl.TexSubImage2D(kTexture2D, 0, kFrameX, kFrameY, 16, 16, kRgba, kUnsignedByte, frame);
        gl.TexSubImage2D(kTexture2D, 0, kGlyphX, kGlyphY, 8, 8, kRgba, kUnsignedByte, glyph);
    }
    for (int i = 0; i < 3; ++i) gl.PixelStorei(kUnpackRowLength + i, unpack[i]);
    for (int i = 0; i < 4; ++i) gl.PixelStorei(kPackRowLength + i, pack[i]);
    gl.BindTexture(kTexture2D, static_cast<unsigned>(bound));
    drain(gl);  // leave no error of ours for the game to find
    return ok;
}

void ApItems::paint(BYTE* holder, const GlFunctions& gl) {
    if (!active() || !gl.loaded()) return;
    if (frames_to_paint_-- > 0) return;
    frames_to_paint_ = kRepaintFrames;
    unsigned texture = texture_of(holder);
    if (texture != 0 && upload(texture, gl, false)) painted_ = texture;
}

unsigned ApItems::texture_of(BYTE* holder) const {
    BYTE* sprites = nullptr;
    BYTE* object = nullptr;
    unsigned texture = 0;
    if (!safe_read(holder, &sprites, sizeof(sprites)) ||
        !safe_read(sprites + kSpritesTexture, &object, sizeof(object)) ||
        !safe_read(object, &texture, sizeof(texture))) {
        return 0;
    }
    return texture;
}

void ApItems::restore(BYTE* records_holder, BYTE* texture_holder, const GlFunctions& gl) {
    // The record back to the game's own values, whether this helper restyled it or an earlier
    // one left it so.
    BYTE* r = record(records_holder);
    if (r != nullptr && is_ours(r)) {
        at<int16_t>(r, kFrame) = kOwnFrame;
        at<int16_t>(r, kTicks) = kOwnTicks;
        at<int16_t>(r, kFrames) = kOwnFrames;
        r[kIcon] = 0;
        r[kPalette] = kOwnPalette;
        std::memset(r + kName, 0, kItemNameMax + 1);
        std::memcpy(r + kName, kOwnName, sizeof(kOwnName));
        at<int16_t>(r, kEvent) = kOwnEvent;
    }
    record_ = nullptr;
    if (painted_ != 0 && gl.loaded() && texture_of(texture_holder) == painted_) {
        upload(painted_, gl, true);
    }
    painted_ = 0;
    labels_.clear();
}

}  // namespace zap
