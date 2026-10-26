#include "files.h"

#include <cstring>
#include <sstream>

#include "game_mem.h"

namespace zap {

// A 3-wide D and I, then the game's own S and 4-wide K (as its KEYS draws them, 0x81 and
// 0x82), each letter one blank column from the next (the owner's pick, 2026-10-25).
const char kDiskHeaderRows[2][8][9] = {
    {"........", "........", "##..###.", "#.#..#..", "#.#..#..", "#.#..#..", "##..###.",
     "........"},
    {"........", "........", ".##.#..#", "#...#.#.", ".#..##..", "..#.#.#.", "##..#..#",
     "........"},
};

namespace {

// An entry's save block and slot (docs/re/memory.md, "The file select").
constexpr ptrdiff_t kChildBlock = 0x1F8, kChildSlot = 0x230;
// The entry (read live on B1, 2026-10-24): 145 glyphs, 5 rows of 29.
constexpr int32_t kEntryGlyphs = 145, kEntryColumns = 29, kEntries = 3, kSlots = 30;
constexpr int kRow2 = 2 * kEntryColumns;
// The keys' block, columns 27 and 28 of rows 2 to 4: the header, then the circles.
constexpr int kKeysHeader[2] = {kRow2 + 27, kRow2 + 28};
constexpr int kKeyCircles[4] = {114, 115, 143, 144};
constexpr int kBlock[6] = {kKeysHeader[0], kKeysHeader[1], 114, 115, 143, 144};
constexpr int32_t kHeaderK = 0x81, kHeaderS = 0x82, kHollow = 0x83, kFilled = 0x84;
constexpr int32_t kBlank = 0;
// The save block an entry shows (the slot's first block, file offset - 2).
constexpr ptrdiff_t kBlockEmpty = 0x935E;
constexpr uint8_t kEmptySlot = 0xFF;
// A glyph's place: its y (sprite::kY), and the top and bottom of its box, set by the label's
// layout (0xA79E0: each glyph at the label's place plus 8 a column and 8 a row).
constexpr ptrdiff_t kBoxTop = 0x130, kBoxBottom = 0x140;
constexpr double kRowHeight = 8.0;
// The count's two glyphs, moved down from their row to center on the items bracket beside
// them (the owner's ask): the bracket (0xD9 over 0xDA, rows 3 and 4) is opaque over all 16
// pixel rows, center 7.5 below row 3's top; a digit's 7 pixel rows (0 to 6), center 3, so 4.5:
// 5, a pixel lower than 4, as the owner chose in game.
constexpr int kCountGlyphs[2] = {114, 115};
constexpr double kCountDrop = 5.0;
constexpr intptr_t kFill = 0xF;

bool is_entry_fn(const FileAddresses& a, void* fn) {
    for (void* f : a.entry_fns) {
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

void FileDisks::set(int count, int required, std::set<int> slots) {
    (void)required;  // the number needed is not shown (the owner's layout)
    counts_.clear();
    for (int slot : slots) counts_[slot] = count;
}

void* FileDisks::original(void* widget) const {
    auto it = wrapped_.find(static_cast<BYTE*>(widget));
    return it == wrapped_.end() ? nullptr : it->second.original;
}

void FileDisks::on_event(const FileAddresses&, void* widget, intptr_t event) {
    // The game's handlers take the event as 32 bits (cmp edx at 0xE1F3F).
    if (static_cast<int32_t>(event) != kFill || !on()) return;
    auto it = wrapped_.find(static_cast<BYTE*>(widget));
    if (it != wrapped_.end()) update(it->first, it->second);
}

void FileDisks::watch(const FileAddresses& a) {
    // Entries whose menu has closed went with it.
    for (auto it = wrapped_.begin(); it != wrapped_.end();) {
        it = is_open(a.current_menu, it->second.menu) ? std::next(it) : wrapped_.erase(it);
    }
    if (!on()) {
        restore(a);
        return;
    }
    BYTE* menu = nullptr;
    if (!read(a.current_menu, 0, menu)) return;
    for (int m = 0; m < kMaxMenus && menu != nullptr; ++m) {
        BYTE** panels = nullptr;
        BYTE* panel = nullptr;
        void* handler = nullptr;
        int32_t capacity = 0;
        BYTE** children = nullptr;
        BYTE* entries[kEntries] = {};
        if (read(menu, 0, panels) && read(reinterpret_cast<BYTE*>(panels), 0, panel) &&
            read(panel, panel::kHandler, handler) && handler == a.file_panel_fn &&
            read(panel, panel::kCapacity, capacity) && capacity >= kEntries &&
            read(panel, panel::kChildren, children) &&
            safe_read(children, entries, sizeof(entries))) {
            for (BYTE* entry : entries) {
                BYTE* glyphs[kEntryGlyphs] = {};
                void* h = nullptr;
                int32_t slot = 0;
                if (!entry_glyphs(entry, glyphs) || !read(entry, widget::kHandler, h) ||
                    !read(entry, kChildSlot, slot) || slot < 1 || slot > kSlots) {
                    continue;
                }
                if (h != a.hook && is_entry_fn(a, h) && a.hook != nullptr &&
                    writable(entry + widget::kHandler, sizeof(void*))) {
                    wrapped_[entry] = Wrapped{menu, h};
                    put(entry, widget::kHandler, a.hook);
                    h = a.hook;
                }
                // Only the entries it has wrapped, whose state it keeps.
                auto it = wrapped_.find(entry);
                if (h == a.hook && it != wrapped_.end()) update(entry, it->second);
            }
        }
        if (!read(menu, menu::kBelow, menu)) break;
    }
}

void FileDisks::update(BYTE* entry, Wrapped& w) {
    BYTE* g[kEntryGlyphs] = {};
    if (!entry_glyphs(entry, g)) return;
    // A fill since the helper wrote the entry: the game's frames again.
    if (w.written) {
        for (int k = 0; k < 6; ++k) {
            if (block_frame(g, k) != w.shown[k]) w.written = false;
        }
    }
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
    uint8_t empty = kEmptySlot;
    if (!read(entry, kChildBlock, block) || !read(block, kBlockEmpty, empty) ||
        empty == kEmptySlot) {
        return false;
    }
    // What the block should read: DISK, then the count, then nothing; with no disk, nothing at
    // all, as a save with no key (the owner's ask).
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
    // Drawn as the game draws the KEYS header (glyph 85): its palette, style and tint.
    BYTE* like = g[kKeysHeader[0]];
    BYTE look[sprite::kTintSize];
    void* palette = nullptr;
    int16_t number = 0;
    int32_t style = 0;
    if (!read(like, sprite::kPalette, palette) || !read(like, sprite::kPaletteNumber, number) ||
        !read(like, sprite::kStyle, style) ||
        !safe_read(like + sprite::kTint, look, sizeof(look))) {
        return false;
    }
    if (!w.written) {
        for (int k = 0; k < 6; ++k) w.game[k] = block_frame(g, k);
    }
    for (int k = 0; k < 6; ++k) {
        BYTE* cell = g[kBlock[k]];
        set_frame(cell, want[k]);
        put(cell, sprite::kPalette, palette);
        put(cell, sprite::kPaletteNumber, number);
        put(cell, sprite::kStyle, style);
        std::memcpy(cell + sprite::kTint, look, sizeof(look));
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

void FileDisks::restore(const FileAddresses& a) {
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
            if (w.written) {
                for (int k = 0; k < 6; ++k) {
                    if (block_frame(g, k) != w.shown[k]) w.written = false;
                }
            }
            put_back(g, w);
            place_count(g, false);
        }
        void* h = nullptr;
        if (read(entry, widget::kHandler, h) && h == a.hook) {
            if (!writable(entry + widget::kHandler, sizeof(void*))) {
                ++it;  // still the helper's: kept, so an unload refuses
                continue;
            }
            put(entry, widget::kHandler, w.original);
        }
        it = wrapped_.erase(it);
    }
}

std::string parse_disk_slots(const std::string& request, int& count, int& required,
                             std::set<int>& slots) {
    count = required = 0;
    slots.clear();
    if (request == "disk_slots") return "";
    if (request.rfind("disk_slots ", 0) != 0) return "not a disk_slots request";
    std::istringstream words(request.substr(11));
    std::string list, extra;
    if (!(words >> count >> required >> list) || (words >> extra) || count < 0 || count > 255 ||
        required < 1 || required > 255) {
        count = required = 0;
        return "bad disk_slots request";
    }
    if (list == "-") return "";
    std::istringstream items(list);
    std::string item;
    while (std::getline(items, item, ',')) {
        if (item.empty() || item.size() > 2 ||
            item.find_first_not_of("0123456789") != std::string::npos) {
            slots.clear();
            return "bad disk_slots request";
        }
        int n = std::stoi(item);
        if (n < 1 || n > kSlots) {
            slots.clear();
            return "bad disk_slots request";
        }
        slots.insert(n);
    }
    return "";
}

}  // namespace zap
