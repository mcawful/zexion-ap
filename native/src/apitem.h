// How other players' items look in the game (helper phase 4).
//
// A location holding another player's item is given item 47 (Scrap Energy) by the world. While
// the client has sent the helper its labels, the helper restyles that item's record (the
// `Objects` file, 100 bytes per record behind `item_records`; docs/re/memory.md, "Other
// players' items"): its in-world animation points at frame 63 of the item sprites (one frame,
// transparent in the game's own art), its popup icon at the font's glyph 0xEE (empty in the
// game's own art), and its pickup no longer runs area event 3549 (a refill drop). The helper
// paints an Archipelago logo into that frame and glyph of the game's sprite texture (OpenGL,
// from the per-frame call, where the game's context is current), and names the record after
// the location in the cell the player stands in ("MASTER SWORD FOR ALICE"), or "ARCHIPELAGO
// ITEM" elsewhere: the game's own pickup popup then shows that name. Every field is checked
// against the game's own values before the first write; unload puts the record back and
// clears the painted pixels (transparent, as in the game's art).
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>

namespace zap {

constexpr int kStandInItem = 47;
constexpr size_t kItemNameMax = 49;  // the record's 50 bytes, NUL-terminated

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
    bool loaded() const {
        return BindTexture && GetIntegerv && GetTexLevelParameteriv && PixelStorei &&
               TexSubImage2D && GetError;
    }
};

// From opengl32.dll if the process has loaded it; otherwise all null.
GlFunctions load_gl();

class ApItems {
public:
    // Game thread. The labels by map cell (index y * 54 + x), already filtered to the font.
    void set_labels(std::map<int, std::string> labels);
    bool active() const { return !labels_.empty(); }

    // Game thread, every frame while active: restyles the record if it reads as the game's own
    // (again, if the game reloaded its records), and names it for the player's cell.
    // `records_holder` holds the pointer to the records; `cell` is the player's map cell index.
    void watch(BYTE* records_holder, int cell);

    // Game thread, every frame while active (from the swap hook, the game's GL context
    // current): paints the logo into the sprite texture now and then, and whenever the
    // texture changes. `texture_holder` holds the pointer to the sprite data.
    void paint(BYTE* texture_holder, const GlFunctions& gl);

    // Game thread: the record back, the pixels cleared, the labels dropped.
    void restore(BYTE* records_holder, BYTE* texture_holder, const GlFunctions& gl);

    bool restyled() const { return record_ != nullptr; }
    unsigned painted_texture() const { return painted_; }

private:
    BYTE* record(BYTE* records_holder) const;
    bool upload(unsigned texture, const GlFunctions& gl, bool clear);

    std::map<int, std::string> labels_;
    BYTE* record_ = nullptr;
    BYTE saved_[0x40] = {};  // the record's first 0x40 bytes: animation, icon, palette, name
    int16_t saved_event_ = 0;
    unsigned painted_ = 0;
    int frames_to_paint_ = 0;
};

}  // namespace zap
