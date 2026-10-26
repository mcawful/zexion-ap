#include "looks.h"

#include <cstring>
#include <vector>

#include "apitem.h"  // kStandInItem, kStandInProgression and their logo frames
#include "game_mem.h"

namespace zap {

namespace {

// The pools' layouts are in game_layout.h (sprite::, obj::; docs/re/memory.md, "Live item
// objects"; B1).
constexpr int kStandInPalette = 8;  // Scrap Energy's and Scrap Chunk's
constexpr int kPalettes = 65;       // the table's filled entries in a live B1 game, 0 to 64
constexpr int kScanFrames = 30;     // frames between scans of the general pool

int logo_frame(int record) {
    return record == kStandInItem ? kStandInFrame : kStandInProgressionFrame;
}

BYTE* read_pointer(const BYTE* at) {
    BYTE* p = nullptr;
    if (at == nullptr || !safe_read(at, &p, sizeof(p))) return nullptr;
    return p;
}

// How item `item` looks at `clock`: the frame its record's animation is on, and its palette.
struct Look {
    int frame = 0;
    int16_t palette = 0;
    BYTE* palette_object = nullptr;
};

// The look of item `item` (its record in `records`) for an object whose frame list holds
// `count` frames: false if the record does not read, its frames do not lie in the list, or its
// palette is not in the table.
bool look_of(const BYTE* palette_table, const BYTE* records, int item, int64_t clock,
             int32_t count, Look& out) {
    BYTE r[record::kSize];
    if (!safe_read(records + item * record::kSize, r, sizeof(r))) return false;
    int16_t first = field<int16_t>(r, record::kFrame);
    int16_t ticks = field<int16_t>(r, record::kTicks);
    int16_t frames = field<int16_t>(r, record::kFrames);
    int palette = static_cast<int8_t>(r[record::kPalette]);
    if (ticks <= 0 || frames <= 0 || first < 0 || first + frames > count || palette < 0 ||
        palette >= kPalettes) {
        return false;
    }
    out.palette_object = read_pointer(palette_table + palette * 8);
    if (out.palette_object == nullptr) return false;
    out.frame = first + static_cast<int>((clock / ticks) % frames);
    out.palette = static_cast<int16_t>(palette);
    return true;
}

// The object drawn in the look's palette, if it is not already (`seen`: its copy).
void put_palette(BYTE* object, const BYTE* seen, const Look& look) {
    if (field<BYTE*>(seen, sprite::kPalette) == look.palette_object) return;
    put(object, sprite::kPalette, look.palette_object);
    put(object, sprite::kPaletteNumber, look.palette);
}

}  // namespace

int ItemLooks::item_at(uint8_t row, uint8_t column) const {
    if (row >= kMapSide || column >= kMapSide) return -1;
    auto it = items_.find(row * kMapSide + column);
    if (it == items_.end() || it->second < 0 || it->second >= record::kCount) return -1;
    return it->second;
}

bool ItemLooks::dressed() const {
    for (const Dressed& d : dressed_) {
        if (d.spawn != 0) return true;
    }
    for (const Dressed& d : flipped_dressed_) {
        if (d.spawn != 0) return true;
    }
    return false;
}

void ItemLooks::undress(BYTE* object, int slot, BYTE* base, BYTE* standin_palette) {
    Dressed& d = dressed_[slot];
    if (d.spawn == 0) return;
    BYTE* now_base = field<BYTE*>(object, sprite::kFrameList);
    if (field<int32_t>(object, obj::kSpawn) != d.spawn || (now_base != d.base &&
        now_base != base)) {
        d = {};  // another spawn, or a base not ours: nothing of ours to put back
        return;
    }
    if (base == nullptr || standin_palette == nullptr) return;  // kept: unload waits
    std::memcpy(object + sprite::kFrameList, &base, sizeof(base));
    std::memcpy(object + sprite::kPalette, &standin_palette, sizeof(standin_palette));
    int16_t number = kStandInPalette;
    std::memcpy(object + sprite::kPaletteNumber, &number, sizeof(number));
    d = {};
}

void ItemLooks::apply(const Places& at) {
    if (at.pool == nullptr) return;
    // The template every item object's frame-list base is copied from, the stand-ins' palette,
    // the records and the clock; without any of them, nothing is dressed.
    BYTE* base = item_frame_base(at);
    BYTE* standin_palette = read_pointer(at.palette_table + kStandInPalette * 8);
    BYTE* records = read_pointer(at.item_records);
    int64_t clock = 0;
    if (base == nullptr || standin_palette == nullptr || records == nullptr ||
        !safe_read(at.frame_clock, &clock, sizeof(clock)) || clock < 0) {
        return;
    }
    if (items_.empty() && !dressed()) return;  // nothing to show, nothing to put back
    pool_.resize(obj::kItemSize * kSlots);
    if (!safe_read(at.pool, pool_.data(), pool_.size())) return;
    // The stand-ins' records as they are now: an object is dressed only while its record shows
    // the one logo frame the shift is worked out from (restyled by apitem.h), so the game's
    // frame plus the shift always stays in the frame list.
    auto restyled = [&](int record) {
        BYTE r[record::kSize];
        return safe_read(records + record * record::kSize, r, sizeof(r)) &&
               field<int16_t>(r, record::kFrame) == logo_frame(record) &&
               field<int16_t>(r, record::kFrames) == 1;
    };
    bool ours[2] = {restyled(kStandInItem), restyled(kStandInProgression)};
    for (int slot = 0; slot < kSlots; ++slot) {
        const BYTE* seen = pool_.data() + slot * obj::kItemSize;
        BYTE* object = at.pool + slot * obj::kItemSize;
        Dressed& d = dressed_[slot];
        int32_t spawn = field<int32_t>(seen, obj::kSpawn);
        int16_t record = field<int16_t>(seen, obj::kRecord);
        bool stand_in = record == kStandInItem || record == kStandInProgression;
        if (d.spawn != 0 && d.spawn != spawn) d = {};  // another object now: none of ours
        if (spawn == 0 || !stand_in || !ours[record == kStandInProgression ? 1 : 0]) {
            undress(object, slot, base, standin_palette);  // put back while still ours
            continue;
        }
        int item = item_at(seen[obj::kItemRow], seen[obj::kItemColumn]);
        if (item < 0) {
            if (d.spawn != 0) undress(object, slot, base, standin_palette);
            continue;
        }
        BYTE* now_base = field<BYTE*>(seen, sprite::kFrameList);
        // Not dressed: it must hold the template's base and the stand-ins' palette. Dressed (this
        // spawn): the base it was given, or the template's if the game set it back.
        const bool expected =
            d.spawn == 0 ? now_base == base && field<BYTE*>(seen, sprite::kPalette) == standin_palette
                         : now_base == d.base || now_base == base;
        Look look;
        if (!expected || !look_of(at.palette_table, records, item, clock,
                                  field<int32_t>(seen, sprite::kFrameCount), look)) {
            continue;
        }
        BYTE* new_base = base + (look.frame - logo_frame(record)) * sprite::kFrameSize;
        if (now_base != new_base) put(object, sprite::kFrameList, new_base);
        put_palette(object, seen, look);
        d = {spawn, new_base};
    }
    apply_flipped(at, base, standin_palette, records, clock, ours);
}

void ItemLooks::undress_flipped(BYTE* object, int slot, BYTE* base, BYTE* standin_palette) {
    Dressed& d = flipped_dressed_[slot];
    if (d.spawn == 0) return;
    BYTE probe[obj::kGeneralSize];
    if (!safe_read(object, probe, sizeof(probe))) {
        d = {};
        return;
    }
    BYTE* now_base = field<BYTE*>(probe, sprite::kFrameList);
    if (field<int32_t>(probe, obj::kSpawn) != d.spawn || (now_base != d.base && now_base != base)) {
        d = {};
        return;
    }
    if (base == nullptr || standin_palette == nullptr) return;
    int32_t frame = field<int32_t>(probe, sprite::kFrameIndex);
    BYTE* shown = base + static_cast<ptrdiff_t>(frame) * sprite::kFrameSize;
    std::memcpy(object + sprite::kFrameList, &base, sizeof(base));
    std::memcpy(object + sprite::kFrameShown, &shown, sizeof(shown));
    std::memcpy(object + sprite::kPalette, &standin_palette, sizeof(standin_palette));
    int16_t number = kStandInPalette;
    std::memcpy(object + sprite::kPaletteNumber, &number, sizeof(number));
    d = {};
}

// An object showing a stand-in: live, drawing from the item sprites' frame list (the
// template's), at one of the two logo frames, which are empty in the game's own art.
BYTE* ItemLooks::item_frame_base(const Places& at) {
    BYTE* templates = read_pointer(at.sprite_templates);
    BYTE* item_template =
        templates != nullptr ? read_pointer(templates + sprite::kItemTemplate) : nullptr;
    return item_template != nullptr ? read_pointer(item_template + sprite::kFrameList) : nullptr;
}

bool ItemLooks::shows_stand_in(const BYTE* o, const BYTE* base) {
    int32_t frame = field<int32_t>(o, sprite::kFrameIndex);
    return field<int32_t>(o, obj::kSpawn) != 0 && field<BYTE*>(o, sprite::kFrameList) == base &&
           (frame == logo_frame(kStandInItem) || frame == logo_frame(kStandInProgression));
}

void ItemLooks::scan(const Places& at) {
    if (at.object_pool == nullptr || frames_to_scan_-- > 0) return;
    frames_to_scan_ = kScanFrames;
    BYTE* base = item_frame_base(at);
    BYTE* standin_palette = read_pointer(at.palette_table + kStandInPalette * 8);
    flipped_slots_.clear();
    if (base == nullptr) return;
    objects_.resize(obj::kGeneralSize * kObjectSlots);
    if (safe_read(at.object_pool, objects_.data(), objects_.size())) {
        for (int slot = 0; slot < kObjectSlots; ++slot) {
            if (shows_stand_in(objects_.data() + slot * obj::kGeneralSize, base)) {
                flipped_slots_.push_back(slot);
            }
        }
    }
    // Any dressed earlier and not found now is put back if it is still ours.
    for (int slot = 0; slot < kObjectSlots; ++slot) {
        if (flipped_dressed_[slot].spawn == 0) continue;
        bool found = false;
        for (int s : flipped_slots_) found = found || s == slot;
        if (!found) {
            undress_flipped(at.object_pool + slot * obj::kGeneralSize, slot, base, standin_palette);
        }
    }
}

void ItemLooks::apply_flipped(const Places& at, BYTE* base, BYTE* standin_palette, BYTE* records,
                              int64_t clock, const bool (&ours)[2]) {
    if (at.object_pool == nullptr) return;
    auto shows_logo = [&](const BYTE* o) { return shows_stand_in(o, base); };
    for (int slot : flipped_slots_) {
        BYTE* object = at.object_pool + slot * obj::kGeneralSize;
        BYTE seen[obj::kGeneralSize];
        Dressed& d = flipped_dressed_[slot];
        if (!safe_read(object, seen, sizeof(seen))) {
            d = {};
            continue;
        }
        int32_t spawn = field<int32_t>(seen, obj::kSpawn);
        if (d.spawn != 0 && d.spawn != spawn) d = {};
        if (!shows_logo(seen)) {
            undress_flipped(object, slot, base, standin_palette);
            continue;
        }
        // Which stand-in: the logo frame the game computed from its record.
        int32_t frame = field<int32_t>(seen, sprite::kFrameIndex);
        int record = frame == logo_frame(kStandInItem)          ? kStandInItem
                     : frame == logo_frame(kStandInProgression) ? kStandInProgression
                                                                : -1;
        if (record < 0 || !ours[record == kStandInProgression ? 1 : 0]) {
            undress_flipped(object, slot, base, standin_palette);
            continue;
        }
        int item = item_at(seen[obj::kGeneralRow], seen[obj::kGeneralColumn]);
        if (item < 0) {
            undress_flipped(object, slot, base, standin_palette);
            continue;
        }
        // Its frame list is left as the game's (the template's); only the frame shown and the
        // palette are written, so whatever frame its own code sets, nothing points outside the
        // list.
        if (d.spawn == 0 && field<BYTE*>(seen, sprite::kPalette) != standin_palette) continue;
        int32_t count = field<int32_t>(seen, sprite::kFrameCount);
        Look look;
        if (frame < 0 || frame >= count ||
            !look_of(at.palette_table, records, item, clock, count, look)) {
            continue;
        }
        put(object, sprite::kFrameShown, base + look.frame * sprite::kFrameSize);
        put_palette(object, seen, look);
        d = {spawn, base};
    }
}

void ItemLooks::restore_flipped(const Places& at, BYTE* base, BYTE* standin_palette) {
    for (int slot = 0; slot < kObjectSlots; ++slot) {
        if (flipped_dressed_[slot].spawn == 0) continue;
        if (at.object_pool == nullptr) {
            flipped_dressed_[slot] = {};
            continue;
        }
        undress_flipped(at.object_pool + slot * obj::kGeneralSize, slot, base, standin_palette);
    }
    flipped_slots_.clear();
    frames_to_scan_ = 0;
}

void ItemLooks::restore(const Places& at) {
    if (at.pool == nullptr || !dressed()) {
        flipped_slots_.clear();  // found again by the next scan
        frames_to_scan_ = 0;
        for (Dressed& d : dressed_) d = {};
        for (Dressed& d : flipped_dressed_) d = {};
        return;
    }
    BYTE* base = item_frame_base(at);
    BYTE* standin_palette = read_pointer(at.palette_table + kStandInPalette * 8);
    for (int slot = 0; slot < kSlots; ++slot) {
        if (dressed_[slot].spawn == 0) continue;
        BYTE* object = at.pool + slot * obj::kItemSize;
        BYTE probe[obj::kItemSize];
        if (!safe_read(object, probe, sizeof(probe))) {
            dressed_[slot] = {};
            continue;
        }
        undress(object, slot, base, standin_palette);
    }
    restore_flipped(at, base, standin_palette);
}

}  // namespace zap
