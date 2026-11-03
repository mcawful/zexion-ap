#include "translation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "apitem.h"  // the helper's icons
#include "equip.h"   // kDiskGlyph
#include "game_layout.h"
#include "game_mem.h"
#include "menu.h"  // kLockGlyph

namespace zap {

namespace {

// The most entries a table is taken to have (SmallText has 448, ItemNames 51: "Translation live
// reads"); a count past it is not a table as traced.
constexpr int kMaxEntries = 4096;
// The most glyphs a label is taken to have, either kind.
constexpr int32_t kMaxGlyphs = 4096;
// The game's builder and its std::wstring (docs/re/memory.md, "Translated menus and the prompt"):
// 0x88480(x, y, &text, &font, label, 0) takes the string by value and frees it; 0x17BD0(&text,
// characters, length) assigns it, from the 32 zero bytes the game's callers start it as.
using BuildFn = void (*)(float x, float y, void* text, void* font, BYTE* label, char flag);
using WstringFn = void* (*)(void* text, const wchar_t* characters, size_t length);
constexpr size_t kWstringSize = 32;
// 0x1870A0(text, _, _, _, _, _, _, entry, kind, message): the 8th to 10th on the stack, as the
// pickup and GAME SAVED call it (docs/re/memory.md, "Translated messages").
using HdTextFn = void (*)(const wchar_t* text, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t,
                          intptr_t, intptr_t entry, intptr_t kind, void* message);
constexpr intptr_t kSmallKind = 0;

// The table's pointers and count as read now through `text`, `offsets` and `count` (the keys:
// each holds a pointer, the count is 16-bit).
bool read_table(const BYTE* text, const BYTE* offsets, const BYTE* count, TextTable& out) {
    int16_t n = 0;
    if (!read(text, 0, out.text) || !read(offsets, 0, out.offsets) || !read(count, 0, n)) {
        return false;
    }
    out.count = n;
    return out.text != nullptr && out.offsets != nullptr && n > 0 && n <= kMaxEntries;
}

// Every offset of the table, copied: empty if it cannot be read.
std::vector<int32_t> offsets_of(const TextTable& t) {
    std::vector<int32_t> out(static_cast<size_t>(t.count));
    if (t.count <= 0 || !safe_read(t.offsets, out.data(), out.size() * sizeof(int32_t))) {
        out.clear();
    }
    return out;
}

// Whether an entry reads as the parser leaves an empty one: nothing but line breaks, then its 0.
bool reads_empty(const TextTable& t, int32_t offset) {
    wchar_t c[4] = {};
    if (offset < 0 || !safe_read(t.text + offset, c, sizeof(c))) return false;
    for (wchar_t ch : c) {
        if (ch == 0) return true;
        if (ch != L'\r' && ch != L'\n') return false;
    }
    return false;
}

bool reads(const TextTable& t, int32_t offset, std::wstring_view text) {
    std::wstring now;
    return read_text(t, offset, static_cast<int32_t>(text.size()) + 1, now) && now == text;
}

}  // namespace

bool translation_keys(const GameAddresses& a) {
    BYTE* const* keys[] = {&a.translation,  &a.tr_flag,     &a.small_text,    &a.small_offsets,
                           &a.item_names,   &a.item_name_offsets, &a.small_count,
                           &a.item_name_count, &a.hd_messages, &a.message_slots,
                           &a.hd_text_fn,   &a.hd_build_fn, &a.wstring_new_fn};
    for (BYTE* const* key : keys) {
        if (*key == nullptr) return false;
    }
    // The fields the other keys name are where the object has them: a layout that differs
    // leaves the translation parts out, as a pattern that differs would.
    BYTE* t = a.translation;
    return a.tr_flag == t + tr::kOn && a.small_text == t + tr::kSmallText &&
           a.small_offsets == t + tr::kSmallOffsets && a.item_names == t + tr::kItemNames &&
           a.item_name_offsets == t + tr::kItemNameOffsets;
}

std::optional<Loaded> loaded_translation(const GameAddresses& a) {
    if (!translation_keys(a)) return std::nullopt;
    uint8_t flag = 0;
    Loaded out;
    if (!read(a.tr_flag, 0, flag) ||
        !read_table(a.small_text, a.small_offsets, a.small_count, out.small) ||
        !read_table(a.item_names, a.item_name_offsets, a.item_name_count, out.items)) {
        return std::nullopt;
    }
    out.on = flag != 0;
    return out;
}

bool translation_on(const GameAddresses& a) {
    uint8_t flag = 0;
    return translation_keys(a) && read(a.tr_flag, 0, flag) && flag != 0;
}

bool hd_font_on(const GameAddresses& a) {
    int32_t font = 0, icons = 0;
    return translation_on(a) && read(a.translation, tr::kFontStyle, font) &&
           read(a.translation, tr::kIconStyle, icons) && font != tr::kPixelFont &&
           icons != tr::kPixelIcons;
}

std::string language_name(const GameAddresses& a) {
    if (!translation_keys(a)) return "?";
    uint8_t flag = 0;
    char name[tr::kNameSize] = {};
    if (!read(a.tr_flag, 0, flag)) return "?";
    if (flag == 0) return "-";
    if (!safe_read(a.translation + tr::kName, name, sizeof(name))) return "?";
    std::string out;
    for (char c : std::string_view(name, strnlen(name, sizeof(name)))) {
        // One word of `hello`: anything but letters, digits and the folders' parentheses as an
        // underscore.
        const bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '(' || c == ')' || c == '-';
        out += keep ? c : '_';
    }
    return out.empty() ? "?" : out;
}

int32_t entry_offset(const TextTable& t, int k) {
    int32_t offset = -1;
    if (k < 0 || k >= t.count || !safe_read(t.offsets + k, &offset, sizeof(offset))) return -1;
    return offset;
}

int32_t entry_room(const TextTable& t, int k) {
    const std::vector<int32_t> all = offsets_of(t);
    if (k < 0 || static_cast<size_t>(k) >= all.size() || all[static_cast<size_t>(k)] < 0) return 0;
    const int32_t own = all[static_cast<size_t>(k)];
    int32_t next = -1;
    for (int32_t o : all) {
        if (o > own && (next < 0 || o < next)) next = o;
    }
    return next < 0 ? 0 : next - own - 1;
}

bool read_text(const TextTable& t, int32_t offset, int32_t room, std::wstring& out) {
    out.clear();
    if (offset < 0 || room <= 0) return false;
    std::vector<wchar_t> chars(static_cast<size_t>(room));
    if (!safe_read(t.text + offset, chars.data(), chars.size() * sizeof(wchar_t))) return false;
    auto end = std::find(chars.begin(), chars.end(), L'\0');
    if (end == chars.end()) return false;
    out.assign(chars.begin(), end);
    return true;
}

bool read_entry(const TextTable& t, int k, std::wstring& out, int32_t last_room) {
    const int32_t room = entry_room(t, k);
    return read_text(t, entry_offset(t, k), room > 0 ? room : last_room, out);
}

bool write_text(const TextTable& t, int32_t offset, int32_t room, std::wstring_view text) {
    const size_t n = text.size() + 1;
    if (offset < 0 || room <= 0 || n > static_cast<size_t>(room)) return false;
    wchar_t* at = t.text + offset;
    if (!writable(at, n * sizeof(wchar_t))) return false;
    std::copy(text.begin(), text.end(), at);
    at[text.size()] = L'\0';
    return true;
}

std::wstring hd_text(std::string_view font_text) {
    std::wstring out;
    for (unsigned char c : font_text) {
        if (c == '{') c = '(';
        if (c == '}') c = ')';
        if (c >= 0x20 && c < 0x7F) out += static_cast<wchar_t>(c);
    }
    return out;
}

std::wstring hd_icon(uint8_t icon) {
    if (icon >= 0xC0 && icon <= 0xEA) return std::wstring(1, static_cast<wchar_t>(icon + 0xDF64));
    if (icon == 0x6E) return L"\xE04F";
    if (icon == 0x77) return L"\xE050";
    // The helper's own glyphs, as it paints them into the icon sheets (apitem.h IconSheets).
    if (icon == kDiskGlyph) return std::wstring(1, kHdDiskIcon);
    if (icon == kLockGlyph) return std::wstring(1, kHdLockIcon);
    if (icon == kApItemGlyph) return std::wstring(1, kHdLogoIcon);
    return L"";
}

std::wstring hd_label(const std::string& name, int width, bool first_line, bool icon) {
    const size_t w = width > 0 ? static_cast<size_t>(width) : name.size();
    std::wstring out;
    for (size_t at = 0, line = 0; at < name.size() && w > 0; at += w, ++line) {
        std::string part = name.substr(at, w);
        const size_t start = part.find_first_not_of(' ');
        if (start == std::string::npos) continue;
        part.erase(0, start);
        part.erase(part.find_last_not_of(' ') + 1);
        std::wstring text;
        // The first line's first character is the item's glyph (resolve_item_labels, apitem.h).
        const auto glyph = static_cast<uint8_t>(part[0]);
        if (line == 0 && (glyph >= 0x80 || glyph == 0x6E || glyph == 0x77)) {
            if (icon) text = hd_icon(glyph);
            part.erase(0, 1);
            const size_t rest = part.find_first_not_of(' ');
            part.erase(0, rest == std::string::npos ? part.size() : rest);
        }
        text += hd_text(part);
        if (text.empty()) continue;
        if (!out.empty()) out += L'\n';
        out += text;
        if (first_line) break;
    }
    return out;
}

namespace {

// What the game-mode panel and the prompt pass the attach as its place, each way (0x217D98).
constexpr double kAttachOffset = -1.0;
// 0x85B00: the screen's middle (0x217CC0), and what a first glyph that is not of the font (an
// icon) adds to the left (0x217630).
constexpr float kScreenMiddle = 160.0f;
constexpr double kIconLeft = 2.0;
constexpr ptrdiff_t kRightCorner = sprite::kCorners + 0x10;
// The icon sheets' templates (apitem.h IconSheets), and where 0x86660 takes an icon to sit
// when it is a text's first glyph (0x217680).
constexpr ptrdiff_t kHdIconSheet = 101 * 8, kPixelIconSheet = 104 * 8;
constexpr double kIconMiddle = 4.0;

// The glyphs the builder makes for `text`: one per character but spaces, tabs and line breaks
// (0x88781 to 0x88853).
int32_t glyphs_in(std::wstring_view text) {
    int32_t n = 0;
    for (wchar_t c : text) {
        if (c != L' ' && c != L'\t' && c != L'\n' && c != L'\r') ++n;
    }
    return n;
}

void build(const GameAddresses& a, BYTE* label, std::wstring_view text, double x, double y,
           bool wrap) {
    // As the game's callers start it (0x8AC65 to 0x8ACA0): 32 zero bytes, then the assign.
    alignas(16) BYTE wide[kWstringSize] = {};
    reinterpret_cast<WstringFn>(a.wstring_new_fn)(wide, text.data(), text.size());
    reinterpret_cast<BuildFn>(a.hd_build_fn)(static_cast<float>(x), static_cast<float>(y), wide,
                                             a.translation + tr::kFont, label, wrap ? 1 : 0);
}

// The left and right of a label's translated text as 0x85B00 takes them: its first glyph (shown
// or not, two further right for an icon) and every shown one after it up to the first missing.
bool extent(const GameAddresses& a, const BYTE* label, float& left, float& right) {
    int32_t n = 0;
    BYTE** list = nullptr;
    void* font = nullptr;
    if (!read(label, hdlabel::kCount, n) || n < 1 || n > kMaxGlyphs ||
        !read(label, hdlabel::kGlyphs, list) || !read(a.translation, tr::kFont, font)) {
        return false;
    }
    for (int32_t i = 0; i < n; ++i) {
        BYTE* g = pointer_at(reinterpret_cast<BYTE*>(list), i * static_cast<ptrdiff_t>(8));
        if (g == nullptr) {
            if (i == 0) return false;
            break;
        }
        uint8_t shown = 0;
        double x = 0, l = 0, r = 0;
        void* made_from = nullptr;
        if (!read(g, sprite::kVisible, shown) || !read(g, sprite::kX, x) ||
            !read(g, sprite::kCorners, l) || !read(g, kRightCorner, r) ||
            !read(g, sprite::kTemplate, made_from)) {
            return false;
        }
        if (i == 0) {
            left = static_cast<float>(x + l + (made_from != font ? kIconLeft : 0.0));
            right = static_cast<float>(x + r);
        } else if (shown != 0) {
            left = std::min(left, static_cast<float>(x + l));
            right = std::max(right, static_cast<float>(x + r));
        }
    }
    return true;
}

// The top and bottom of a label's translated text as 0x86660 takes them: its first glyph (its
// quad's top right and bottom right corners about its place, or its place plus 4 for an icon),
// and every shown one after it from the font or an icon sheet, up to the first missing.
bool vertical_extent(const GameAddresses& a, const BYTE* label, float& top, float& bottom) {
    int32_t n = 0;
    BYTE** list = nullptr;
    void* font = nullptr;
    if (!read(label, hdlabel::kCount, n) || n < 1 || n > kMaxGlyphs ||
        !read(label, hdlabel::kGlyphs, list) || !read(a.translation, tr::kFont, font)) {
        return false;
    }
    BYTE* templates = pointer_at(a.sprite_templates);
    void* sheets[2] = {pointer_at(templates, kHdIconSheet), pointer_at(templates, kPixelIconSheet)};
    for (int32_t i = 0; i < n; ++i) {
        BYTE* g = pointer_at(reinterpret_cast<BYTE*>(list), i * static_cast<ptrdiff_t>(8));
        if (g == nullptr) {
            if (i == 0) return false;
            break;
        }
        uint8_t shown = 0;
        double y = 0, upper = 0, lower = 0;
        void* made_from = nullptr;
        if (!read(g, sprite::kVisible, shown) || !read(g, sprite::kY, y) ||
            !read(g, sprite::kCorners + 0x08, upper) || !read(g, sprite::kCorners + 0x28, lower) ||
            !read(g, sprite::kTemplate, made_from)) {
            return false;
        }
        if (i == 0) {
            const bool of_font = made_from == font;
            top = static_cast<float>(y + (of_font ? upper : kIconMiddle));
            bottom = static_cast<float>(y + (of_font ? lower : kIconMiddle));
        } else if (shown != 0 &&
                   (made_from == font || made_from == sheets[0] || made_from == sheets[1])) {
            top = std::min(top, static_cast<float>(y + upper));
            bottom = std::max(bottom, static_cast<float>(y + lower));
        }
    }
    return true;
}

// Whether a label's translated text is built and hidden: its first glyph's +0x120 0.
bool translated_hidden(const BYTE* label) {
    int32_t n = 0;
    BYTE** list = nullptr;
    uint8_t shown = 1;
    return read(label, hdlabel::kCount, n) && n > 0 && read(label, hdlabel::kGlyphs, list) &&
           read(pointer_at(reinterpret_cast<BYTE*>(list)), sprite::kVisible, shown) &&
           shown == 0;
}

// Its translated glyphs hidden, their count (+0x244, the ones event 0xF shows) kept.
void hide_translated(BYTE* label) {
    int32_t n = 0;
    BYTE** list = nullptr;
    if (!read(label, hdlabel::kCount, n) || n < 0 || n > kMaxGlyphs ||
        !read(label, hdlabel::kGlyphs, list)) {
        return;
    }
    for (int32_t i = 0; i < n; ++i) {
        BYTE* g = pointer_at(reinterpret_cast<BYTE*>(list), i * static_cast<ptrdiff_t>(8));
        if (g != nullptr && writable(g + sprite::kVisible, 1)) g[sprite::kVisible] = 0;
    }
}

}  // namespace

BuiltMark built_mark(const BYTE* label) {
    BuiltMark m;
    int32_t count = -1;
    if (read(label, hdlabel::kCount, count) && read(label, hdlabel::kX, m.x) &&
        read(label, hdlabel::kY, m.y)) {
        m.count = count;
    }
    return m;
}

bool translated_extent(const GameAddresses& a, const BYTE* label, float& left, float& right) {
    return extent(a, label, left, right);
}

namespace {

// The builder's advances (0x88480; docs/re/memory.md, "Laying out the helper's popups"): the
// font's glyph table by character (+0x30, 4 bytes each, -1 for none: the character is skipped),
// its glyph entries (+0x08, 0x20 bytes each: advance from +0x08 to +0x10 in 1/64 pixel at the
// font's size), its space advance (+0x40040, a float) and its scale (+0x40060, a double); the
// HD font's size divisor (0x217B40) and the HD icons' (0x217690).
constexpr ptrdiff_t kFontGlyphTable = 0x30, kFontGlyphs = 0x08, kFontSpace = 0x40040;
constexpr ptrdiff_t kFontScale = 0x40060;
constexpr ptrdiff_t kGlyphFrom = 0x08, kGlyphTo = 0x10;
constexpr size_t kGlyphEntry = 0x20;
constexpr float kHdFontDivisor = 4.8214f;
constexpr double kHdIconDivisor = 4.8214;
// The icon sheets (apitem.h IconSheets): the builder takes an icon's advance from its frame's
// entry, +0x28 with a pixel font, +0x26 over kHdIconDivisor with an HD one (0x8911D); U+E024 to
// U+E04E are frames 0x80 to 0xAA, U+E04F frame 0xBD, U+E050 frame 0xC6; definition 104's sheet
// with a pixel font or icon style 1 (tr::kIconStyle), else 101's (0x885BF).
constexpr ptrdiff_t kFrameEntry = 0x38, kPixelAdvance = 0x28, kHdAdvance = 0x26;
constexpr int32_t kSheetFrames = 256;
// Bounds on what a loaded font reads (the HD font's scale and space are near 1 and 15 to 20),
// and on the spaces one line is padded with.
constexpr double kMostFactor = 1000, kMostSpace = 1000, kMostPad = 64;

int icon_frame(wchar_t c) {
    if (c >= 0xE024 && c <= 0xE04E) return c - 0xDFA4;
    if (c == 0xE04F) return 0xBD;
    if (c == 0xE050) return 0xC6;
    return -1;
}

}  // namespace

bool FontMetrics::load(const GameAddresses& a) {
    *this = FontMetrics();
    if (!translation_keys(a)) return false;
    int32_t style = 0, icons = 0;
    float space = 0;
    font_ = a.translation + tr::kFont;
    if (!read(a.translation, tr::kFontStyle, style) || !read(a.translation, tr::kIconStyle, icons) ||
        !read(font_, kFontGlyphs, glyphs_) || glyphs_ == nullptr ||
        !read(font_, kFontScale, factor_) || !read(font_, kFontSpace, space)) {
        return false;
    }
    // Values a loaded font has (a reload in progress could leave others): measured only then.
    if (!std::isfinite(factor_) || factor_ <= 0 || factor_ > kMostFactor ||
        !std::isfinite(space) || space < 0 || space > kMostSpace) {
        return false;
    }
    pixel_font_ = style == tr::kPixelFont;
    scale_ = pixel_font_ ? 1.0f : kHdFontDivisor;
    space_ = static_cast<float>(space / static_cast<float>(scale_));
    const ptrdiff_t sheet = pixel_font_ || icons == 1 ? kPixelIconSheet : kHdIconSheet;
    // The sheet's frames, only if it has its 256 (icon_frame's last is 0xC6).
    BYTE* sheet_template = pointer_at(pointer_at(a.sprite_templates), sheet);
    int32_t frames = 0;
    if (read(sheet_template, sprite::kFrameCount, frames) && frames == kSheetFrames) {
        icon_frames_ = pointer_at(sheet_template, sprite::kFrameList);
    }
    return true;
}

double FontMetrics::advance(wchar_t c) const {
    if (c == L' ') return space_;
    int32_t index = -1;
    if (!read(font_, kFontGlyphTable + static_cast<ptrdiff_t>(c) * 4, index)) return -1;
    if (index == -1) return 0;  // no glyph: the builder skips it
    if (const int frame = icon_frame(c); frame >= 0) {
        int16_t w = 0;
        const BYTE* entry = icon_frames_ + frame * kFrameEntry;
        if (icon_frames_ == nullptr ||
            !read(entry, pixel_font_ ? kPixelAdvance : kHdAdvance, w)) {
            return -1;
        }
        return pixel_font_ ? static_cast<float>(w) : static_cast<float>(w / kHdIconDivisor);
    }
    int32_t from = 0, to = 0;
    const BYTE* glyph = glyphs_ + static_cast<size_t>(index) * kGlyphEntry;
    if (index < 0 || !read(glyph, kGlyphFrom, from) || !read(glyph, kGlyphTo, to)) return -1;
    const double unit = static_cast<double>(static_cast<float>(scale_)) * 64.0;
    const auto width = static_cast<float>(static_cast<double>(static_cast<float>(to)) / unit -
                                          static_cast<double>(static_cast<float>(from)) / unit);
    return static_cast<float>(static_cast<double>(width) * factor_);
}

double FontMetrics::width(std::wstring_view line) const {
    double w = 0;
    for (wchar_t c : line) {
        const double step = advance(c);
        if (step < 0) return -1;
        w += step;
    }
    return w;
}

namespace {

// `text`'s words in lines no wider than `cap` (`fits` says whether a line fits), a word wider than
// a line alone on its line.
template <typename Fits>
std::vector<std::wstring> wrap(const std::wstring& text, Fits&& fits) {
    std::vector<std::wstring> lines;
    std::wstring line;
    size_t at = 0;
    while (at < text.size()) {
        const size_t start = text.find_first_not_of(L' ', at);
        if (start == std::wstring::npos) break;
        size_t end = text.find(L' ', start);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring word = text.substr(start, end - start);
        const std::wstring longer = line.empty() ? word : line + L' ' + word;
        if (!line.empty() && !fits(longer)) {
            lines.push_back(line);
            line = word;
        } else {
            line = longer;
        }
        at = end;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// `lines` centered on the widest with leading spaces, as the game starts every line of a
// popup at the same x (a line whose width does not read left as it is).
void center(std::vector<std::wstring>& lines, const FontMetrics& metrics) {
    if (metrics.space() <= 0) return;
    double widest = 0;
    std::vector<double> widths;
    for (const std::wstring& line : lines) {
        widths.push_back(metrics.width(line));
        widest = (std::max)(widest, widths.back());
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        if (widths[i] < 0) continue;
        const double spaces = (widest - widths[i]) / 2 / metrics.space() + 0.5;
        const auto pad = static_cast<size_t>(std::clamp(spaces, 0.0, kMostPad));
        lines[i].insert(0, pad, L' ');
    }
}

std::wstring joined(const std::vector<std::wstring>& lines) {
    std::wstring out;
    for (const std::wstring& line : lines) {
        if (!out.empty()) out += L'\n';
        out += line;
    }
    return out;
}

}  // namespace

std::wstring centered_lines(const std::wstring& text, const FontMetrics* metrics) {
    if (metrics == nullptr) return text;
    std::vector<std::wstring> lines;
    for (size_t at = 0; at <= text.size();) {
        size_t end = text.find(L'\n', at);
        if (end == std::wstring::npos) end = text.size();
        lines.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    center(lines, *metrics);
    return joined(lines);
}

std::wstring layout_popup(const std::wstring& head, const std::wstring& suffix,
                          const FontMetrics* metrics, double cap, size_t fallback) {
    auto fits = [&](const std::wstring& line) {
        if (metrics == nullptr) return line.size() <= fallback;
        const double w = metrics->width(line);
        return w >= 0 && w <= cap;
    };
    std::vector<std::wstring> lines;
    for (const std::wstring* part : {&head, &suffix}) {
        // Each part's own lines (a notice may hold line breaks) wrapped at the cap.
        size_t at = 0;
        while (at <= part->size() && !part->empty()) {
            size_t end = part->find(L'\n', at);
            if (end == std::wstring::npos) end = part->size();
            for (std::wstring& line : wrap(part->substr(at, end - at), fits)) {
                lines.push_back(std::move(line));
            }
            at = end + 1;
        }
    }
    // A popup's lines centered on the widest; a notice's left as they wrap.
    if (!head.empty() && metrics != nullptr) center(lines, *metrics);
    return joined(lines);
}

std::wstring hd_suffix(const std::string& as_sent) {
    std::wstring out = hd_text(as_sent);
    // The client's "FROM <sender>" (client/session.py), its word in the sentence's case.
    if (out.rfind(L"FROM ", 0) == 0) out.replace(0, 4, L"from");
    return out;
}

bool rebuild_label(const GameAddresses& a, BYTE* label, std::wstring_view text,
                   const Rebuild& how) {
    if (!translation_on(a)) return false;
    uint8_t built = 0;
    int32_t capacity = 0;
    double x = 0, y = 0;
    // More glyphs than it holds would be laid over its last one (0x88B14 to 0x88B28).
    if (!read(label, hdlabel::kBuilt, built) || built == 0 ||
        !read(label, hdlabel::kCapacity, capacity) || glyphs_in(text) > capacity ||
        !writable(label, hdlabel::kSize)) {
        return false;
    }
    if (how.attached) {
        // As 0x8AC30 takes its place: the first pixel glyph's, which the label's layout (0xA79E0)
        // keeps where the label is.
        BYTE* first = pointer_at(pointer_at(label, label::kGlyphs));
        if (first == nullptr || !read(first, sprite::kX, x) || !read(first, sprite::kY, y)) {
            return false;
        }
        x += kAttachOffset;
        y += kAttachOffset;
    } else if (!read(label, hdlabel::kX, x) || !read(label, hdlabel::kY, y)) {
        return false;
    }
    // The middle of the text it replaces, before that is gone.
    float top = 0, bottom = 0;
    const bool middle_known = how.keep_middle && vertical_extent(a, label, top, bottom);
    const float middle = (top + bottom) * 0.5f;
    // Hidden as its text was, if it was: the builder shows every glyph it builds (0x89F51),
    // while a hidden widget's are hidden with its count kept, for the widget handler to show
    // that many on event 0xF (0xD0290 to 0xD02EA), as the game-mode panel's are on the file
    // select before an empty slot is picked.
    const bool hidden = translated_hidden(label);
    build(a, label, text, x, y, how.wrap);
    int32_t style = 0;
    const bool pixel_font =
        read(a.translation, tr::kFontStyle, style) && style == tr::kPixelFont;
    // 0x85B00's and 0x86660's moves, made by building it there instead (its own place then kept
    // with it); each rounded for a pixel font (0x85BCA to 0x85BFB, 0x86760 to 0x86785).
    float dx = 0, dy = 0, left = 0, right = 0;
    if (how.center && extent(a, label, left, right)) {
        dx = kScreenMiddle - (right - left) * 0.5f - left + how.center_by;
        if (pixel_font) dx = std::round(dx);
    }
    if (middle_known && vertical_extent(a, label, top, bottom)) {
        dy = middle - (bottom - top) * 0.5f - top;
        if (pixel_font) dy = std::round(dy);
    }
    if (dx != 0 || dy != 0) build(a, label, text, x + dx, y + dy, how.wrap);
    if (hidden) hide_translated(label);
    return true;
}

// The most characters read for the last LargeText entry, whose room is not known.
constexpr int32_t kLargeLastRoom = 256;

bool read_large_entry(const GameAddresses& a, int k, std::wstring& out) {
    out.clear();
    if (a.large_count == nullptr || !translation_on(a)) return false;
    TextTable large;
    return read_table(a.translation + tr::kLargeText, a.translation + tr::kLargeOffsets,
                      a.large_count, large) &&
           read_entry(large, k, out, kLargeLastRoom);
}

namespace {

// A label's glyph lists: its translated glyphs (count, and the list) and its pixel ones.
struct LabelGlyphs {
    int32_t translated = 0, pixel = 0;
    BYTE** translated_list = nullptr;
    BYTE** pixel_list = nullptr;
};

bool glyph_lists(const BYTE* label, LabelGlyphs& out) {
    uint8_t built = 0;
    if (!read(label, hdlabel::kBuilt, built) || built == 0 ||
        !read(label, hdlabel::kCount, out.translated) ||
        !read(label, hdlabel::kGlyphs, out.translated_list) ||
        !read(label, label::kLength, out.pixel) || !read(label, label::kGlyphs, out.pixel_list) ||
        out.translated < 0 || out.translated > kMaxGlyphs || out.pixel < 0 ||
        out.pixel > kMaxGlyphs || !writable(label, hdlabel::kSize)) {
        return false;
    }
    return true;
}

// Each of a list's first `n` glyphs shown or hidden, those that can be written.
void show_each(BYTE** list, int32_t n, bool shown) {
    for (int32_t i = 0; i < n; ++i) {
        BYTE* g = pointer_at(reinterpret_cast<BYTE*>(list), i * static_cast<ptrdiff_t>(8));
        if (g != nullptr && writable(g + sprite::kVisible, 1)) g[sprite::kVisible] = shown ? 1 : 0;
    }
}

// The label's pixel glyphs shown and its translated ones hidden (`pixel`), or the other way.
bool show_glyphs(BYTE* label, bool pixel) {
    LabelGlyphs g;
    if (!glyph_lists(label, g)) return false;
    at<int32_t>(label, hdlabel::kShown) = pixel ? 0 : g.translated;
    show_each(g.translated_list, g.translated, !pixel);
    at<int32_t>(label, hdlabel::kPixelShown) = pixel ? g.pixel : 0;
    show_each(g.pixel_list, g.pixel, pixel);
    return true;
}

}  // namespace

bool show_pixel_glyphs(BYTE* label) { return show_glyphs(label, true); }
bool show_translated_glyphs(BYTE* label) { return show_glyphs(label, false); }

bool pixel_glyphs_shown(const BYTE* label) {
    int32_t shown = -1, pixel = -1, length = -1;
    return read(label, hdlabel::kShown, shown) && read(label, hdlabel::kPixelShown, pixel) &&
           read(label, label::kLength, length) && shown == 0 && pixel == length;
}

void EntryWriter::put_back(const std::optional<Loaded>& now, const EntryKey& key, const Held& h) {
    if (!now || now->table(key.first) != h.at || entry_offset(h.at, key.second) != h.offset) {
        return;  // another table: the one written was freed with its translation
    }
    if (reads(h.at, h.offset, h.written)) write_text(h.at, h.offset, h.room, h.original);
}

void EntryWriter::watch(const std::optional<Loaded>& now,
                        const std::map<EntryKey, std::wstring>& want) {
    for (auto it = held_.begin(); it != held_.end();) {
        const EntryKey& key = it->first;
        const Held& h = it->second;
        if (!now || now->table(key.first) != h.at || entry_offset(h.at, key.second) != h.offset) {
            it = held_.erase(it);  // its table was freed (or replaced): nothing to put back
            continue;
        }
        auto wanted = want.find(key);
        std::wstring text = wanted != want.end() ? wanted->second : std::wstring();
        if (text.size() + 1 > static_cast<size_t>(h.room)) text.resize(h.room - 1);
        if (!now->on || wanted == want.end() || text != h.written) {
            put_back(now, key, h);
            it = held_.erase(it);
            continue;
        }
        if (!reads(h.at, h.offset, h.written)) {
            it = held_.erase(it);  // changed by the game since (a reload in place): its own now
            continue;
        }
        ++it;
    }
    if (!now || !now->on) return;
    for (const auto& [key, wanted] : want) {
        if (held_.count(key) != 0) continue;
        const TextTable& t = now->table(key.first);
        Held h;
        h.at = t;
        h.offset = entry_offset(t, key.second);
        h.room = entry_room(t, key.second);
        if (h.offset < 0 || h.room < 2 || !read_text(t, h.offset, h.room, h.original)) continue;
        h.written = wanted;
        if (h.written.size() + 1 > static_cast<size_t>(h.room)) h.written.resize(h.room - 1);
        if (h.written == h.original) continue;  // reads so already: nothing of the helper's
        if (write_text(t, h.offset, h.room, h.written)) held_[key] = std::move(h);
    }
}

void EntryWriter::restore(const std::optional<Loaded>& now) {
    for (const auto& [key, h] : held_) put_back(now, key, h);
    held_.clear();
}

std::optional<std::wstring> EntryWriter::original(EntryKey key) const {
    auto it = held_.find(key);
    if (it == held_.end()) return std::nullopt;
    return it->second.original;
}

BYTE* hd_object(const GameAddresses& a, BYTE* message) {
    int32_t slot = -1;
    if (a.hd_messages == nullptr || a.message_slots == nullptr ||
        !read(message, message::kSlot, slot) || slot < 0 || slot >= hd::kSlots ||
        message != a.message_slots + static_cast<ptrdiff_t>(message::kSize) * slot) {
        return nullptr;
    }
    BYTE* object = a.hd_messages + static_cast<ptrdiff_t>(hd::kSize) * slot;
    return writable(object, hd::kSize) ? object : nullptr;
}

void clear_mark(const GameAddresses& a, BYTE* message) {
    if (!translation_on(a)) return;
    BYTE* object = hd_object(a, message);
    // Only an object not bound to this message: the mark is the last translated message's in
    // that slot. A slot's messages are the slot itself, so its object's message (+0x220) is the
    // same pointer whatever it was bound for; what tells them apart is the serial it was bound
    // with (+0x228, the slot's +0x1D8 then), and the message routine gives each message a new
    // one and binds none (docs/re/memory.md, "Translated messages").
    int32_t bound = 0, serial = 0;
    if (object != nullptr && read(object, hd::kSerial, bound) &&
        read(message, message::kSerial, serial) && bound != serial) {
        object[hd::kHasText] = 0;
    }
}

void PopupAreas::forget() {
    at_ = {};
    run_ = -1;
    run_room_ = 0;
    moved_from_ = -1;
    for (Area& area : areas_) area = Area{};
    waited_ = 0;
}

bool PopupAreas::prepare(const Loaded& now) {
    if (run_ >= 0 && now.small == at_ &&
        entry_offset(at_, kFirst) == run_ && entry_offset(at_, kMoved) == areas_[1].offset) {
        return true;
    }
    // A table loaded since (or the split undone there): what was ours went with the old one.
    forget();
    const std::vector<int32_t> all = offsets_of(now.small);
    if (all.size() <= static_cast<size_t>(kNext)) return false;
    // The run as the parser leaves it: its entries in order, each empty, the next one after.
    for (int k = kFirst; k < kNext; ++k) {
        if (all[k] < 0 || all[k + 1] <= all[k] || !reads_empty(now.small, all[k])) return false;
    }
    const int32_t start = all[kFirst];
    const int32_t room = all[kNext] - start - 1;
    if (room < 32) return false;
    int32_t* moved = now.small.offsets + kMoved;
    if (!writable(now.small.text + start, static_cast<size_t>(room) * sizeof(wchar_t)) ||
        !writable(moved, sizeof(int32_t))) {
        return false;
    }
    at_ = now.small;
    run_ = start;
    run_room_ = room;
    moved_from_ = all[kMoved];
    const int32_t half = room / 2;
    areas_[0].entry = kFirst;
    areas_[0].offset = start;
    areas_[0].room = half;
    areas_[1].entry = kMoved;
    areas_[1].offset = start + half;
    areas_[1].room = room - half;
    *moved = areas_[1].offset;
    return true;
}

int PopupAreas::reserve(const Loaded& now, size_t chars) {
    if (!now.on || !prepare(now)) return kNone;
    const size_t need = chars + 1;
    if (need > static_cast<size_t>(run_room_)) return kNone;
    const bool free[2] = {areas_[0].message == nullptr, areas_[1].message == nullptr};
    for (int k = 0; k < 2; ++k) {
        const int i = (next_ + k) % 2;
        if (free[i] && need <= static_cast<size_t>(areas_[i].room)) {
            waited_ = 0;
            return i;
        }
    }
    if (free[0] && free[1]) {
        waited_ = 0;
        return kWhole;
    }
    // Taken: freed within a frame or two once its message ends (the helper's popups come one at
    // a time; the one before may still be closing).
    if (++waited_ > kMaxWait) {
        waited_ = 0;
        return kNone;
    }
    return kWait;
}

bool PopupAreas::show(const GameAddresses& a, const Loaded& now, int where, BYTE* message,
                      const std::wstring& text) {
    if (where != 0 && where != 1 && where != kWhole) return false;
    if (!now.on || !prepare(now)) return false;
    BYTE* object = hd_object(a, message);
    int32_t serial = 0;
    if (object == nullptr || !read(message, message::kSerial, serial)) return false;
    Area& area = areas_[where == kWhole ? 0 : where];
    const int32_t room = where == kWhole ? run_room_ : area.room;
    if (area.message != nullptr || (where == kWhole && areas_[1].message != nullptr) ||
        !write_text(now.small, area.offset, room, text)) {
        return false;
    }
    area.message = message;
    area.serial = serial;
    area.object = object;
    area.written = text;
    if (where == kWhole) {
        // Area 1's offset lies inside: taken too, by the same message, with no text of its own,
        // so it frees in the same watch as area 0.
        areas_[1].message = message;
        areas_[1].serial = serial;
        areas_[1].object = object;
    }
    next_ = (where == 0) ? 1 : 0;
    reinterpret_cast<HdTextFn>(a.hd_text_fn)(text.c_str(), 0, 0, 0, 0, 0, 0, area.entry,
                                             kSmallKind, message);
    return true;
}

bool PopupAreas::area_free(const Area& area) {
    if (area.message == nullptr) return true;
    int32_t serial = 0, entry = -1, kind = -1, bound_serial = 0;
    BYTE* bound = nullptr;
    // Its message gone (its serial cleared at 0 frames left, or the slot reused), or its HD object
    // given another's (a translated message of the game's in that slot).
    if (!read(area.message, message::kSerial, serial) || serial != area.serial) return true;
    if (!read(area.object, hd::kMessage, bound) || !read(area.object, hd::kSerial, bound_serial) ||
        !read(area.object, hd::kEntry, entry) || !read(area.object, hd::kKind, kind)) {
        return true;
    }
    return bound != area.message || bound_serial != area.serial || kind != kSmallKind ||
           (entry != kFirst && entry != kMoved);
}

void PopupAreas::empty(const Loaded& now, Area& area) {
    // Zeros over what it wrote, where it still reads so: the run back to empty entries, so a
    // save-state load that binds a message to it again (0x2007D0) finds none and shows its pixel
    // text (docs/re/memory.md, "Borrowed entries for popups").
    if (!area.written.empty() && now.small == at_ && reads(at_, area.offset, area.written)) {
        wchar_t* p = at_.text + area.offset;
        if (writable(p, (area.written.size() + 1) * sizeof(wchar_t))) {
            std::fill(p, p + area.written.size() + 1, L'\0');
        }
    }
    area.message = nullptr;
    area.object = nullptr;
    area.serial = 0;
    area.written.clear();
}

void PopupAreas::watch(const std::optional<Loaded>& now) {
    if (run_ < 0) return;
    if (!now || now->small != at_) {
        forget();  // another table: the old one, and what was in it, freed
        return;
    }
    for (Area& area : areas_) {
        if (area.message != nullptr && area_free(area)) empty(*now, area);
    }
}

void PopupAreas::restore(const std::optional<Loaded>& now) {
    watch(now);
    if (run_ < 0 || in_use()) return;
    if (now && now->small == at_ && entry_offset(at_, kMoved) == areas_[1].offset &&
        writable(at_.offsets + kMoved, sizeof(int32_t))) {
        at_.offsets[kMoved] = moved_from_;
    }
    forget();
}

}  // namespace zap
