#include "looks.h"

#include <cstring>
#include <vector>

#include "apitem.h"  // kStandInItem, kStandInProgression and their logo frames
#include "menu.h"    // safe_read

namespace zap {

namespace {

// The item object pool and its fields (docs/re/memory.md, "Live item objects"; B1).
constexpr size_t kSlotSize = 0x3B0;
constexpr ptrdiff_t kPalettePtr = 0x198, kPaletteNumber = 0x1A0, kFrameBase = 0x1A8;
constexpr ptrdiff_t kFrameCount = 0x1BC, kSpawn = 0x1D8, kRecord = 0x1E8;
constexpr ptrdiff_t kRow = 0x3AC, kColumn = 0x3AD;
constexpr ptrdiff_t kFrameSize = 0x38;          // one entry of a sprite set's frame list
constexpr ptrdiff_t kItemTemplate = 34 * 8;     // the item sprites' template: definition 34
constexpr int kStandInPalette = 8;              // Scrap Energy's and Scrap Chunk's
constexpr int kPalettes = 64;                   // records use 0 to 59
// Item records: the in-world frame, ticks per frame, frame count, palette.
constexpr ptrdiff_t kRecordSize = 100, kRecFrame = 0x02, kRecTicks = 0x04, kRecFrames = 0x06;
constexpr ptrdiff_t kRecPalette = 0x09;
constexpr int kRecords = 51;

template <typename T>
T field(const BYTE* p, ptrdiff_t offset) {
    T v;
    std::memcpy(&v, p + offset, sizeof(v));
    return v;
}

int logo_frame(int record) {
    return record == kStandInItem ? kStandInFrame : kStandInProgressionFrame;
}

BYTE* read_pointer(const BYTE* at) {
    BYTE* p = nullptr;
    if (at == nullptr || !safe_read(at, &p, sizeof(p))) return nullptr;
    return p;
}

}  // namespace

bool ItemLooks::dressed() const {
    for (const Dressed& d : dressed_) {
        if (d.spawn != 0) return true;
    }
    return false;
}

void ItemLooks::undress(BYTE* object, int slot, BYTE* base, BYTE* standin_palette) {
    Dressed& d = dressed_[slot];
    // Only the spawn this helper dressed, still a stand-in, still with the base it was given.
    if (field<int32_t>(object, kSpawn) == d.spawn && d.spawn != 0 &&
        (field<int16_t>(object, kRecord) == kStandInItem ||
         field<int16_t>(object, kRecord) == kStandInProgression) &&
        field<BYTE*>(object, kFrameBase) == d.base && base != nullptr &&
        standin_palette != nullptr) {
        std::memcpy(object + kFrameBase, &base, sizeof(base));
        std::memcpy(object + kPalettePtr, &standin_palette, sizeof(standin_palette));
        int16_t number = kStandInPalette;
        std::memcpy(object + kPaletteNumber, &number, sizeof(number));
    }
    d = {};
}

void ItemLooks::apply(const Places& at) {
    if (at.pool == nullptr) return;
    // The template every item object's frame-list base is copied from, the stand-ins' palette,
    // the records and the clock; without any of them, nothing is dressed.
    BYTE* templates = read_pointer(at.sprite_templates);
    BYTE* item_template = templates != nullptr ? read_pointer(templates + kItemTemplate) : nullptr;
    BYTE* base = item_template != nullptr ? read_pointer(item_template + kFrameBase) : nullptr;
    BYTE* standin_palette = read_pointer(at.palette_table + kStandInPalette * 8);
    BYTE* records = read_pointer(at.item_records);
    int64_t clock = 0;
    if (base == nullptr || standin_palette == nullptr || records == nullptr ||
        !safe_read(at.frame_clock, &clock, sizeof(clock)) || clock < 0) {
        return;
    }
    std::vector<BYTE> pool(kSlotSize * kSlots);
    if (!safe_read(at.pool, pool.data(), pool.size())) return;
    for (int slot = 0; slot < kSlots; ++slot) {
        const BYTE* seen = pool.data() + slot * kSlotSize;
        BYTE* object = at.pool + slot * kSlotSize;
        Dressed& d = dressed_[slot];
        int32_t spawn = field<int32_t>(seen, kSpawn);
        int16_t record = field<int16_t>(seen, kRecord);
        bool stand_in = record == kStandInItem || record == kStandInProgression;
        if (spawn == 0 || !stand_in || (d.spawn != 0 && d.spawn != spawn)) {
            d = {};  // gone, or another object now: nothing of ours to put back
            if (spawn == 0 || !stand_in) continue;
        }
        uint8_t row = seen[kRow], column = seen[kColumn];
        auto it = row < 54 && column < 54 ? items_.find(row * 54 + column) : items_.end();
        if (it == items_.end() || it->second < 0 || it->second >= kRecords) {
            if (d.spawn != 0) undress(object, slot, base, standin_palette);
            continue;
        }
        // An object this helper has not dressed must still have the template's base and the
        // stand-ins' palette; one it dressed, the base it was given.
        BYTE* now_base = field<BYTE*>(seen, kFrameBase);
        if (d.spawn == 0 ? (now_base != base || field<BYTE*>(seen, kPalettePtr) != standin_palette)
                         : now_base != d.base) {
            continue;
        }
        BYTE record_bytes[kRecordSize];
        if (!safe_read(records + it->second * kRecordSize, record_bytes, sizeof(record_bytes))) {
            continue;
        }
        int16_t first = field<int16_t>(record_bytes, kRecFrame);
        int16_t ticks = field<int16_t>(record_bytes, kRecTicks);
        int16_t frames = field<int16_t>(record_bytes, kRecFrames);
        int palette = static_cast<int8_t>(record_bytes[kRecPalette]);
        int32_t count = field<int32_t>(seen, kFrameCount);
        if (ticks <= 0 || frames <= 0 || first < 0 || first + frames > count || palette < 0 ||
            palette >= kPalettes) {
            continue;
        }
        BYTE* item_palette = read_pointer(at.palette_table + palette * 8);
        if (item_palette == nullptr) continue;
        int frame = first + static_cast<int>((clock / ticks) % frames);
        BYTE* new_base = base + static_cast<ptrdiff_t>(frame - logo_frame(record)) * kFrameSize;
        if (now_base != new_base) std::memcpy(object + kFrameBase, &new_base, sizeof(new_base));
        if (field<BYTE*>(seen, kPalettePtr) != item_palette) {
            std::memcpy(object + kPalettePtr, &item_palette, sizeof(item_palette));
            int16_t number = static_cast<int16_t>(palette);
            std::memcpy(object + kPaletteNumber, &number, sizeof(number));
        }
        d = {spawn, new_base};
    }
}

void ItemLooks::restore(const Places& at) {
    if (at.pool == nullptr || !dressed()) {
        for (Dressed& d : dressed_) d = {};
        return;
    }
    BYTE* templates = read_pointer(at.sprite_templates);
    BYTE* item_template = templates != nullptr ? read_pointer(templates + kItemTemplate) : nullptr;
    BYTE* base = item_template != nullptr ? read_pointer(item_template + kFrameBase) : nullptr;
    BYTE* standin_palette = read_pointer(at.palette_table + kStandInPalette * 8);
    for (int slot = 0; slot < kSlots; ++slot) {
        if (dressed_[slot].spawn == 0) continue;
        BYTE* object = at.pool + slot * kSlotSize;
        BYTE probe[kSlotSize];
        if (!safe_read(object, probe, sizeof(probe))) {
            dressed_[slot] = {};
            continue;
        }
        undress(object, slot, base, standin_palette);
    }
}

}  // namespace zap
