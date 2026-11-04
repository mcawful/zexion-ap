// How other players' items look (native/README.md, "Other players' items"; docs/re/memory.md,
// "Other players' items"). While labels are set, the stand-in records (47, 48) are restyled and
// the logos painted into the sprite texture, in the frame call. Records are written only over the
// game's own values or the helper's, the texture only where TextureCheck finds the place blank or
// already the helper's. Unload restores the records and clears the pixels.
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zap {

constexpr int kStandInItem = 47;         // Scrap Energy: other players' items
constexpr int kStandInProgression = 48;  // Scrap Chunk: other players' progression items
// The empty item frames their logos are painted into.
constexpr int kStandInFrame = 63, kStandInProgressionFrame = 95;
constexpr int kStandInCount = 2;
// A stand-in's index (0 Scrap Energy, 1 Scrap Chunk) from its record or logo frame, -1 for neither.
constexpr int stand_in_index(int record) {
    return record == kStandInItem ? 0 : record == kStandInProgression ? 1 : -1;
}
constexpr int logo_index(int frame) {
    return frame == kStandInFrame ? 0 : frame == kStandInProgressionFrame ? 1 : -1;
}
constexpr int kStandInRecords[kStandInCount] = {kStandInItem, kStandInProgression};
constexpr int kStandInFrames[kStandInCount] = {kStandInFrame, kStandInProgressionFrame};
// Their own names, as the game loads their records (47's, then 48's).
constexpr const char* kStandInNames[kStandInCount] = {"SCRAP ENERGY", "SCRAP CHUNK"};
constexpr size_t kItemNameMax = 49;  // the record's 50 bytes, NUL-terminated
// The logo glyph, which starts a stand-in's name (the record's own icon stays 0, so the popup
// shows the name as written and it can be centered), and the name where no label is.
constexpr uint8_t kApItemGlyph = 0xEE;
constexpr char kApItemName[] = "AP ITEM";

// Font glyph `code`'s place in the sprite texture: 16 a row of 8 x 8, from (0, 976).
constexpr int font_x(uint8_t code) { return (code % 16) * 8; }
constexpr int font_y(uint8_t code) { return 976 + (code / 16) * 8; }

// The OpenGL 1.1 functions the painting uses, taken from opengl32.dll (or a test's fakes).
struct GlFunctions {
    void(WINAPI* BindTexture)(unsigned target, unsigned texture) = nullptr;
    void(WINAPI* GetIntegerv)(unsigned name, int* value) = nullptr;
    void(WINAPI* GetTexLevelParameteriv)(unsigned target, int level, unsigned name,
                                         int* value) = nullptr;
    void(WINAPI* PixelStorei)(unsigned name, int value) = nullptr;
    void(WINAPI* TexSubImage2D)(unsigned target, int level, int x, int y, int width, int height,
                                unsigned format, unsigned type, const void* pixels) = nullptr;
    unsigned(WINAPI* GetError)() = nullptr;
    void(WINAPI* GetTexImage)(unsigned target, int level, unsigned format, unsigned type,
                              void* pixels) = nullptr;
    unsigned char(WINAPI* IsTexture)(unsigned texture) = nullptr;
    bool loaded() const {
        return BindTexture && GetIntegerv && GetTexLevelParameteriv && PixelStorei &&
               TexSubImage2D && GetError && GetTexImage && IsTexture;
    }
};

// A live stand-in object in the loaded rooms: its record (47 or 48), the map cell its location
// is in (index y * 54 + x), and where it is, in pixels like the player's position.
struct StandInSeen {
    int item = 0;
    int cell = -1;
    double x = 0, y = 0;
};

// How far from the player (pixels, between the positions) an object counts as within reach.
// Chosen, not measured: past touching range, under half a cell (320 x 224).
constexpr double kStandInReach = 96;

// For each stand-in record (47, then 48), the map cell to name it after: its nearest labeled
// object's cell if within kStandInReach; else `player_cell` if labeled; else that object's cell
// wherever it is; else `player_cell`. Returns the index (0 or 1) whose object is nearest within
// reach (the one a pickup is about to take) or -1; and, given `in_reach`, each one's reach.
int name_cells(const std::vector<StandInSeen>& seen, double player_x, double player_y,
               int player_cell, const std::map<int, std::string>& labels, int (&cells)[2],
               bool* in_reach = nullptr);

// From opengl32.dll if the process has loaded it; otherwise all null.
GlFunctions load_gl();

// The sprite texture read back (glGetTexImage, 16 MB), once per texture: whether it holds the
// game's art where Sprites.png has it, and the alpha of the font and the item frames, against
// which each place is checked before the first paint. One unreadable or not as the PNG is read
// up to 3 times, 5 s apart (it may still be loading), then not until the texture changes.
class TextureCheck {
public:
    // Game thread, inside on_texture: whether the w x h place at (x, y) is blank as read, or
    // already the helper's (`ours(px, py)`: where the helper's pixels are opaque).
    template <typename Ours>
    bool place_ok(unsigned texture, const GlFunctions& gl, int x, int y, int w, int h,
                  Ours&& ours) {
        if (!ready(texture, gl) || !kept(x, y, w, h)) return false;
        for (int py = 0; py < h; ++py) {
            for (int px = 0; px < w; ++px) {
                uint8_t a = alpha(x + px, y + py);
                if (a != 0 && a != (ours(px, py) ? 0xFF : 0)) return false;
            }
        }
        return true;
    }

private:
    bool ready(unsigned texture, const GlFunctions& gl);
    void read(const GlFunctions& gl);
    static bool kept(int x, int y, int w, int h);
    uint8_t alpha(int x, int y) const;

    unsigned texture_ = 0;  // the texture last read
    bool good_ = false;     // read, with the game's art where expected
    int tries_ = 0;
    uint64_t tried_ms_ = 0;
    std::vector<uint8_t> alpha_;  // the kept rectangle's alpha, as read
};

// A pixel's color: '#' white, or the logo's (k outline; r y g b p o circles); false otherwise.
bool glyph_rgb(char c, uint8_t (&rgb)[3]);

// An 8 x 8 font glyph painted into a slot empty in the game's art (the logo glyph, the padlock,
// the disk glyphs), or an image in the texture check's kept rectangle (pause.h, files.h). Painted
// while wanted, again now and then and on a texture change; cleared when no longer wanted.
class FontGlyph {
public:
    // `rows`: 8 strings of 8, '#' for an opaque white pixel.
    FontGlyph(uint8_t code, const char (*rows)[9])
        : x_(font_x(code)), y_(font_y(code)), w_(8), h_(8), rows_(rows[0]) {}
    // H rows of W - 1 at (x, y) in the sprite texture, each pixel as glyph_rgb reads it.
    template <size_t H, size_t W>
    FontGlyph(int x, int y, const char (&rows)[H][W])
        : x_(x), y_(y), w_(static_cast<int>(W - 1)), h_(static_cast<int>(H)), rows_(rows[0]) {}
    // Game thread (the GL context current); `texture_holder` holds the sprite data's pointer.
    void paint(BYTE* texture_holder, const GlFunctions& gl, TextureCheck& check);
    void clear(BYTE* texture_holder, const GlFunctions& gl, TextureCheck& check);
    bool painted() const { return painted_ != 0; }
    // How far toward white the image is painted (0 to 255); a change is painted at once (files.h).
    void set_white(int level) { white_ = level < 0 ? 0 : level > 255 ? 255 : level; }

    // Inside on_texture: whether its place may be painted, and the place painted (or cleared).
    bool fits(TextureCheck& check, unsigned texture, const GlFunctions& gl) const;
    void upload_pixels(const GlFunctions& gl, bool clear) const;

private:
    bool upload(unsigned texture, const GlFunctions& gl, TextureCheck& check, bool clear);
    char pixel(int px, int py) const { return rows_[py * (w_ + 1) + px]; }
    bool opaque(int px, int py) const {
        uint8_t rgb[3];
        return glyph_rgb(pixel(px, py), rgb);
    }
    int x_, y_, w_, h_;
    const char* rows_;  // h_ NUL-terminated strings of w_, back to back
    unsigned painted_ = 0;
    unsigned failed_ = 0;  // a texture it could not paint into: tried again only now and then
    int frames_to_paint_ = 0;
    int white_ = 0, painted_white_ = 0;
};

// The logo glyph's ring of dots (kApItemGlyph).
extern const char kApItemRows[8][9];
// The AP logo, 16 x 16: Archipelago's six circles (data/icon.png in Archipelago) in glyph_rgb's
// letters, outlined side-on (native/README.md, "Other players' items"). The file select marks
// AP saves with it too (files.h).
extern const char kApLogoRows[16][17];

// The helper's disk, padlock and AP logo in the translations' fonts, painted into the free frames
// 0x96 to 0x98 of both icon sheets (101, 64 x 64 cells; 104, 10 x 9) and drawn as these
// characters (docs/re/memory.md, "The translations' icons").
constexpr wchar_t kHdDiskIcon = 0xE03A, kHdLockIcon = 0xE03B, kHdLogoIcon = 0xE03C;
constexpr int kHdIconCount = 3;
constexpr int kHdIconSheet = 101, kPixelIconSheet = 104;  // the sheets' definitions
// Definition `definition`'s template (`holder` holds the templates' list) if it is a sheet of
// 256 frames (16 x 16, 0x1DF1C0); else null.
BYTE* icon_sheet(BYTE* holder, int definition);

class IconSheets {
public:
    // Game thread (the GL context current), each frame while wanted: paints the icons into each
    // loaded sheet now and then and on a texture change, only where their cells are blank or the
    // helper's. `templates_holder` holds the pointer to the sprite definitions' templates.
    void paint(BYTE* templates_holder, const GlFunctions& gl);
    // Game thread: the icons cleared from each sheet painted (no longer wanted, or an unload).
    void clear(BYTE* templates_holder, const GlFunctions& gl);
    bool painted() const { return sheets_[0].painted != 0 || sheets_[1].painted != 0; }

    // The icon `k` (0 the disk, 1 the padlock, 2 the logo) as painted into a cell of w x h: white,
    // its alpha the shape's (exposed for the tests).
    static std::vector<uint8_t> art(int k, int w, int h);
    // How many times art has run (the tests: each sheet makes its icons once).
    static int arts_made() { return arts_made_; }

private:
    struct Sheet {
        int definition, width, height, cell_w, cell_h;
        unsigned painted = 0, failed = 0, checked = 0;
        int frames_to_paint = 0;
        int tries = 0;  // reads of `failed` that found it not as traced
        std::vector<uint8_t> icons[kHdIconCount];  // art(k, cell_w, cell_h), made once
    };
    bool upload(Sheet& s, unsigned texture, const GlFunctions& gl, bool clear);
    // The sheet's icon k, made the first time it is wanted (the HD ones take milliseconds).
    static const std::vector<uint8_t>& icon(Sheet& s, int k);
    Sheet sheets_[2] = {{kHdIconSheet, 1024, 1024, 64, 64}, {kPixelIconSheet, 160, 144, 10, 9}};
    inline static int arts_made_ = 0;
};

class ApItems {
public:
    // Game thread. The labels by map cell (index y * 54 + x), already filtered to the font.
    void set_labels(std::map<int, std::string> labels) { labels_ = std::move(labels); }
    const std::map<int, std::string>& labels() const { return labels_; }
    bool active() const { return !labels_.empty(); }

    // Game thread, every frame while active: restyles the records if they read as the game's
    // own (again, if the game reloaded its records), and names each for its cell in `cells`
    // (47's, then 48's: name_cells). `records_holder` holds the pointer to the records.
    void watch(BYTE* records_holder, const int (&cells)[2]);

    // Game thread, every frame while active (the GL context current): paints the logos now and
    // then and on a texture change. `now_ms` (GetTickCount64) sets the progression arrow's color.
    // `world`: false while a menu is open, when the logos are cleared instead.
    void paint(BYTE* texture_holder, const GlFunctions& gl, TextureCheck& check, uint64_t now_ms,
               bool world = true);

    // Game thread: the records back to the game's own values (also ones an earlier helper left
    // restyled), the pixels cleared if this helper painted them, the labels dropped.
    void restore(BYTE* records_holder, BYTE* texture_holder, const GlFunctions& gl,
                 TextureCheck& check);

    // Game thread: the records back to the game's own values, the labels and the logo kept
    // (the next watch restyles them again): for menus, which show the records too.
    void release(BYTE* records_holder);

    bool restyled() const { return records_[0] != nullptr || records_[1] != nullptr; }
    unsigned painted_texture() const { return painted_; }

private:
    // Paints (or clears) the logos and the glyph where the texture check allows. `arrow_only`:
    // just the progression logo's frame, for the arrow's next color.
    bool upload(unsigned texture, const GlFunctions& gl, TextureCheck& check, bool clear,
                bool arrow_only);
    // The logos and the glyph cleared, if painted into the texture there now.
    void clear(BYTE* texture_holder, const GlFunctions& gl, TextureCheck& check);

    std::map<int, std::string> labels_;
    BYTE* records_[kStandInCount] = {};
    FontGlyph glyph_{kApItemGlyph, kApItemRows};
    unsigned painted_ = 0;
    int frames_to_paint_ = 0;
    int arrow_phase_ = 0;
};

}  // namespace zap
