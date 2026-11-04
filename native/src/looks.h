// Other Zexion players' items shown as themselves (docs/re/memory.md, "Live item objects").
//
// A location holding another Zexion player's item holds a stand-in (Scrap Energy or Scrap
// Chunk, restyled by apitem.h), so picking it up gives only scrap. While the stand-ins are
// restyled, every frame call looks through the game's pool of live item objects for stand-ins
// in a map cell whose label names a Zexion item, and dresses each one as that item: its palette
// set to the item's, and its frame-list base moved so the frame the game computes from the
// stand-in's record lands on the item's frame, animated with the game's own clock. The record,
// and so the pickup, stay the stand-in's. Every value is checked before a write, and an object
// is put back only while it is the same spawn this helper dressed.
//
// Some rooms show an item through an object in the general pool instead. Every 31st frame call
// a scan finds any live one drawing a logo frame from the item sprites' list, and dresses it by
// its frame shown and palette only, leaving its frame list as the game's.
//
// R'Talva X34 Y51's marker (type 1146, its first state 8210) draws the logo frame 63 in its own
// palette while it waits for the boss (flag 154 clear): empty in the game's art, it would show the
// helper's logo (docs/re/memory.md, "Other players' items"). After the boss it becomes location
// 81's reward display. While the stand-ins are restyled (whatever the labels: the logo is
// painted for any) and flag 154 is clear, it is hidden every
// frame (its shown byte cleared, which the draw loop skips and its own update leaves); it is shown
// again, on whatever frame, as soon as it no longer is such a marker (the flag set, another
// frame), and when the stand-ins are put back or the helper unloads.
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
    // cells, and puts back any it dressed whose cell no longer names an item. `pool`: this
    // frame's copy of the item pool (kSlots objects; null if it could not be read), `base` the
    // item sprites' frame list (item_frame_base), both taken once a frame by the caller.
    void apply(const GameAddresses& at, const BYTE* pool, BYTE* base);

    // Game thread, every frame while the stand-ins are restyled, before apply: every 31st frame,
    // finds the general objects showing a stand-in's logo (flipped_slots), and puts back any it
    // dressed that no longer shows one.
    void scan(const GameAddresses& at, BYTE* base);

    // Game thread, every frame while the stand-ins are restyled, after scan, whatever the labels:
    // R'Talva X34 Y51's marker among the scan's objects hidden while it waits on a painted logo
    // frame, shown again otherwise (above).
    void hide_marker(const GameAddresses& at, BYTE* base);

    // Game thread: puts back every object it dressed that is still that spawn; forgets them.
    void restore(const GameAddresses& at);
    bool dressed() const;
    // General objects showing a stand-in's logo at the last scan (slots of the pool); each
    // still to be checked with shows_stand_in, as the list can be 30 frames old.
    const std::vector<int>& flipped_slots() const { return flipped_slots_; }
    // A live object drawing from the item sprites' frame list (`base`, item_frame_base) at a
    // logo frame, empty in the game's own art: a stand-in, or a marker (is_marker).
    static bool shows_stand_in(const BYTE* object, const BYTE* base);
    // General object `slot` (`object`: a copy up to its first state, obj::kFirstState) is the
    // marker, not a stand-in: type 1146's first state, in a palette other than the stand-ins'
    // (`standin_palette`), not dressed by this helper. A marker on a logo frame (shows_stand_in)
    // is the one to hide while flag 154 is clear.
    bool is_marker(int slot, const BYTE* object, const BYTE* standin_palette) const;
    // Markers this helper has hidden, still to be put back (tests).
    int hidden_markers() const;
    // The stand-ins' palette object, or null.
    static BYTE* stand_ins_palette(const GameAddresses& at);
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
    // Puts `object` (`seen`: this frame's copy of it) back if it is still the spawn `d` dressed,
    // on the base it was given or the template's: the template's base `base` and the stand-ins'
    // palette, and with `frame_shown` the frame it shows on that base (a general object's).
    // Forgets it only if put back, or if it is no longer that spawn. undress_live reads the
    // object's `size` bytes first (one that does not read is forgotten).
    static void undress(BYTE* object, const BYTE* seen, Dressed& d, BYTE* base,
                        BYTE* standin_palette, bool frame_shown);
    static void undress_live(BYTE* object, size_t size, Dressed& d, BYTE* base,
                             BYTE* standin_palette, bool frame_shown);

    // General objects showing a stand-in's logo (found by scan), dressed every frame; markers
    // among them hidden.
    void apply_flipped(const GameAddresses& at, BYTE* base, BYTE* standin_palette, BYTE* records,
                       int64_t clock, const bool (&ours)[2]);
    // Flag 154 (set by R'Talva X34 Y51's boss) is clear: the marker still waits, empty.
    static bool marker_waits(const GameAddresses& at);
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
    std::vector<int> flipped_slots_;  // general objects showing a logo at the last scan
    int frames_to_scan_ = 0;
    std::vector<BYTE> objects_;  // the last scan's copy of the general pool
};

}  // namespace zap
