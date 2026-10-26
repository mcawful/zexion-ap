#include "files.h"

#include <cstring>

#include "game_mem.h"

namespace zap {

// A 3-wide D and I, then the game's own S and 4-wide K (as its KEYS draws them, 0x81 and
// 0x82), each letter one blank column from the next.
const char kDiskHeaderRows[2][8][9] = {
    {"........", "........", "##..###.", "#.#..#..", "#.#..#..", "#.#..#..", "##..###.",
     "........"},
    {"........", "........", ".##.#..#", "#...#.#.", ".#..##..", "..#.#.#.", "##..#..#",
     "........"},
};

namespace {

// An entry's save block (its slot's first block, laid out as slot::) and its slot
// (docs/re/memory.md, "The file select").
constexpr ptrdiff_t kChildBlock = 0x1F8, kChildSlot = 0x230;
// The entry (read live on B1, 2026-10-24): 145 glyphs, 5 rows of 29.
constexpr int32_t kEntryGlyphs = 145, kEntryColumns = 29, kEntries = 3;
constexpr int kRow2 = 2 * kEntryColumns;
// The keys' block, columns 27 and 28 of rows 2 to 4: the header, then the circles.
constexpr int kKeysHeader[2] = {kRow2 + 27, kRow2 + 28};
constexpr int kKeyCircles[4] = {114, 115, 143, 144};
constexpr int kBlock[6] = {kKeysHeader[0], kKeysHeader[1], kKeyCircles[0],
                           kKeyCircles[1], kKeyCircles[2], kKeyCircles[3]};
constexpr int32_t kHeaderK = 0x81, kHeaderS = 0x82, kHollow = 0x83, kFilled = 0x84;
constexpr int32_t kBlank = 0;
// A glyph's place: its y (sprite::kY), and the top and bottom of its box, set by the label's
// layout (0xA79E0: each glyph at the label's place plus 8 a column and 8 a row).
constexpr ptrdiff_t kBoxTop = 0x130, kBoxBottom = 0x140;
constexpr double kRowHeight = 8.0;
// The count's glyphs drop 5 px to center on the items bracket beside them (0xD9 over 0xDA, rows
// 3 and 4: center 7.5 below row 3's top; a digit's 3, so 4.5; 5 chosen in game).
constexpr int kCountGlyphs[2] = {kKeyCircles[0], kKeyCircles[1]};
constexpr double kCountDrop = 5.0;
constexpr intptr_t kFill = 0xF;

// One of the entry handlers: the file select's, COPY FILE's or DELETE FILE's.
bool is_entry_fn(const GameAddresses& a, void* fn) {
    for (BYTE* f : {a.file_entry_fn, a.copy_entry_fn, a.delete_entry_fn}) {
        if (f != nullptr && fn == f) return true;
    }
    return false;
}

// The entry's label glyphs, if it is an entry as traced.
bool entry_glyphs(BYTE* entry, BYTE* (&glyphs)[kEntryGlyphs]) {
    int32_t released = 1, count = 0, columns = 0;
    BYTE* label = nullptr;
    BYTE** list = nullptr;
    if (!read(entry, sprite::kReleased, released) || released != 0 ||
        !read(entry, widget::kLabel, label) ||
        !read(label, label::kLength, count) || count != kEntryGlyphs ||
        !read(label, label::kColumns, columns) || columns != kEntryColumns ||
        !read(label, label::kGlyphs, list) || !safe_read(list, glyphs, sizeof(glyphs))) {
        return false;
    }
    return true;
}

bool shows_disk(BYTE* (&g)[kEntryGlyphs]) {
    return frame_of(g[kKeysHeader[0]]) == kDiskHeaderGlyphs[0] &&
           frame_of(g[kKeysHeader[1]]) == kDiskHeaderGlyphs[1];
}

int32_t block_frame(BYTE* (&g)[kEntryGlyphs], int k) { return frame_of(g[kBlock[k]]); }

// Whether the keys' block still shows what the helper wrote (no fill since).
bool still_written(BYTE* (&g)[kEntryGlyphs], const int32_t (&shown)[6]) {
    for (int k = 0; k < 6; ++k) {
        if (block_frame(g, k) != shown[k]) return false;
    }
    return true;
}

// Moves the count's glyphs between their row (where the game's layout puts them) and kCountDrop
// below it: down while `dropped`, else back up. Their row is found from the glyphs above and
// below them (85 and 143), which the helper never moves; a glyph at neither place is left alone.
void place_count(BYTE* (&g)[kEntryGlyphs], bool dropped) {
    double above = 0, below = 0;
    if (!read(g[kKeysHeader[0]], sprite::kY, above) ||
        !read(g[kKeyCircles[2]], sprite::kY, below) ||
        below - above != 2 * kRowHeight) {
        return;
    }
    double row = above + kRowHeight;
    double from = dropped ? row : row + kCountDrop, to = dropped ? row + kCountDrop : row;
    for (int i : kCountGlyphs) {
        double y = 0, top = 0, bottom = 0;
        if (!read(g[i], sprite::kY, y) || y != from || !read(g[i], kBoxTop, top) ||
            !read(g[i], kBoxBottom, bottom) || !writable(g[i], sprite::kSize)) {
            continue;
        }
        put(g[i], sprite::kY, to);
        put(g[i], kBoxTop, top + (to - from));
        put(g[i], kBoxBottom, bottom + (to - from));
    }
}

}  // namespace

std::string file_disk_digits(int value) {
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    return std::string(1, static_cast<char>('0' + value / 10)) +
           static_cast<char>('0' + value % 10);
}

void* FileDisks::original(void* widget) const {
    auto it = wrapped_.find(static_cast<BYTE*>(widget));
    return it == wrapped_.end() ? nullptr : it->second.original;
}

void FileDisks::on_event(const GameAddresses&, void* widget, intptr_t event) {
    // The game's handlers take the event as 32 bits (cmp edx at 0xE1F3F).
    if (static_cast<int32_t>(event) != kFill || !on()) return;
    auto it = wrapped_.find(static_cast<BYTE*>(widget));
    if (it != wrapped_.end()) update(it->first, it->second);
}

void FileDisks::watch(const GameAddresses& a) {
    // Entries whose menu has closed went with it.
    for (auto it = wrapped_.begin(); it != wrapped_.end();) {
        it = is_open(a.current_menu, it->second.menu) ? std::next(it) : wrapped_.erase(it);
    }
    if (!on()) {
        restore(a);
        return;
    }
    BYTE* menu = pointer_at(a.current_menu);
    for (int m = 0; m < kMaxMenus && menu != nullptr; ++m) {
        BYTE* panel = panel_at(menu, 0);
        void* handler = nullptr;
        int32_t capacity = 0;
        BYTE** children = nullptr;
        BYTE* entries[kEntries] = {};
        if (read(panel, panel::kHandler, handler) && handler == a.file_panel_fn &&
            read(panel, panel::kCapacity, capacity) && capacity >= kEntries &&
            read(panel, panel::kChildren, children) &&
            safe_read(children, entries, sizeof(entries))) {
            for (BYTE* entry : entries) {
                BYTE* glyphs[kEntryGlyphs] = {};
                void* h = nullptr;
                int32_t slot = 0;
                if (!entry_glyphs(entry, glyphs) || !read(entry, widget::kHandler, h) ||
                    !read(entry, kChildSlot, slot) || slot < 1 || slot > slot::kCount) {
                    continue;
                }
                if (h != a.hooks.file_entry && is_entry_fn(a, h) && a.hooks.file_entry != nullptr &&
                    writable(entry + widget::kHandler, sizeof(void*))) {
                    wrapped_[entry] = Wrapped{menu, h};
                    put(entry, widget::kHandler, a.hooks.file_entry);
                    h = a.hooks.file_entry;
                }
                // Only the entries it has wrapped, whose state it keeps.
                auto it = wrapped_.find(entry);
                if (h == a.hooks.file_entry && it != wrapped_.end()) update(entry, it->second);
            }
        }
        if (!read(menu, menu::kBelow, menu)) break;
    }
}

void FileDisks::update(BYTE* entry, Wrapped& w) {
    BYTE* g[kEntryGlyphs] = {};
    if (!entry_glyphs(entry, g)) return;
    // A fill since the helper wrote the entry: the game's frames again.
    w.written = w.written && still_written(g, w.shown);
    if (!rewrite(entry, g, w)) put_back(g, w);  // not one to show: the game's frames
    place_count(g, w.written && shows_disk(g));
}

bool FileDisks::rewrite(BYTE* entry, BYTE* (&g)[kEntryGlyphs], Wrapped& w) {
    int32_t slot = 0;
    if (!read(entry, kChildSlot, slot)) return false;
    auto listed = counts_.find(slot);
    if (listed == counts_.end()) return false;
    const int shown = listed->second;
    BYTE* block = nullptr;
    uint8_t empty = slot::kEmptyMark;
    if (!read(entry, kChildBlock, block) || !read(block, slot::kEmpty, empty) ||
        empty == slot::kEmptyMark) {
        return false;
    }
    // What the block should read: DISK, then the count, then nothing; with no disk, nothing at
    // all, as a save with no key.
    std::string count = file_disk_digits(shown);
    int32_t want[6] = {kDiskHeaderGlyphs[0],
                       kDiskHeaderGlyphs[1],
                       static_cast<unsigned char>(count[0]),
                       static_cast<unsigned char>(count[1]),
                       kBlank,
                       kBlank};
    if (shown <= 0) {
        for (int32_t& f : want) f = kBlank;
    }
    if (w.written) {
        if (std::memcmp(w.shown, want, sizeof(want)) == 0) return true;  // as it should be
    } else {
        // Only over the game's frames: the KEYS header or none, the circles or none.
        int32_t k = block_frame(g, 0), s = block_frame(g, 1);
        if (!((k == kHeaderK && s == kHeaderS) || (k == kBlank && s == kBlank))) return false;
        for (int i = 2; i < 6; ++i) {
            int32_t f = block_frame(g, i);
            if (f != kHollow && f != kFilled && f != kBlank) return false;
        }
    }
    for (int i : kBlock) {
        if (!writable(g[i], sprite::kSize)) return false;
    }
    if (!w.written) {
        for (int k = 0; k < 6; ++k) w.game[k] = block_frame(g, k);
    }
    // Drawn as the game draws the KEYS header (glyph 85, one of the block, checked writable).
    const BYTE* like = g[kKeysHeader[0]];
    for (int k = 0; k < 6; ++k) {
        BYTE* cell = g[kBlock[k]];
        set_frame(cell, want[k]);
        look_like(cell, like);
        w.shown[k] = block_frame(g, k);
    }
    w.written = true;
    return true;
}

// The game's frames back over the helper's, while the entry still shows the helper's (the
// game's six glyphs all share glyph 85's palette, style and tint, which the helper copied).
void FileDisks::put_back(BYTE* (&g)[kEntryGlyphs], Wrapped& w) {
    if (!w.written) return;
    for (int i : kBlock) {
        if (!writable(g[i], sprite::kSize)) return;
    }
    for (int k = 0; k < 6; ++k) set_frame(g[kBlock[k]], w.game[k]);
    w.written = false;
}

void FileDisks::restore(const GameAddresses& a) {
    for (auto it = wrapped_.begin(); it != wrapped_.end();) {
        BYTE* entry = it->first;
        Wrapped& w = it->second;
        if (!is_open(a.current_menu, w.menu)) {
            it = wrapped_.erase(it);  // freed with its menu
            continue;
        }
        // The keys' block and the count's glyphs as the game left them.
        BYTE* g[kEntryGlyphs] = {};
        if (entry_glyphs(entry, g)) {
            w.written = w.written && still_written(g, w.shown);
            put_back(g, w);
            place_count(g, false);
        }
        void* h = nullptr;
        if (read(entry, widget::kHandler, h) && h == a.hooks.file_entry) {
            if (!writable(entry + widget::kHandler, sizeof(void*))) {
                ++it;  // still the helper's: kept, so an unload refuses
                continue;
            }
            put(entry, widget::kHandler, w.original);
        }
        it = wrapped_.erase(it);
    }
}

}  // namespace zap
