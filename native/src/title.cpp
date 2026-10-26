#include "title.h"

#include <algorithm>
#include <cstring>

#include "game_mem.h"

namespace zap {

const char kTitleCredit[] = "C2026 MCAWFUL";
const char kTitleHint[] = "START THE AP CLIENT TO PLAY A SEED";

std::string title_mod_line(const std::string& version) { return "ARCHIPELAGO MOD " + version; }

int title_line_x(size_t length) { return 160 - 4 * static_cast<int>(length); }

namespace {

// The draw list (from disassembly, B1: 0x93885, 0xA8620).
constexpr ptrdiff_t kListHead = 0x18, kListTail = 0x20;
constexpr ptrdiff_t kOwner = 0x00, kNext = 0x10, kPrevious = 0x18;
constexpr int kMaxObjects = 4096;
// A text object (0x92300), past its label fields (game_layout.h): the count shown and its tag.
constexpr ptrdiff_t kTextHint = 0x1E4, kTextFlag = 0x1E8, kTextShown = 0x1EC;
constexpr ptrdiff_t kTag = 0x178, kTagNumber = 0x180;
constexpr size_t kTextSize = 0x268;
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
    int32_t n = 0, columns = 0;
    BYTE** list = nullptr;
    return length > 0 && length <= kMaxGlyphs && read(object, label::kLength, n) && n == length &&
           read(object, label::kColumns, columns) && columns >= length &&
           read(object, label::kGlyphs, list) && list != nullptr &&
           safe_read(list, glyphs, sizeof(BYTE*) * static_cast<size_t>(length));
}

// Whether the text's glyphs show `text` (glyph 0 the © when `copyright`).
bool shows(BYTE* object, const std::string& text, bool copyright) {
    BYTE* g[kMaxGlyphs] = {};
    if (!glyphs_of(object, static_cast<int32_t>(text.size()), g)) return false;
    for (size_t i = 0; i < text.size(); ++i) {
        int32_t want = copyright && i == 0 ? kCopyright : static_cast<uint8_t>(text[i]);
        if (frame_of(g[i]) != want) return false;
    }
    return true;
}

bool is_game_line(BYTE* object) {
    BYTE* g[kGameLineLength] = {};
    if (!glyphs_of(object, kGameLineLength, g) || frame_of(g[0]) != kCopyright) return false;
    const size_t end = sizeof(kGameLineEnd) - 1;
    for (size_t i = 0; i < end; ++i) {
        if (frame_of(g[kGameLineLength - end + i]) != static_cast<uint8_t>(kGameLineEnd[i])) {
            return false;
        }
    }
    return true;
}

struct Look {
    BYTE* list = nullptr;  // the draw list's holder
    BYTE* game_line = nullptr;
    BYTE* mod_line = nullptr;
    BYTE* credit = nullptr;
    BYTE* hint = nullptr;
};

Look look(BYTE* draw_layer, const std::string& mod_line) {
    Look l;
    BYTE* object = nullptr;
    if (!read(draw_layer, 0, l.list) || l.list == nullptr || !read(l.list, kListHead, object)) {
        return l;
    }
    for (int i = 0; i < kMaxObjects && object != nullptr; ++i) {
        BYTE* owner = nullptr;
        if (!read(object, kOwner, owner) || owner != l.list) break;
        if (l.game_line == nullptr && is_game_line(object)) {
            l.game_line = object;
        } else if (l.mod_line == nullptr && shows(object, mod_line, false)) {
            l.mod_line = object;
        } else if (l.credit == nullptr && shows(object, kTitleCredit, true)) {
            l.credit = object;
        } else if (l.hint == nullptr && shows(object, kTitleHint, false)) {
            l.hint = object;
        }
        if (!read(object, kNext, object)) break;
    }
    return l;
}

void show(BYTE* object, size_t length, bool visible) {
    BYTE* g[kMaxGlyphs] = {};
    if (!glyphs_of(object, static_cast<int32_t>(length), g)) return;
    for (size_t i = 0; i < length; ++i) {
        uint8_t now = 0;
        if (read(g[i], sprite::kVisible, now) && now != (visible ? 1 : 0) &&
            writable(g[i], sprite::kSize)) {
            *(g[i] + sprite::kVisible) = visible ? 1 : 0;
        }
    }
}

// Makes one line as the title routine makes the copyright line, styled as `model` (the game's
// line). Returns "" or why not.
std::string make_line(const TitleAddresses& a, BYTE* list, BYTE* model, const std::string& text,
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
    // One row: the game's 32 columns, or the line's length if longer (a row of 32 wraps a longer
    // line onto a second, seen in game with helper 0.23.0).
    const int32_t columns = (std::max)(length, kTextColumnsAll);
    BYTE* t = reinterpret_cast<New>(a.text_new_fn)(text.c_str(), length, columns);
    BYTE* g[kMaxGlyphs] = {};
    BYTE* owner = reinterpret_cast<BYTE*>(1);
    if (t == nullptr || !writable(t, kTextSize) || !glyphs_of(t, length, g) ||
        !read(t, kOwner, owner)) {
        return "no text object";  // nothing in the list yet: the game's heap keeps it
    }
    put<int32_t>(t, kTextHint, -1);
    reinterpret_cast<Place>(a.text_place_fn)(t, title_line_x(text.size()), y);
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
    put<int32_t>(t, kTextShown, length);
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

std::string TitleLines::watch(const TitleAddresses& a, bool client) {
    if (mod_line_.empty() || frames_-- > 0) return "";
    frames_ = kLookFrames;
    Look l = look(a.draw_layer, mod_line_);
    if (l.game_line == nullptr) {
        shown_ = false;
        last_error_.clear();  // a new title tries again
        return "";
    }
    // Once a title: a line that failed is not made again until the game's line goes and comes.
    std::string why = last_error_;
    if (why.empty() && l.mod_line == nullptr) {
        why = make_line(a, l.list, l.game_line, mod_line_, false, kTitleModY, l.mod_line);
    }
    if (why.empty() && l.credit == nullptr) {
        why = make_line(a, l.list, l.game_line, kTitleCredit, true, kTitleCreditY, l.credit);
    }
    if (why.empty() && l.hint == nullptr) {
        why = make_line(a, l.list, l.game_line, kTitleHint, false, kTitleHintY, l.hint);
    }
    last_error_ = why;
    shown_ = l.mod_line != nullptr && l.credit != nullptr && l.hint != nullptr;
    // Shown while the game's © is (and again after a refused unload hid them); the hint only
    // while no client is connected.
    BYTE* g[kGameLineLength] = {};
    uint8_t visible = 0;
    if (glyphs_of(l.game_line, kGameLineLength, g) && read(g[0], sprite::kVisible, visible)) {
        if (l.mod_line != nullptr) show(l.mod_line, mod_line_.size(), visible != 0);
        if (l.credit != nullptr) show(l.credit, sizeof(kTitleCredit) - 1, visible != 0);
        if (l.hint != nullptr) show(l.hint, sizeof(kTitleHint) - 1, visible != 0 && !client);
    }
    return why;
}

void TitleLines::hide(const TitleAddresses& a) {
    Look l = look(a.draw_layer, mod_line_);
    if (l.mod_line != nullptr) show(l.mod_line, mod_line_.size(), false);
    if (l.credit != nullptr) show(l.credit, sizeof(kTitleCredit) - 1, false);
    if (l.hint != nullptr) show(l.hint, sizeof(kTitleHint) - 1, false);
    shown_ = false;
}

}  // namespace zap
