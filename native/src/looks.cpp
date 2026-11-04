#include "looks.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "apitem.h"  // the stand-ins and their logo frames
#include "game_mem.h"

namespace zap {

namespace {

constexpr int kStandInPalette = 8;  // Scrap Energy's and Scrap Chunk's
// The helper's look for them: palette 26 (no shader, glColor from the style) at style 34, which
// every rewrite of the style table sets to color 3: white, or cream with the fourth PALETTE
// display option (docs/re/memory.md, "Other players' items").
constexpr int kOwnPalette = 26;
constexpr int32_t kOwnStyle = 34;
constexpr int kPalettes = 65;       // the table's filled entries in a live B1 game, 0 to 64
constexpr int kScanFrames = 30;     // frames between scans of the general pool
constexpr int16_t kMarkerFirstState = 8210;  // type 1146's first state (Enemies, B1)
constexpr int kMarkerFlag = 154;             // its wait ends once set (state 8210's branch)

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
    out.palette_object = pointer_at(palette_table, palette * 8);
    if (out.palette_object == nullptr) return false;
    out.frame = first + static_cast<int>((clock / ticks) % frames);
    out.palette = static_cast<int16_t>(palette);
    return true;
}

// Which stand-ins' records are restyled now (one logo frame each): their objects may be dressed.
void restyled_stand_ins(const BYTE* records, bool (&ours)[kStandInCount]) {
    for (int i = 0; i < kStandInCount; ++i) {
        BYTE r[record::kSize];
        ours[i] = records != nullptr &&
                  safe_read(records + kStandInRecords[i] * record::kSize, r, sizeof(r)) &&
                  field<int16_t>(r, record::kFrame) == kStandInFrames[i] &&
                  field<int16_t>(r, record::kFrames) == 1;
    }
}

// The object drawn in the look's palette, if it is not already (`seen`: its copy).
void put_palette(BYTE* object, const BYTE* seen, const Look& look) {
    if (field<BYTE*>(seen, sprite::kPalette) == look.palette_object) return;
    put(object, sprite::kPalette, look.palette_object);
    put(object, sprite::kPaletteNumber, look.palette);
}

// The stand-ins' palette object as the game gives it (8), or null.
BYTE* stand_ins_palette(const GameAddresses& at) {
    return pointer_at(at.palette_table, kStandInPalette * 8);
}

// The palette object the helper draws them in (26), or null.
BYTE* own_palette(const GameAddresses& at) { return pointer_at(at.palette_table, kOwnPalette * 8); }

// With `entry` (a look kept for a spawn) set: `act` on a copy of `object`'s first `size` bytes;
// one that does not read is forgotten.
template <typename Entry, typename Act>
void act_live(BYTE* object, size_t size, Entry& entry, Act act) {
    if (entry.spawn == 0) return;
    BYTE seen[obj::kGeneralSize];
    static_assert(obj::kItemSize <= sizeof(seen));
    if (size > sizeof(seen) || !safe_read(object, seen, size)) {
        entry = {};
        return;
    }
    act(seen);
}

// `palette` is a stand-in's: the game's (8) or the helper's (26).
bool stand_in_palette(const GameAddresses& at, const BYTE* palette) {
    return palette != nullptr &&
           (palette == stand_ins_palette(at) || palette == own_palette(at));
}

}  // namespace

void ItemLooks::color(BYTE* object, const BYTE* seen, Colored& c, BYTE* own) {
    const int32_t spawn = field<int32_t>(seen, obj::kSpawn);
    if (c.spawn != spawn) c = {spawn, field<int32_t>(seen, sprite::kStyle)};
    if (field<BYTE*>(seen, sprite::kPalette) != own) {
        put(object, sprite::kPalette, own);
        put<int16_t>(object, sprite::kPaletteNumber, kOwnPalette);
    }
    if (field<int32_t>(seen, sprite::kStyle) != kOwnStyle) {
        put<int32_t>(object, sprite::kStyle, kOwnStyle);
    }
}

void ItemLooks::uncolor(BYTE* object, const BYTE* seen, Colored& c, BYTE* game, const BYTE* own) {
    if (c.spawn == 0) return;
    if (field<int32_t>(seen, obj::kSpawn) == c.spawn && game != nullptr) {
        if (field<BYTE*>(seen, sprite::kPalette) == own) {
            put(object, sprite::kPalette, game);
            put<int16_t>(object, sprite::kPaletteNumber, kStandInPalette);
        }
        if (field<int32_t>(seen, sprite::kStyle) == kOwnStyle) {
            put<int32_t>(object, sprite::kStyle, c.style);
        }
    }
    c = {};
}

void ItemLooks::uncolor_live(BYTE* object, size_t size, Colored& c, BYTE* game,
                             const BYTE* own) {
    act_live(object, size, c, [&](const BYTE* seen) { uncolor(object, seen, c, game, own); });
}

int ItemLooks::colored() const {
    int n = 0;
    for (const Colored& c : colored_items_) n += c.spawn != 0;
    for (const Colored& c : colored_flipped_) n += c.spawn != 0;
    return n;
}

void ItemLooks::own_colors(const GameAddresses& at, const BYTE* pool, BYTE* base) {
    BYTE* game = stand_ins_palette(at);
    BYTE* own = own_palette(at);
    BYTE* records = pointer_at(at.item_records);
    if (game == nullptr || own == nullptr || records == nullptr || base == nullptr) return;
    bool ours[kStandInCount];
    restyled_stand_ins(records, ours);
    if (at.item_pool != nullptr && pool != nullptr) {
        for (int slot = 0; slot < kSlots; ++slot) {
            const BYTE* seen = pool + slot * obj::kItemSize;
            BYTE* object = at.item_pool + slot * obj::kItemSize;
            Colored& c = colored_items_[slot];
            const int32_t spawn = field<int32_t>(seen, obj::kSpawn);
            if (c.spawn != 0 && c.spawn != spawn) c = {};  // another object now
            const int stand_in = stand_in_index(field<int16_t>(seen, obj::kRecord));
            if (spawn == 0 || stand_in < 0) continue;
            if (!ours[stand_in]) {
                uncolor(object, seen, c, game, own);
            } else if (dressed_[slot].spawn != spawn &&
                       field<BYTE*>(seen, sprite::kFrameList) == base &&
                       stand_in_palette(at, field<BYTE*>(seen, sprite::kPalette))) {
                color(object, seen, c, own);
            }
        }
    }
    if (at.object_pool == nullptr) return;
    for (int slot : flipped_slots_) {
        BYTE* object = at.object_pool + slot * obj::kGeneralSize;
        BYTE seen[obj::kGeneralSize];
        Colored& c = colored_flipped_[slot];
        if (!safe_read(object, seen, sizeof(seen))) {
            c = {};
            continue;
        }
        const int32_t spawn = field<int32_t>(seen, obj::kSpawn);
        if (c.spawn != 0 && c.spawn != spawn) c = {};
        const int stand_in = logo_index(field<int32_t>(seen, sprite::kFrameIndex));
        if (!shows_stand_in(seen, base) || stand_in < 0 || !ours[stand_in]) {
            uncolor(object, seen, c, game, own);
        } else if (flipped_dressed_[slot].spawn != spawn &&
                   stand_in_palette(at, field<BYTE*>(seen, sprite::kPalette))) {
            color(object, seen, c, own);
        }
    }
}

bool ItemLooks::is_marker(int slot, const BYTE* object, const GameAddresses& at) const {
    return slot >= 0 && slot < kObjectSlots && flipped_dressed_[slot].spawn == 0 &&
           stand_ins_palette(at) != nullptr &&
           !stand_in_palette(at, field<BYTE*>(object, sprite::kPalette)) &&
           field<int16_t>(object, obj::kFirstState) == kMarkerFirstState;
}

bool ItemLooks::marker_waits(const GameAddresses& at) {
    uint8_t flag = 1;
    // The game's branch reads the flag as a signed byte and goes on when it is above 0.
    return read(at.flags, kMarkerFlag, flag) && static_cast<int8_t>(flag) <= 0;
}

int ItemLooks::hidden_markers() const {
    return static_cast<int>(std::count_if(hidden_.begin(), hidden_.end(),
                                          [](const Hidden& h) { return h.spawn != 0; }));
}

void ItemLooks::unhide(BYTE* object, const BYTE* seen, Hidden& h) {
    if (h.spawn == 0) return;
    if (field<int32_t>(seen, obj::kSpawn) == h.spawn && seen[sprite::kVisible] == 0) {
        put<BYTE>(object, sprite::kVisible, 1);
    }
    h = {};
}

void ItemLooks::unhide_live(BYTE* object, Hidden& h) {
    act_live(object, obj::kGeneralSize, h, [&](const BYTE* seen) { unhide(object, seen, h); });
}

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

void ItemLooks::undress(BYTE* object, const BYTE* seen, Dressed& d, BYTE* base,
                        BYTE* standin_palette, bool frame_shown) {
    if (d.spawn == 0) return;
    BYTE* now_base = field<BYTE*>(seen, sprite::kFrameList);
    if (field<int32_t>(seen, obj::kSpawn) != d.spawn || (now_base != d.base && now_base != base)) {
        d = {};  // another spawn, or a base not ours: nothing of ours to put back
        return;
    }
    if (base == nullptr || standin_palette == nullptr) return;  // kept: unload waits
    put(object, sprite::kFrameList, base);
    if (frame_shown) {
        const ptrdiff_t frame = field<int32_t>(seen, sprite::kFrameIndex);
        put(object, sprite::kFrameShown, base + frame * sprite::kFrameSize);
    }
    put(object, sprite::kPalette, standin_palette);
    put<int16_t>(object, sprite::kPaletteNumber, kStandInPalette);
    d = {};
}

void ItemLooks::undress_live(BYTE* object, size_t size, Dressed& d, BYTE* base,
                             BYTE* standin_palette, bool frame_shown) {
    act_live(object, size, d, [&](const BYTE* seen) {
        undress(object, seen, d, base, standin_palette, frame_shown);
    });
}

void ItemLooks::apply(const GameAddresses& at, const BYTE* pool, BYTE* base) {
    if (at.item_pool == nullptr) return;
    BYTE* standin_palette = stand_ins_palette(at);
    BYTE* records = pointer_at(at.item_records);
    int64_t clock = 0;
    if (base == nullptr || standin_palette == nullptr || records == nullptr ||
        !safe_read(at.frame_clock, &clock, sizeof(clock)) || clock < 0) {
        return;
    }
    if (items_.empty() && !dressed()) return;  // nothing to show, nothing to put back
    if (pool == nullptr) return;
    // The stand-ins' records as they are now: an object is dressed only while its record shows
    // the one logo frame the shift is worked out from (restyled by apitem.h), so the game's
    // frame plus the shift always stays in the frame list.
    bool ours[kStandInCount];
    restyled_stand_ins(records, ours);
    for (int slot = 0; slot < kSlots; ++slot) {
        const BYTE* seen = pool + slot * obj::kItemSize;
        BYTE* object = at.item_pool + slot * obj::kItemSize;
        Dressed& d = dressed_[slot];
        int32_t spawn = field<int32_t>(seen, obj::kSpawn);
        int16_t record = field<int16_t>(seen, obj::kRecord);
        const int stand_in = stand_in_index(record);
        if (d.spawn != 0 && d.spawn != spawn) d = {};  // another object now: none of ours
        // The item it stands in for, if a live, restyled stand-in; else put back while ours.
        const int item = spawn == 0 || stand_in < 0 || !ours[stand_in]
                             ? -1
                             : item_at(seen[obj::kItemRow], seen[obj::kItemColumn]);
        if (item < 0) {
            undress(object, seen, d, base, standin_palette, false);
            continue;
        }
        BYTE* now_base = field<BYTE*>(seen, sprite::kFrameList);
        // Not dressed: it must hold the template's base and the stand-ins' palette. Dressed (this
        // spawn): the base it was given, or the template's if the game set it back.
        BYTE* now_palette = field<BYTE*>(seen, sprite::kPalette);
        const bool expected = d.spawn == 0
                                  ? now_base == base && stand_in_palette(at, now_palette)
                                  : now_base == d.base || now_base == base;
        Look look;
        if (!expected || !look_of(at.palette_table, records, item, clock,
                                  field<int32_t>(seen, sprite::kFrameCount), look)) {
            continue;
        }
        BYTE* new_base = base + (look.frame - kStandInFrames[stand_in]) * sprite::kFrameSize;
        if (now_base != new_base) put(object, sprite::kFrameList, new_base);
        put_palette(object, seen, look);
        d = {spawn, new_base};
    }
    apply_flipped(at, base, standin_palette, records, clock, ours);
}

BYTE* ItemLooks::item_frame_base(const GameAddresses& at) {
    BYTE* item_template = pointer_at(pointer_at(at.sprite_templates), sprite::kItemTemplate);
    return pointer_at(item_template, sprite::kFrameList);
}

bool ItemLooks::shows_stand_in(const BYTE* o, const BYTE* base) {
    return field<int32_t>(o, obj::kSpawn) != 0 && field<BYTE*>(o, sprite::kFrameList) == base &&
           logo_index(field<int32_t>(o, sprite::kFrameIndex)) >= 0;
}

void ItemLooks::scan(const GameAddresses& at, BYTE* base) {
    if (at.object_pool == nullptr || frames_to_scan_-- > 0) return;
    frames_to_scan_ = kScanFrames;
    BYTE* standin_palette = stand_ins_palette(at);
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
    // Any dressed, hidden or colored earlier and not found now is put back if it is still ours.
    BYTE* own = own_palette(at);
    for (int slot = 0; slot < kObjectSlots; ++slot) {
        if (std::find(flipped_slots_.begin(), flipped_slots_.end(), slot) != flipped_slots_.end()) {
            continue;
        }
        BYTE* object = at.object_pool + slot * obj::kGeneralSize;
        uncolor_live(object, obj::kGeneralSize, colored_flipped_[slot], standin_palette, own);
        unhide_live(object, hidden_[slot]);
        undress_live(object, obj::kGeneralSize, flipped_dressed_[slot], base, standin_palette,
                     true);
    }
}

void ItemLooks::hide_marker(const GameAddresses& at, BYTE* base) {
    if (at.object_pool == nullptr) return;
    bool ours[kStandInCount];
    restyled_stand_ins(pointer_at(at.item_records), ours);
    const bool waits = marker_waits(at);
    for (int slot : flipped_slots_) {
        BYTE* object = at.object_pool + slot * obj::kGeneralSize;
        BYTE seen[obj::kGeneralSize];
        Hidden& h = hidden_[slot];
        if (!safe_read(object, seen, sizeof(seen))) {
            h = {};
            continue;
        }
        const int32_t spawn = field<int32_t>(seen, obj::kSpawn);
        if (h.spawn != 0 && h.spawn != spawn) h = {};  // another object now: not ours to put back
        const int stand_in = logo_index(field<int32_t>(seen, sprite::kFrameIndex));
        // Hidden while it waits on the helper's logo frame, if the game shows it (one the game
        // hid is left to the game); shown again otherwise, whatever its frame.
        if (base != nullptr && shows_stand_in(seen, base) &&
            is_marker(slot, seen, at) && stand_in >= 0 && ours[stand_in] && waits) {
            if (h.spawn != 0 || seen[sprite::kVisible] != 0) {
                if (seen[sprite::kVisible] != 0) put<BYTE>(object, sprite::kVisible, 0);
                h = {spawn};
            }
        } else {
            unhide(object, seen, h);
        }
    }
}

void ItemLooks::apply_flipped(const GameAddresses& at, BYTE* base, BYTE* standin_palette,
                              BYTE* records, int64_t clock, const bool (&ours)[2]) {
    if (at.object_pool == nullptr) return;
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
        int32_t frame = field<int32_t>(seen, sprite::kFrameIndex);
        const int stand_in = logo_index(frame);
        // The marker is no stand-in to dress (hide_marker hides it).
        if (shows_stand_in(seen, base) && is_marker(slot, seen, at)) continue;
        int item = item_at(seen[obj::kGeneralRow], seen[obj::kGeneralColumn]);
        if (!shows_stand_in(seen, base) || stand_in < 0 || !ours[stand_in] || item < 0) {
            undress(object, seen, d, base, standin_palette, true);
            continue;
        }
        // Its frame list stays the template's; only the frame shown and the palette are written,
        // so whatever frame its own code sets, nothing points outside the list.
        if (d.spawn == 0 && !stand_in_palette(at, field<BYTE*>(seen, sprite::kPalette))) continue;
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

void ItemLooks::restore(const GameAddresses& at) {
    // The colors first: an object dressed as a Zexion item keeps its style until it is undressed
    // below, and its palette is then the game's already.
    if (colored() > 0) {
        BYTE* game = stand_ins_palette(at);
        BYTE* own = own_palette(at);
        for (int slot = 0; slot < kSlots; ++slot) {
            if (at.item_pool == nullptr) {
                colored_items_[slot] = {};
            } else {
                uncolor_live(at.item_pool + slot * obj::kItemSize, obj::kItemSize,
                             colored_items_[slot], game, own);
            }
        }
        for (int slot = 0; slot < kObjectSlots; ++slot) {
            if (at.object_pool == nullptr) {
                colored_flipped_[slot] = {};
            } else {
                uncolor_live(at.object_pool + slot * obj::kGeneralSize, obj::kGeneralSize,
                             colored_flipped_[slot], game, own);
            }
        }
    }
    flipped_slots_.clear();  // found again by the next scan
    frames_to_scan_ = 0;
    if (hidden_markers() > 0) {
        for (int slot = 0; slot < kObjectSlots; ++slot) {
            if (at.object_pool == nullptr) {
                hidden_[slot] = {};
            } else {
                unhide_live(at.object_pool + slot * obj::kGeneralSize, hidden_[slot]);
            }
        }
    }
    if (at.item_pool == nullptr || !dressed()) {
        for (Dressed& d : dressed_) d = {};
        for (Dressed& d : flipped_dressed_) d = {};
        return;
    }
    BYTE* base = item_frame_base(at);
    BYTE* standin_palette = stand_ins_palette(at);
    for (int slot = 0; slot < kSlots; ++slot) {
        undress_live(at.item_pool + slot * obj::kItemSize, obj::kItemSize, dressed_[slot], base,
                     standin_palette, false);
    }
    for (int slot = 0; slot < kObjectSlots && at.object_pool != nullptr; ++slot) {
        undress_live(at.object_pool + slot * obj::kGeneralSize, obj::kGeneralSize,
                     flipped_dressed_[slot], base, standin_palette, true);
    }
    if (at.object_pool == nullptr) flipped_dressed_.assign(kObjectSlots, {});
}

}  // namespace zap
