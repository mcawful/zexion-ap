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

// The world's AP item logo (apitem.cpp kLogo), in glyph_rgb's colors: k the outline, then the
// six circles, r y g b p o.
const char kApLogoRows[16][17] = {
    ".....krrrrk.....", ".....rrrrrr.....", ".kyyk.rrrr.kgkk.", "kyyyyy.rr.gggggk",
    "yyyyyykrrkgggggg", "yyyyyykrrkgggggg", "yy...yk..kg....g", ".kbbkk....kpppk.",
    "kbbbbb....pppppk", "bbbbbb.kk.pppppp", "bbbbbkooookppppp", "bbbb.oooooo.pppp",
    "kbbbkooooookpppk", ".kkkkooooookkkk.", ".....oooooo.....", ".....kooook.....",
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
constexpr int32_t kBlank = 0, kSpace = 0x20;
// A glyph's place: its y (sprite::kY), and the top and bottom of its box, set by the label's
// layout (0xA79E0: each glyph at the label's place plus 8 a column and 8 a row).
constexpr ptrdiff_t kBoxTop = 0x130, kBoxBottom = 0x140;
constexpr double kRowHeight = 8.0;
// The count's glyphs drop 5 px to center on the items bracket beside them (0xD9 over 0xDA, rows
// 3 and 4: center 7.5 below row 3's top; a digit's 3, so 4.5; 5 chosen in game).
constexpr int kCountGlyphs[2] = {kKeyCircles[0], kKeyCircles[1]};
constexpr double kCountDrop = 5.0;
constexpr intptr_t kFill = 0xF;
// The logo's cells: right of FILE NN (columns 8 to 14 of row 0), columns 16 and 17 of rows 0
// and 1, blank in every entry read live (B1, 2026-10-26); glyphs 15 and 44, beside them in rows
// 0 and 1, which the helper never moves, give the rows. The logo is raised 4 px, so that its 16
// rows center on FILE NN's 8 and stay 4 px clear of the row below.
constexpr int kLogoCells[4] = {16, 17, kEntryColumns + 16, kEntryColumns + 17};
constexpr int32_t kLogoFrames[4] = {kApLogoGlyphs[0], kApLogoGlyphs[1], kApLogoGlyphs[2],
                                    kApLogoGlyphs[3]};
constexpr int kLogoRowGlyphs[2] = {15, kEntryColumns + 15};
constexpr double kLogoRaise = 4.0;
// A glyph shows its own colors in palette 26 (0x368A0: the fixed pipeline, the texture times
// the one color its style picks) at style 0x15 (white); the shader palettes (2, as the keys')
// would not keep them (docs/re/memory.md, "The file select").
constexpr int16_t kOwnColorsPalette = 26;
constexpr int32_t kWhiteStyle = 0x15;

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

// Whether `cells` show `frames` (what the helper wrote, with no fill since).
template <size_t N>
bool shows(BYTE* (&g)[kEntryGlyphs], const int (&cells)[N], const int32_t (&frames)[N]) {
    for (size_t k = 0; k < N; ++k) {
        if (frame_of(g[cells[k]]) != frames[k]) return false;
    }
    return true;
}

// `frames` written into `cells`, if every one is writable.
template <size_t N>
bool put_frames(BYTE* (&g)[kEntryGlyphs], const int (&cells)[N], const int32_t (&frames)[N]) {
    for (int i : cells) {
        if (!writable(g[i], sprite::kSize)) return false;
    }
    for (size_t k = 0; k < N; ++k) set_frame(g[cells[k]], frames[k]);
    return true;
}

// Moves a glyph between `row` (where the game's layout puts it) and `shift` from it: there while
// `moved`, else back. A glyph at neither place is left alone.
void shift_glyph(BYTE* glyph, double row, double shift, bool moved) {
    double from = moved ? row : row + shift, to = moved ? row + shift : row;
    double y = 0, top = 0, bottom = 0;
    if (!read(glyph, sprite::kY, y) || y != from || !read(glyph, kBoxTop, top) ||
        !read(glyph, kBoxBottom, bottom) || !writable(glyph, sprite::kSize)) {
        return;
    }
    put(glyph, sprite::kY, to);
    put(glyph, kBoxTop, top + (to - from));
    put(glyph, kBoxBottom, bottom + (to - from));
}

// Moves the count's glyphs between their row and kCountDrop below it: down while `dropped`, else
// back up. Their row is found from the glyphs above and below them (85 and 143), which the helper
// never moves.
void place_count(BYTE* (&g)[kEntryGlyphs], bool dropped) {
    double above = 0, below = 0;
    if (!read(g[kKeysHeader[0]], sprite::kY, above) ||
        !read(g[kKeyCircles[2]], sprite::kY, below) ||
        below - above != 2 * kRowHeight) {
        return;
    }
    for (int i : kCountGlyphs) shift_glyph(g[i], above + kRowHeight, kCountDrop, dropped);
}

// Moves the logo's glyphs between their rows and kLogoRaise above them: up while `raised`, else
// back down.
void place_logo(BYTE* (&g)[kEntryGlyphs], bool raised) {
    double row0 = 0, row1 = 0;
    if (!read(g[kLogoRowGlyphs[0]], sprite::kY, row0) ||
        !read(g[kLogoRowGlyphs[1]], sprite::kY, row1) || row1 - row0 != kRowHeight) {
        return;
    }
    for (int k = 0; k < 4; ++k) {
        shift_glyph(g[kLogoCells[k]], k < 2 ? row0 : row1, -kLogoRaise, raised);
    }
}

// Whether the entry shows a save (its slot's first block not empty), and with `randomizer` a
// randomizer one (+0x9400 = 1, read as the game holds it now: a copy over the slot shows at once).
bool in_use(BYTE* entry, bool randomizer = false) {
    BYTE* block = nullptr;
    uint8_t empty = slot::kEmptyMark;
    uint16_t marker = 0;
    return read(entry, kChildBlock, block) && read(block, slot::kEmpty, empty) &&
           empty != slot::kEmptyMark &&
           (!randomizer || (read(block, slot::kRandoMarker, marker) && marker == 1));
}

}  // namespace

std::string file_disk_digits(int value) {
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    return std::string(1, static_cast<char>('0' + value / 10)) +
           static_cast<char>('0' + value % 10);
}

void* FileEntries::original(void* widget) const {
    auto it = wrapped_.find(static_cast<BYTE*>(widget));
    return it == wrapped_.end() ? nullptr : it->second.original;
}

void FileEntries::on_event(const GameAddresses&, void* widget, intptr_t event) {
    // The game's handlers take the event as 32 bits (cmp edx at 0xE1F3F).
    if (static_cast<int32_t>(event) != kFill || !on()) return;
    auto it = wrapped_.find(static_cast<BYTE*>(widget));
    if (it != wrapped_.end()) update(it->first, it->second);
}

void FileEntries::watch(const GameAddresses& a) {
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

void FileEntries::update(BYTE* entry, Wrapped& w) {
    BYTE* g[kEntryGlyphs] = {};
    if (!entry_glyphs(entry, g)) return;
    forget_fills(g, w);
    // Each part where it is not one to show: the game's frames.
    if (!rewrite(entry, g, w)) put_back(g, w);
    place_count(g, w.keys_written && shows_disk(g));
    if (!mark(entry, g, w)) unmark(g, w);
    place_logo(g, w.marked);
}

void FileEntries::forget_fills(BYTE* (&g)[kEntryGlyphs], Wrapped& w) {
    // A fill since the helper wrote a part: the game's frames there again.
    w.keys_written = w.keys_written && shows(g, kBlock, w.keys_shown);
    w.marked = w.marked && shows(g, kLogoCells, kLogoFrames);
}

bool FileEntries::mark(BYTE* entry, BYTE* (&g)[kEntryGlyphs], Wrapped& w) {
    int32_t slot = 0;
    if (!read(entry, kChildSlot, slot) || ap_slots_.count(slot) == 0 || !in_use(entry, true)) {
        return false;
    }
    if (w.marked) return true;
    // Only over blank cells that keep the logo's colors, each of which set_frame can set (so
    // no entry is left half marked).
    for (int i : kLogoCells) {
        int32_t frame = frame_of(g[i]), style = 0, frames = 0;
        int16_t palette = 0;
        BYTE* list = nullptr;
        if ((frame != kBlank && frame != kSpace) ||
            !read(g[i], sprite::kFrameList, list) || list == nullptr ||
            !read(g[i], sprite::kPaletteNumber, palette) || palette != kOwnColorsPalette ||
            !read(g[i], sprite::kStyle, style) || style != kWhiteStyle ||
            !read(g[i], sprite::kFrameCount, frames) || frames <= kApLogoGlyphs[3] ||
            !writable(g[i], sprite::kSize)) {
            return false;
        }
    }
    for (int k = 0; k < 4; ++k) w.logo_game[k] = frame_of(g[kLogoCells[k]]);
    w.marked = put_frames(g, kLogoCells, kLogoFrames);
    return w.marked;
}

// The game's frames back in the logo's cells, while they still show the logo.
void FileEntries::unmark(BYTE* (&g)[kEntryGlyphs], Wrapped& w) {
    if (w.marked && put_frames(g, kLogoCells, w.logo_game)) w.marked = false;
}

bool FileEntries::rewrite(BYTE* entry, BYTE* (&g)[kEntryGlyphs], Wrapped& w) {
    int32_t slot = 0;
    if (!read(entry, kChildSlot, slot)) return false;
    auto listed = counts_.find(slot);
    if (listed == counts_.end() || !in_use(entry)) return false;
    const int shown = listed->second;
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
    if (w.keys_written) {
        if (std::memcmp(w.keys_shown, want, sizeof(want)) == 0) return true;  // as it should be
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
    if (!w.keys_written) {
        for (int k = 0; k < 6; ++k) w.keys_game[k] = block_frame(g, k);
    }
    // Drawn as the game draws the KEYS header (glyph 85, one of the block, checked writable).
    const BYTE* like = g[kKeysHeader[0]];
    for (int k = 0; k < 6; ++k) {
        BYTE* cell = g[kBlock[k]];
        set_frame(cell, want[k]);
        look_like(cell, like);
        w.keys_shown[k] = block_frame(g, k);
    }
    w.keys_written = true;
    return true;
}

// The game's frames back over the helper's, while the entry still shows the helper's (the
// game's six glyphs all share glyph 85's palette, style and tint, which the helper copied).
void FileEntries::put_back(BYTE* (&g)[kEntryGlyphs], Wrapped& w) {
    if (w.keys_written && put_frames(g, kBlock, w.keys_game)) w.keys_written = false;
}

void FileEntries::restore(const GameAddresses& a) {
    for (auto it = wrapped_.begin(); it != wrapped_.end();) {
        BYTE* entry = it->first;
        Wrapped& w = it->second;
        if (!is_open(a.current_menu, w.menu)) {
            it = wrapped_.erase(it);  // freed with its menu
            continue;
        }
        // The keys' block, the logo's cells and their glyphs' rows as the game left them.
        BYTE* g[kEntryGlyphs] = {};
        if (entry_glyphs(entry, g)) {
            forget_fills(g, w);
            put_back(g, w);
            place_count(g, false);
            unmark(g, w);
            place_logo(g, false);
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
