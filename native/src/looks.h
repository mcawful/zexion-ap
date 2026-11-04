// Other Zexion players' items shown as themselves, and the stand-ins' own colors
// (native/README.md, "Other Zexion players' items, as themselves" and "Other players' items";
// docs/re/memory.md, "Live item objects" and "Other players' items"). While the stand-ins are
// restyled, stand-ins in a cell labeled with a Zexion item are dressed as it (palette and
// frame-list base; the record, and so the pickup, stay the stand-in's), the others drawn in
// palette 26 at style 34, and R'Talva X34 Y51's marker hidden while it waits for the boss. Every
// value is checked before a write, and an object is put back only while it is the same spawn.
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <vector>

#include "addresses.h"

namespace zap {

class ItemLooks {
public:
    // Game thread. Map cell (y * 54 + x) -> the Zexion item to show there.
    void set_items(std::map<int, int> items) { items_ = std::move(items); }

    // Game thread, every frame while the stand-ins are restyled: dresses the stand-ins in those
    // cells, and puts back any whose cell no longer names an item. `pool`: this frame's copy of
    // the item pool (kSlots objects; null if unread), `base`: item_frame_base.
    void apply(const GameAddresses& at, const BYTE* pool, BYTE* base);

    // Game thread, each frame while restyled, before apply: every 31st, finds the general objects
    // showing a logo (flipped_slots), and puts back any it dressed that no longer shows one.
    void scan(const GameAddresses& at, BYTE* base);

    // Game thread, every frame while the stand-ins are restyled, after scan, whatever the labels:
    // R'Talva X34 Y51's marker hidden while it waits on a painted logo frame, else shown again.
    void hide_marker(const GameAddresses& at, BYTE* base);

    // Game thread, each frame while restyled, after apply: each live stand-in not dressed drawn in
    // palette 26 at style 34, or put back (palette 8, its own style) where it is not restyled.
    void own_colors(const GameAddresses& at, const BYTE* pool, BYTE* base);
    int colored() const;  // objects drawn in palette 26 now (tests)

    // Game thread: puts back each object it dressed, hid or colored still that spawn; forgets all.
    void restore(const GameAddresses& at);
    bool dressed() const;
    // General objects showing a stand-in's logo at the last scan (slots of the pool); each
    // still to be checked with shows_stand_in, as the list can be 30 frames old.
    const std::vector<int>& flipped_slots() const { return flipped_slots_; }
    // A live object drawing from the item sprites' frame list (`base`, item_frame_base) at a
    // logo frame, empty in the game's own art: a stand-in, or a marker (is_marker).
    static bool shows_stand_in(const BYTE* object, const BYTE* base);
    // General object `slot` (`object`: a copy up to its first state, obj::kFirstState) is the
    // marker, not a stand-in: type 1146's first state, in a palette other than a stand-in's (after
    // the boss it shows location 81's item, a stand-in in palette 8), not dressed by this helper.
    bool is_marker(int slot, const BYTE* object, const GameAddresses& at) const;
    // Markers this helper has hidden, still to be put back (tests).
    int hidden_markers() const;
    // The item sprites' frame list (every item object's base, the template's), or null.
    static BYTE* item_frame_base(const GameAddresses& at);

    static constexpr int kSlots = 128;
    static constexpr int kObjectSlots = 1000;

private:
    struct Dressed {
        int32_t spawn = 0;  // its spawn (obj::kSpawn) when dressed; 0 for none
        BYTE* base = nullptr;  // the frame-list base it was given
    };
    // The Zexion item to show at map cell (row, column), or -1.
    int item_at(uint8_t row, uint8_t column) const;
    // Puts `object` (`seen`: this frame's copy) back to `base` and the stand-ins' palette (with
    // `frame_shown`, the frame shown too) if it is still the spawn `d` dressed, on its base or
    // `base`. Forgets it only if put back or no longer that spawn. undress_live reads `size`
    // bytes first (one that does not read: forgotten).
    static void undress(BYTE* object, const BYTE* seen, Dressed& d, BYTE* base,
                        BYTE* standin_palette, bool frame_shown);
    static void undress_live(BYTE* object, size_t size, Dressed& d, BYTE* base,
                             BYTE* standin_palette, bool frame_shown);

    // General objects showing a stand-in's logo (found by scan), dressed every frame.
    void apply_flipped(const GameAddresses& at, BYTE* base, BYTE* standin_palette, BYTE* records,
                       int64_t clock, const bool (&ours)[2]);
    // Flag 154 (set by R'Talva X34 Y51's boss) is clear: the marker still waits, empty.
    static bool marker_waits(const GameAddresses& at);
    // An object drawn in palette 26: its spawn, and the style it had (put back with palette 8).
    struct Colored {
        int32_t spawn = 0;  // 0 for none
        int32_t style = 0;
    };
    // Draws `object` (`seen`: its copy) in palette 26 (`own`) at style 34, its first style in `c`.
    static void color(BYTE* object, const BYTE* seen, Colored& c, BYTE* own);
    // Puts `object` back in palette 8 (`game`) with the style `c` kept, if it is still that spawn
    // and still the helper's look; forgets it.
    static void uncolor(BYTE* object, const BYTE* seen, Colored& c, BYTE* game, const BYTE* own);
    static void uncolor_live(BYTE* object, size_t size, Colored& c, BYTE* game, const BYTE* own);
    // A marker hidden: its spawn, so it is put back only while it is the same object.
    struct Hidden {
        int32_t spawn = 0;  // 0 for none
    };
    // Shows marker `object` (`seen`: its copy) again if it is still the spawn `h` hid and still
    // hidden, whatever frame it is on (after the boss, its reward); forgets it.
    static void unhide(BYTE* object, const BYTE* seen, Hidden& h);
    static void unhide_live(BYTE* object, Hidden& h);

    std::map<int, int> items_;
    Dressed dressed_[kSlots] = {};
    std::vector<Dressed> flipped_dressed_ = std::vector<Dressed>(kObjectSlots);
    std::vector<Hidden> hidden_ = std::vector<Hidden>(kObjectSlots);
    Colored colored_items_[kSlots] = {};
    std::vector<Colored> colored_flipped_ = std::vector<Colored>(kObjectSlots);
    std::vector<int> flipped_slots_;  // general objects showing a logo at the last scan
    int frames_to_scan_ = 0;
    std::vector<BYTE> objects_;  // the last scan's copy of the general pool
};

}  // namespace zap
