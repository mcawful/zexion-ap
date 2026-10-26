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
// Some rooms show an item through an object in the game's general object pool (1000 slots of
// 0x5C8 bytes) instead: type 2026 in the reversed-gravity rooms, type 715 elsewhere, each with
// the same sprite fields as an item object and its map cell at +0x210 and +0x211
// (docs/re/memory.md, "Live item objects"). Any live one drawing a stand-in's logo (the item
// sprites' frame list, a logo frame) is found by a scan of the pool every 30 frames and dressed
// by the frame shown (+0x1B0) and the palette only, its frame list left as the game's.
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <vector>

namespace zap {

class ItemLooks {
public:
    struct Places {
        BYTE* pool = nullptr;              // 128 item objects, 0x3B0 bytes each
        BYTE* palette_table = nullptr;     // palette objects by number
        BYTE* frame_clock = nullptr;       // 64-bit count the item animations follow
        BYTE* sprite_templates = nullptr;  // holds the pointer to the sprite definitions' templates
        BYTE* item_records = nullptr;      // holds the pointer to the item records
        BYTE* object_pool = nullptr;       // the general objects, 1000 of 0x5C8 bytes
    };

    // Game thread. Map cell (y * 54 + x) -> the Zexion item to show there.
    void set_items(std::map<int, int> items) { items_ = std::move(items); }

    // Game thread, every frame while the stand-ins are restyled: dresses the stand-ins in those
    // cells, and puts back any it dressed whose cell no longer names an item. `pool`: this
    // frame's copy of the item pool (kSlots objects; null if it could not be read), `base` the
    // item sprites' frame list (item_frame_base), both taken once a frame by the caller.
    void apply(const Places& at, const BYTE* pool, BYTE* base);

    // Game thread, every frame while the stand-ins are restyled, before apply: every 30 frames,
    // finds the general objects showing a stand-in's logo (flipped_slots), and puts back any it
    // dressed that no longer shows one.
    void scan(const Places& at, BYTE* base);

    // Game thread: puts back every object it dressed that is still that spawn; forgets them.
    void restore(const Places& at);
    bool dressed() const;
    // General objects showing a stand-in's logo at the last scan (slots of the pool); each
    // still to be checked with shows_stand_in, as the list can be 30 frames old.
    const std::vector<int>& flipped_slots() const { return flipped_slots_; }
    // A live object drawing from the item sprites' frame list (`base`, item_frame_base) at a
    // logo frame, empty in the game's own art: a stand-in.
    static bool shows_stand_in(const BYTE* object, const BYTE* base);
    // The item sprites' frame list (every item object's base, the template's), or null.
    static BYTE* item_frame_base(const Places& at);

    static constexpr int kSlots = 128;
    static constexpr int kObjectSlots = 1000;

private:
    struct Dressed {
        int32_t spawn = 0;  // the object's +0x1D8 when dressed; 0 for none
        BYTE* base = nullptr;  // the frame-list base it was given
    };
    // The Zexion item to show at map cell (row, column), or -1.
    int item_at(uint8_t row, uint8_t column) const;
    // Puts the object back if it is still the spawn this helper dressed with the base it gave
    // it; forgets it only if put back, or if it is no longer that spawn.
    void undress(BYTE* object, int slot, BYTE* base, BYTE* standin_palette);

    // General objects showing a stand-in's logo (found by scan), dressed every frame.
    void apply_flipped(const Places& at, BYTE* base, BYTE* standin_palette, BYTE* records,
                       int64_t clock, const bool (&ours)[2]);
    void undress_flipped(BYTE* object, int slot, BYTE* base, BYTE* standin_palette);
    void restore_flipped(const Places& at, BYTE* base, BYTE* standin_palette);

    std::map<int, int> items_;
    Dressed dressed_[kSlots] = {};
    std::vector<Dressed> flipped_dressed_ = std::vector<Dressed>(kObjectSlots);
    std::vector<int> flipped_slots_;  // general objects showing a logo at the last scan
    int frames_to_scan_ = 0;
    std::vector<BYTE> objects_;  // the last scan's copy of the general pool
};

}  // namespace zap
