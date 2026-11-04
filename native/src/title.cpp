#include "title.h"

#include <algorithm>
#include <cstring>

#include "game_mem.h"
#include "text.h"  // in_capitals
#include "translation.h"

namespace zap {

const char kTitleCredit[] = "C2026 MCAWFUL";
const char kTitleHint[] = "Start the AP client to play a seed";

namespace {

// The helper's lines and where they go: centered on the 320-pixel screen, 8 pixels a character,
// 12 apart in the band below the planet: the hint, then the version, then the credit.
constexpr int kLines = 3;
constexpr int kModLine = 0, kCredit = 1, kHint = 2;
constexpr int kLineY[kLines] = {202, 214, 190};
int line_x(size_t length) { return 160 - 4 * static_cast<int>(length); }

// The draw list (docs/re/memory.md, "The title screen").
constexpr ptrdiff_t kListHead = 0x18, kListTail = 0x20;
constexpr ptrdiff_t kOwner = 0x00, kNext = 0x10, kPrevious = 0x18;
constexpr int kMaxObjects = 4096;
// A text object's own fields (0x92300): past label:: (game_layout.h), 0x1E4 and 0x1E8 (the
// count shown and the size are hdlabel::'s, translation.h), and its tag (0x178, 0x180).
constexpr ptrdiff_t kTextHint = 0x1E4, kTextFlag = 0x1E8;
constexpr ptrdiff_t kTag = 0x178, kTagNumber = 0x180;
constexpr int32_t kTextColumnsAll = 32;
constexpr int32_t kCopyright = 0xA9;  // the font's ©
// The game's line: © then "2025 GALLANT LEAF" (18 glyphs); the year is not checked.
constexpr char kGameLineEnd[] = "GALLANT LEAF";
constexpr int32_t kGameLineLength = 18;
constexpr int kLookFrames = 8;
constexpr int kMaxGlyphs = 64;

// A text object's glyphs, if `object` reads as one of `length` glyphs in one row (the game's
// line has 32 columns; the helper's are at least as wide as they are long).
bool glyphs_of(BYTE* object, int32_t length, BYTE** glyphs) {
    int32_t columns = 0;
    return length <= kMaxGlyphs && read(object, label::kColumns, columns) && columns >= length &&
           label_glyphs(object, length, glyphs);
}

// Whether the text's glyphs show `text` (glyph 0 the © instead when `copyright`).
bool shows(BYTE* object, std::string_view text, bool copyright) {
    BYTE* g[kMaxGlyphs] = {};
    const size_t from = copyright ? 1 : 0;
    return glyphs_of(object, static_cast<int32_t>(text.size()), g) &&
           (!copyright || frame_of(g[0]) == kCopyright) && glyphs_show(g + from, text.substr(from));
}

bool is_game_line(BYTE* object) {
    BYTE* g[kGameLineLength] = {};
    const size_t end = sizeof(kGameLineEnd) - 1;
    return glyphs_of(object, kGameLineLength, g) && frame_of(g[0]) == kCopyright &&
           glyphs_show(g + kGameLineLength - end, kGameLineEnd);
}

struct Look {
    BYTE* list = nullptr;  // the draw list's holder
    BYTE* game_line = nullptr;
    BYTE* made[kLines] = {};  // the helper's lines, by kLines' order
};

Look look(BYTE* draw_layer, const std::string (&texts)[kLines]) {
    Look l;
    BYTE* object = nullptr;
    l.list = pointer_at(draw_layer);
    if (l.list == nullptr || !read(l.list, kListHead, object)) {
        return l;
    }
    for (int i = 0; i < kMaxObjects && object != nullptr; ++i) {
        BYTE* owner = nullptr;
        if (!read(object, kOwner, owner) || owner != l.list) break;
        if (l.game_line == nullptr && is_game_line(object)) {
            l.game_line = object;
        } else {
            for (int k = 0; k < kLines; ++k) {
                if (l.made[k] == nullptr && shows(object, texts[k], k == kCredit)) {
                    l.made[k] = object;
                    break;
                }
            }
        }
        if (!read(object, kNext, object)) break;
    }
    return l;
}

// Whether the line shows its translated text (built, its pixel glyphs hidden).
bool shows_translated(BYTE* object) {
    uint8_t built = 0;
    int32_t count = 0;
    return read(object, hdlabel::kBuilt, built) && built != 0 &&
           read(object, hdlabel::kCount, count) && count > 0 && !pixel_glyphs_shown(object);
}

// A glyph shown or hidden, where it reads otherwise and can be written.
void set_shown(BYTE* g, bool visible) {
    uint8_t now = 0;
    if (g != nullptr && read(g, sprite::kVisible, now) && now != (visible ? 1 : 0) &&
        writable(g, sprite::kSize)) {
        g[sprite::kVisible] = visible ? 1 : 0;
    }
}

void show_translated(BYTE* object, bool visible) {
    int32_t count = 0, capacity = 0;
    BYTE** list = nullptr;
    if (!read(object, hdlabel::kCapacity, capacity) ||
        !translated_list(object, capacity, count, list)) {
        return;
    }
    for (int32_t i = 0; i < count; ++i) set_shown(glyph_at(list, i), visible);
}

// The line in the translation's font (`hd`: built each time it is not shown so, as with the
// translation loaded now, its pixel glyphs hidden), or in pixels again.
void use_font(const GameAddresses& a, BYTE* object, const std::string& text, bool hd) {
    if (hd) {
        if (shows_translated(object)) return;
        if (rebuild_label(a, object, hd_text(text),
                          {.attached = true, .center = true, .first = true})) {
            show_translated_glyphs(object);
        }
    } else if (shows_translated(object)) {
        show_pixel_glyphs(object);
    }
}

void show(BYTE* object, size_t length, bool visible) {
    if (shows_translated(object)) {
        show_translated(object, visible);
        return;
    }
    BYTE* g[kMaxGlyphs] = {};
    if (!glyphs_of(object, static_cast<int32_t>(length), g)) return;
    for (size_t i = 0; i < length; ++i) set_shown(g[i], visible);
}

// Makes one line as the title routine makes the copyright line, styled as `model` (the game's
// line). Returns "" or why not.
std::string make_line(const GameAddresses& a, BYTE* list, BYTE* model, const std::string& text,
                      bool copyright, int y, BYTE*& made) {
    BYTE* mg[kGameLineLength] = {};
    BYTE* palette = nullptr;
    int16_t palette_number = 0;
    int32_t style = 0;
    BYTE* tag = nullptr;
    int16_t tag_number = 0;
    // Glyph 1, a plain character, carries the palette and style the routine gives them all.
    if (!glyphs_of(model, kGameLineLength, mg) || !read(mg[1], sprite::kPalette, palette) ||
        !read(mg[1], sprite::kPaletteNumber, palette_number) ||
        !read(mg[1], sprite::kStyle, style) ||
        !read(model, kTag, tag) || !read(model, kTagNumber, tag_number)) {
        return "the game's line does not read as traced";
    }
    const int32_t length = static_cast<int32_t>(text.size());
    using New = BYTE* (*)(const char*, int, int);
    using Place = void (*)(void*, int, int);
    using Link = void (*)(void*);
    // One row: the game's 32 columns, or the line's length if longer (the row wraps at its
    // columns: docs/re/memory.md, "The title screen").
    const int32_t columns = (std::max)(length, kTextColumnsAll);
    BYTE* t = reinterpret_cast<New>(a.text_new_fn)(text.c_str(), length, columns);
    BYTE* g[kMaxGlyphs] = {};
    BYTE* owner = reinterpret_cast<BYTE*>(1);
    if (t == nullptr || !writable(t, hdlabel::kSize) || !glyphs_of(t, length, g) ||
        !read(t, kOwner, owner)) {
        return "no text object";  // nothing in the list yet: the game's heap keeps it
    }
    put<int32_t>(t, kTextHint, -1);
    reinterpret_cast<Place>(a.text_place_fn)(t, line_x(text.size()), y);
    if (owner == nullptr) {
        // At the list's tail, as at 0x93885.
        BYTE* tail = nullptr;
        if (!read(list, kListTail, tail) || !writable(list, kListTail + sizeof(BYTE*))) {
            return "the draw list is not writable";
        }
        if (tail != nullptr) {
            put(tail, kNext, t);
        } else {
            put(list, kListHead, t);
        }
        put(t, kPrevious, tail);
        put<BYTE*>(t, kNext, nullptr);
        put(list, kListTail, t);
        put(t, kOwner, list);
    }
    reinterpret_cast<Link>(a.text_link_fn)(t);
    put<int32_t>(t, hdlabel::kPixelShown, length);
    for (int32_t i = 0; i < length; ++i) {
        if (!writable(g[i], sprite::kSize)) continue;
        *(g[i] + sprite::kVisible) = 1;
        put(g[i], sprite::kPalette, palette);
        put(g[i], sprite::kPaletteNumber, palette_number);
        put(g[i], sprite::kStyle, style);
    }
    if (copyright) set_frame(g[0], kCopyright);
    put(t, kTag, tag);
    put(t, kTagNumber, tag_number);
    *(t + sprite::kVisible) = 0;
    put<int32_t>(t, kTextFlag, 0);
    made = t;
    return "";
}

}  // namespace

std::string TitleLines::watch(const GameAddresses& a, bool client, bool hd) {
    // A change of language looked at in the same frame call: no glyph built in a translation
    // no longer loaded stays shown.
    if (hd != hd_) frames_ = 0;
    hd_ = hd;
    if (mod_line_.empty() || frames_-- > 0) return "";
    // In English(HD) every frame call: a menu's builder hides the HD list's glyphs, the lines'
    // too (docs/re/memory.md, "Where a rebuilt label goes"); the title's draw list is short.
    frames_ = hd ? 0 : kLookFrames;
    const std::string texts[kLines] = {in_capitals(mod_line_), kTitleCredit,
                                       in_capitals(kTitleHint)};
    Look l = look(a.draw_layer, texts);
    if (l.game_line == nullptr) {
        // Any of the helper's lines still in the list hidden (the game frees them with its line).
        for (int k = 0; k < kLines; ++k) {
            if (l.made[k] != nullptr) show(l.made[k], texts[k].size(), false);
        }
        shown_ = false;
        last_error_.clear();  // a new title tries again
        return "";
    }
    // No menu open over the title (menu_state 0; unreadable taken as one).
    uint8_t menu = 0xFF;
    const bool no_menu = read(a.menu_state, 0, menu) && menu == 0;
    // Once a title: a line that failed is not made again until the game's line goes and comes.
    // Made only with no menu open: made under one, they would join the list over its objects.
    std::string why = last_error_;
    for (int k = 0; k < kLines && why.empty(); ++k) {
        if (l.made[k] == nullptr && no_menu) {
            why = make_line(a, l.list, l.game_line, texts[k], k == kCredit, kLineY[k], l.made[k]);
        }
    }
    last_error_ = why;
    // In English(HD), the version's line and the hint in its font; the credit in pixels.
    const std::string hd_texts[kLines] = {mod_line_, "", kTitleHint};
    for (int k : {kModLine, kHint}) {
        if (l.made[k] != nullptr) use_font(a, l.made[k], hd_texts[k], hd);
    }
    shown_ = l.made[kModLine] != nullptr && l.made[kCredit] != nullptr && l.made[kHint] != nullptr;
    // Shown while the game's © is (and again after a refused unload hid them), the hint only with
    // no client. Translated glyphs are drawn from the HD list, over any menu opened on the title,
    // so they show only with no menu open too (docs/re/memory.md, "The title screen").
    BYTE* g[kGameLineLength] = {};
    uint8_t visible = 0;  // the © unreadable: hidden
    if (!glyphs_of(l.game_line, kGameLineLength, g) || !read(g[0], sprite::kVisible, visible)) {
        visible = 0;
    }
    for (int k = 0; k < kLines; ++k) {
        if (l.made[k] == nullptr) continue;
        const bool shown = visible != 0 && (k != kHint || !client) &&
                           (no_menu || !shows_translated(l.made[k]));
        show(l.made[k], texts[k].size(), shown);
    }
    return why;
}

void TitleLines::hide(const GameAddresses& a) {
    const std::string texts[kLines] = {in_capitals(mod_line_), kTitleCredit,
                                       in_capitals(kTitleHint)};
    Look l = look(a.draw_layer, texts);
    for (int k = 0; k < kLines; ++k) {
        if (l.made[k] != nullptr) show(l.made[k], texts[k].size(), false);
    }
    shown_ = false;
}

}  // namespace zap
