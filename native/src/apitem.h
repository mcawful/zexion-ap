// How other players' items look (docs/re/memory.md, "Other players' items"). The world puts
// Scrap Energy (47) at locations holding another player's item and Scrap Chunk (48) at their
// progression items. While labels are set, the helper restyles both records: one frame each (63
// and 95, transparent in the game's art), no pickup area event (3549, 3550), and a name from the
// label (the logo glyph 0xEE, then e.g. MASTER SWORD FOR ALICE) or AP ITEM. It paints the logos
// into those frames and the glyph into the font through OpenGL, in the frame call. Records are
// written only over the game's own values or the helper's, and the texture only where
// TextureCheck finds the place blank or already the helper's. Unload restores the records and
// clears the pixels.
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zap {

constexpr int kStandInItem = 47;         // Scrap Energy: other players' items
constexpr int kStandInProgression = 48;  // Scrap Chunk: other players' progression items
// The empty item frames their logos are painted into (frames 63 and 95 of the item sprites).
constexpr int kStandInFrame = 63, kStandInProgressionFrame = 95;
constexpr int kStandInCount = 2;
// Each stand-in's index (0 for Scrap Energy, 1 for Scrap Chunk) from its record, or from its
// logo frame; -1 for neither. And the records and logo frames by index.
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

// For each stand-in record (47, then 48), the map cell to name it after: the cell of the nearest
// object of that record with a labeled cell, if within kStandInReach; else `player_cell`, if it
// has a label; else that nearest object's cell, wherever it is; else `player_cell`. Returns the
// record (0 or 1) whose object is nearest within reach, the one a pickup is about to take, or -1.
int name_cells(const std::vector<StandInSeen>& seen, double player_x, double player_y,
               int player_cell, const std::map<int, std::string>& labels, int (&cells)[2]);

// From opengl32.dll if the process has loaded it; otherwise all null.
GlFunctions load_gl();

// The sprite texture as read back (glGetTexImage, 16 MB), once per texture for everything the
// helper paints into it: whether it holds the game's art where Sprites.png has it, and the alpha
// of the font's glyphs and the item frames as they were, against which each place is checked
// before the first paint (FontGlyph, ApItems). A texture that could not be read, or whose art
// is not where the PNG has it (a sprite mod, say), is read up to 3 times, 5 s apart (it may
// still be loading), then not until the texture changes.
class TextureCheck {
public:
    // Game thread, inside on_texture (the texture bound, plain pixel stores). Whether the w x h
    // place at (x, y), in the font or the item frames, is blank in the texture as read, or
    // already the helper's: `ours(px, py)` says where the helper's pixels are opaque.
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

// One glyph of the game's 8 x 8 font, painted by the helper into a slot that is empty in the
// game's art (the AP logo glyph, the padlock, the disk glyphs), or any one-color image of its own
// placed in the texture check's kept rectangle (the pause row's disc, pause.h). Painted while
// wanted, again now and then and whenever the texture changes, and cleared when no longer wanted.
class FontGlyph {
public:
    // `rows`: 8 strings of 8, '#' for an opaque white pixel.
    FontGlyph(uint8_t code, const char (*rows)[9])
        : x_((code % 16) * 8), y_(976 + (code / 16) * 8), w_(8), h_(8), rows_(rows[0]) {}
    // An image of H rows of W - 1 at (x, y) in the sprite texture, '#' for an opaque white pixel.
    template <size_t H, size_t W>
    FontGlyph(int x, int y, const char (&rows)[H][W])
        : x_(x), y_(y), w_(static_cast<int>(W - 1)), h_(static_cast<int>(H)), rows_(rows[0]) {}
    // Game thread (the GL context current). `texture_holder` holds the pointer to the sprite
    // data.
    void paint(BYTE* texture_holder, const GlFunctions& gl, TextureCheck& check);
    void clear(BYTE* texture_holder, const GlFunctions& gl, TextureCheck& check);
    bool painted() const { return painted_ != 0; }

    // Inside on_texture: whether its place may be painted, and the place painted (or cleared).
    bool fits(TextureCheck& check, unsigned texture, const GlFunctions& gl) const;
    void upload_pixels(const GlFunctions& gl, bool clear) const;

private:
    bool upload(unsigned texture, const GlFunctions& gl, TextureCheck& check, bool clear);
    bool opaque(int px, int py) const { return rows_[py * (w_ + 1) + px] == '#'; }
    int x_, y_, w_, h_;
    const char* rows_;  // h_ NUL-terminated strings of w_, back to back
    unsigned painted_ = 0;
    unsigned failed_ = 0;  // a texture it could not paint into: tried again only now and then
    int frames_to_paint_ = 0;
};

// The logo glyph's ring of dots (kApItemGlyph).
extern const char kApItemRows[8][9];

class ApItems {
public:
    // Game thread. The labels by map cell (index y * 54 + x), already filtered to the font.
    void set_labels(std::map<int, std::string> labels);
    const std::map<int, std::string>& labels() const { return labels_; }
    bool active() const { return !labels_.empty(); }

    // Game thread, every frame while active: restyles the records if they read as the game's
    // own (again, if the game reloaded its records), and names each for its cell in `cells`
    // (47's, then 48's: name_cells). `records_holder` holds the pointer to the records.
    void watch(BYTE* records_holder, const int (&cells)[2]);

    // Game thread, every frame while active (from the swap hook, the game's GL context
    // current): paints the logo into the sprite texture now and then, and whenever the
    // texture changes. `texture_holder` holds the pointer to the sprite data.
    // `now_ms` is a millisecond clock (GetTickCount64): the progression arrow's color follows it.
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
    // Paints (or clears) the logos and the glyph, only where the texture check finds the three
    // places blank (or already the helper's) and the game's art where the PNG has it.
    // `arrow_only`: just the progression logo's frame, for the arrow's next color.
    bool upload(unsigned texture, const GlFunctions& gl, TextureCheck& check, bool clear,
                bool arrow_only);

    std::map<int, std::string> labels_;
    BYTE* records_[kStandInCount] = {};
    FontGlyph glyph_{kApItemGlyph, kApItemRows};
    unsigned painted_ = 0;
    int frames_to_paint_ = 0;
    int arrow_phase_ = 0;
};

}  // namespace zap
