// How other players' items look in the game (helper phase 4).
//
// A location holding another player's item is given item 47 (Scrap Energy) by the world, or 48
// (Scrap Chunk) for a progression item. While the client has sent the helper its labels, the
// helper restyles both items' records (the `Objects` file, 100 bytes per record behind
// `item_records`; docs/re/memory.md, "Other players' items"): their in-world animation points
// at an item frame that is transparent in the game's own art (63 and 95, one frame each), their
// popup icon at the font's glyph 0xEE (empty in the game's own art), and their pickup no longer
// runs an area event (3549 and 3550, a refill drop). The helper paints Archipelago's logo into
// those frames (with its up arrow for progression) and its ring of dots into the glyph of the
// game's sprite texture (OpenGL, from the per-frame call, where the game's context is current),
// and names each record after a location ("MASTER SWORD FOR ALICE"), or "AP ITEM" where no label
// is: the game's own pickup popup then shows that name. The location is the one a live object of
// that stand-in within the player's reach belongs to, by the map cell the object carries (an
// enemy's drop lands wherever the enemy dies, and a room can span several cells); else the
// player's cell's, if it has a label; else that of the nearest such object in the loaded rooms. Every field is checked against the game's own values before the first write, and the
// texture is read back once (the places to paint blank, known art where the PNG has it) before
// the first paint; unload puts the records back to the game's values and clears the painted
// pixels (transparent, as in the game's art).
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

// How far from the player (pixels, between the positions) an object counts as within reach:
// more than a stand-in's and the player's half-sizes together, less than half a cell.
constexpr double kStandInReach = 96;

// For each stand-in record (47, then 48), the map cell to name it after: the cell of the nearest
// object of that record with a labeled cell, if within kStandInReach; else `player_cell`, if it
// has a label; else that nearest object's cell, wherever it is; else `player_cell`. Returns the
// record (0 or 1) whose object is nearest within reach, the one a pickup is about to take, or -1.
int name_cells(const std::vector<StandInSeen>& seen, double player_x, double player_y,
               int player_cell, const std::map<int, std::string>& labels, int (&cells)[2]);

// From opengl32.dll if the process has loaded it; otherwise all null.
GlFunctions load_gl();

// One glyph of the game's 8 x 8 font, painted by the helper into a slot that is empty in the
// game's art (the locked ASSIST's padlock, menu.h). Painted while wanted, again now and then
// and whenever the texture changes, and cleared when no longer wanted.
class FontGlyph {
public:
    // `rows`: 8 strings of 8, '#' for an opaque white pixel.
    FontGlyph(uint8_t code, const char (*rows)[9]) : code_(code), rows_(rows) {}
    // Game thread (the GL context current). `texture_holder` holds the pointer to the sprite
    // data.
    void paint(BYTE* texture_holder, const GlFunctions& gl);
    void clear(BYTE* texture_holder, const GlFunctions& gl);
    bool painted() const { return painted_ != 0; }

private:
    bool upload(unsigned texture, const GlFunctions& gl, bool clear);
    uint8_t code_;
    const char (*rows_)[9];
    unsigned painted_ = 0;
    unsigned verified_ = 0;
    unsigned failed_ = 0;  // a texture it could not paint into: tried again only now and then
    int frames_to_paint_ = 0;
};

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
    // Both named for one cell.
    void watch(BYTE* records_holder, int cell) {
        const int cells[2] = {cell, cell};
        watch(records_holder, cells);
    }

    // Game thread, every frame while active (from the swap hook, the game's GL context
    // current): paints the logo into the sprite texture now and then, and whenever the
    // texture changes. `texture_holder` holds the pointer to the sprite data.
    // `now_ms` is a millisecond clock (GetTickCount64): the progression arrow's color follows it.
    // `world`: false while a menu is open, when the logos are cleared instead.
    void paint(BYTE* texture_holder, const GlFunctions& gl, uint64_t now_ms, bool world = true);

    // Game thread: the record back to the game's own values (also one an earlier helper left
    // restyled), the pixels cleared if this helper painted them, the labels dropped.
    void restore(BYTE* records_holder, BYTE* texture_holder, const GlFunctions& gl);

    // Game thread: the records back to the game's own values, the labels and the logo kept
    // (the next watch restyles them again): for menus, which show the records too.
    void release(BYTE* records_holder);

    bool restyled() const { return records_[0] != nullptr || records_[1] != nullptr; }
    unsigned painted_texture() const { return painted_; }

private:
    BYTE* record(BYTE* records_holder, int item) const;
    // Paints (or clears) the logo, the first time for a texture only after reading it back:
    // the two places blank (or already the logo) and known art where the PNG has it.
    // `arrow_only`: just the progression logo's frame, for the arrow's next color.
    bool upload(unsigned texture, const GlFunctions& gl, bool clear, bool arrow_only);

    std::map<int, std::string> labels_;
    BYTE* records_[kStandInCount] = {};
    unsigned painted_ = 0;
    unsigned verified_ = 0;  // the texture read back and found as expected
    int frames_to_paint_ = 0;
    int arrow_phase_ = 0;
};

}  // namespace zap
