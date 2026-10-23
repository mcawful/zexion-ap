#include "apitem.h"

#include <cstring>

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
constexpr unsigned kUnpackBufferBinding = 0x88EF;

// The logo: 16 x 16, levels 0 to 3 (the shader picks a palette shade from the red channel,
// as the game's grey sprites are drawn: 0x00, 0x67, 0xCC, 0xFF), -1 transparent.
constexpr int8_t kLogo[16][16] = {
    {-1, -1, -1, -1, -1, -1, -1, 0, 0, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, 0, 3, 3, 0, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, 0, 3, 3, 2, 1, 0, -1, -1, -1, -1, -1},
    {-1, -1, 0, 0, 0, 0, 3, 2, 1, 1, 0, 0, 0, 0, -1, -1},
    {-1, 0, 3, 3, 2, 0, 0, 1, 1, 0, 0, 3, 3, 2, 0, -1},
    {-1, 0, 3, 2, 2, 1, 0, 0, 0, 0, 3, 3, 2, 1, 0, -1},
    {-1, 0, 2, 2, 1, 0, 0, 3, 3, 0, 0, 2, 1, 1, 0, -1},
    {-1, -1, 0, 0, 0, 0, 3, 3, 2, 1, 0, 0, 0, 0, -1, -1},
    {-1, -1, 0, 0, 0, 0, 3, 2, 1, 1, 0, 0, 0, 0, -1, -1},
    {-1, 0, 3, 3, 2, 0, 0, 1, 1, 0, 0, 3, 3, 2, 0, -1},
    {-1, 0, 3, 2, 2, 1, 0, 0, 0, 0, 3, 3, 2, 1, 0, -1},
    {-1, 0, 2, 2, 1, 0, 0, 3, 3, 0, 0, 2, 1, 1, 0, -1},
    {-1, -1, 0, 0, 0, 0, 3, 3, 2, 1, 0, 0, 0, 0, -1, -1},
    {-1, -1, -1, -1, -1, 0, 3, 2, 1, 1, 0, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, 0, 1, 1, 0, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, 0, 0, -1, -1, -1, -1, -1, -1, -1},
};
// The popup icon: 8 x 8, white like the game's own icon glyphs.
constexpr char kGlyph[8][9] = {
    "...##...", ".#....#.", "#..##..#", "#.#..#.#",
    "#.#..#.#", "#..##..#", ".#....#.", "...##...",
};
constexpr uint8_t kShade[4] = {0x00, 0x67, 0xCC, 0xFF};

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
    if (r != record_ || !is_ours(r)) {
        if (!is_own(r)) {
            record_ = nullptr;  // neither the game's own nor ours: leave it alone
            return;
        }
        std::memcpy(saved_, r, sizeof(saved_));
        std::memcpy(&saved_event_, r + kEvent, 2);
        at<int16_t>(r, kFrame) = kLogoFrame;
        at<int16_t>(r, kFrames) = kLogoFrames;
        r[kIcon] = kLogoGlyph;
        at<int16_t>(r, kEvent) = kNoEvent;
        record_ = r;
    }
    auto it = labels_.find(cell);
    const std::string& name = it != labels_.end() ? it->second : std::string(kDefaultName);
    char want[kItemNameMax + 1] = {};
    std::memcpy(want, name.data(), (std::min)(name.size(), kItemNameMax));
    if (std::memcmp(r + kName, want, sizeof(want)) != 0) {
        std::memcpy(r + kName, want, sizeof(want));
    }
}

bool ApItems::upload(unsigned texture, const GlFunctions& gl, bool clear) {
    int bound = 0, unpack_buffer = 0, row_length = 0, skip_rows = 0, skip_pixels = 0;
    gl.GetIntegerv(kUnpackBufferBinding, &unpack_buffer);
    if (gl.GetError() != 0) unpack_buffer = 0;  // GL before 2.1: no unpack buffers
    if (unpack_buffer != 0) return false;        // pixels would come from a buffer: not ours
    gl.GetIntegerv(kTextureBinding2D, &bound);
    gl.GetIntegerv(kUnpackRowLength, &row_length);
    gl.GetIntegerv(kUnpackSkipRows, &skip_rows);
    gl.GetIntegerv(kUnpackSkipPixels, &skip_pixels);
    gl.BindTexture(kTexture2D, texture);
    int width = 0, height = 0;
    gl.GetTexLevelParameteriv(kTexture2D, 0, kTextureWidth, &width);
    gl.GetTexLevelParameteriv(kTexture2D, 0, kTextureHeight, &height);
    bool ok = width == kTextureSize && height == kTextureSize;
    if (ok) {
        gl.PixelStorei(kUnpackRowLength, 0);
        gl.PixelStorei(kUnpackSkipRows, 0);
        gl.PixelStorei(kUnpackSkipPixels, 0);
        uint8_t frame[16][16][4] = {};
        uint8_t glyph[8][8][4] = {};
        if (!clear) {
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    if (kLogo[y][x] < 0) continue;
                    uint8_t s = kShade[kLogo[y][x]];
                    frame[y][x][0] = frame[y][x][1] = frame[y][x][2] = s;
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
        gl.PixelStorei(kUnpackRowLength, row_length);
        gl.PixelStorei(kUnpackSkipRows, skip_rows);
        gl.PixelStorei(kUnpackSkipPixels, skip_pixels);
    }
    gl.BindTexture(kTexture2D, static_cast<unsigned>(bound));
    while (gl.GetError() != 0) {
        // Leave no error of ours for the game to find.
    }
    return ok;
}

void ApItems::paint(BYTE* holder, const GlFunctions& gl) {
    if (!active() || !gl.loaded()) return;
    if (frames_to_paint_-- > 0) return;
    frames_to_paint_ = kRepaintFrames;
    BYTE* sprites = nullptr;
    BYTE* object = nullptr;
    unsigned texture = 0;
    if (!safe_read(holder, &sprites, sizeof(sprites)) ||
        !safe_read(sprites + kSpritesTexture, &object, sizeof(object)) ||
        !safe_read(object, &texture, sizeof(texture)) || texture == 0) {
        return;
    }
    if (upload(texture, gl, false)) painted_ = texture;
}

void ApItems::restore(BYTE* records_holder, BYTE* texture_holder, const GlFunctions& gl) {
    if (record_ != nullptr && record(records_holder) == record_ && is_ours(record_)) {
        std::memcpy(record_, saved_, sizeof(saved_));
        at<int16_t>(record_, kEvent) = saved_event_;
    }
    record_ = nullptr;
    if (painted_ != 0 && gl.loaded()) {
        BYTE* sprites = nullptr;
        BYTE* object = nullptr;
        unsigned texture = 0;
        if (safe_read(texture_holder, &sprites, sizeof(sprites)) &&
            safe_read(sprites + kSpritesTexture, &object, sizeof(object)) &&
            safe_read(object, &texture, sizeof(texture)) && texture == painted_) {
            upload(texture, gl, true);
        }
    }
    painted_ = 0;
    labels_.clear();
}

}  // namespace zap
