#include "menu.h"

#include <cmath>
#include <initializer_list>
#include <cstring>

#include "game_mem.h"
#include "apitem.h"  // kHdLockIcon
#include "translation.h"

namespace zap {

// The padlock: 7 wide, like the font's letters.
const char kLockRows[8][9] = {
    "..###...", ".#...#..", ".#...#..", "#######.",
    "###.###.", "###.###.", "#######.", "........",
};

namespace {

constexpr int32_t kMaxPanels = 16, kMaxChildren = 64;
constexpr ptrdiff_t kPanelSelected = 0x2DC;  // 32-bit: the selected child, -1 for none
constexpr int16_t kLockedPalette = 0;  // as the controls screen's STYLE: AUTO
// A glyph's two RGB tint sets (their alphas left alone), and STYLE: AUTO's gray, as read live.
constexpr ptrdiff_t kGlyphTint[6] = {0x68, 0x6C, 0x70, 0x78, 0x7C, 0x80};
constexpr float kLockedTint[6] = {0.412f, 0.439f, 0.494f, 0.412f, 0.439f, 0.494f};
constexpr int32_t kLockedStyle = 5;  // as the controls screen's STYLE: AUTO
// A translated glyph's lock: the options entries' own style (0x1628B0), at an alpha near the
// pixel lock's gray on the black options screen.
constexpr int32_t kLockedHdStyle = 0x15;
constexpr ptrdiff_t kGlyphAlpha = 0x84;
constexpr float kLockedHdAlpha = 0.45f;
constexpr int kLockAt = 7;  // ASSIST, a blank, the padlock
// In a translation: ASSIST's entry, and the options box's top right piece, where its right edge
// starts (docs/re/memory.md, "Where a rebuilt label goes").
constexpr int kAssistEntry = 137;
constexpr ptrdiff_t kBoxTopRight = 0x228;
constexpr int32_t kMaxTranslatedGlyphs = 3 * kAssistLength;  // the label's capacity
constexpr char kAssist[kAssistLength + 1] = "ASSIST\0\0\0\0\0";


// Whether `wanted` (not null) is one of `menu`'s panels (up to kMaxPanels).
bool lists_panel(const BYTE* menu, const BYTE* wanted) {
    const int32_t panels = panel_count(menu);
    for (int32_t p = 0; panels <= kMaxPanels && p < panels; ++p) {
        if (panel_at(menu, p) == wanted) return true;
    }
    return false;
}

// fn(panel, index, child) for each child of `menu`'s panels, within kMaxPanels and kMaxChildren,
// until fn returns true; returns whether one did.
template <typename Fn>
bool for_each_child(const BYTE* menu, Fn&& fn) {
    const int32_t panels = panel_count(menu);
    for (int32_t p = 0; panels <= kMaxPanels && p < panels; ++p) {
        BYTE* panel = panel_at(menu, p);
        BYTE* children[kMaxChildren] = {};
        const int32_t n = children_of(panel, children, kMaxChildren);
        for (int32_t c = 0; n <= kMaxChildren && c < n; ++c) {
            if (children[c] != nullptr && fn(panel, c, children[c])) return true;
        }
    }
    return false;
}

// Children `from` to `to` - 1 of a panel's list into `run`: false unless each is live and at its
// place (widget::kIndex).
bool live_run(BYTE** children, int32_t from, int32_t to, BYTE* (&run)[kMaxChildren]) {
    if (to > kMaxChildren) return false;
    for (int32_t k = from; k < to; ++k) {
        int32_t released = 1, place = -1;
        if (!read(reinterpret_cast<BYTE*>(children), k * 8, run[k]) || run[k] == nullptr ||
            !read(run[k], sprite::kReleased, released) || released != 0 ||
            !read(run[k], widget::kIndex, place) || place != k) {
            return false;
        }
    }
    return true;
}

}  // namespace

namespace {

// Whether a 12-glyph label reads as the lock leaves ASSIST: ASSIST, a blank, the padlock (or a
// blank, where the font has no frame for it: draw_locked's set_frame then fails), four blanks.
bool shows_locked(BYTE* (&glyphs)[kAssistLength]) {
    for (int i = 0; i < kAssistLength; ++i) {
        const int32_t frame = frame_of(glyphs[i]);
        if (frame != static_cast<unsigned char>(kAssist[i]) &&
            !(i == kLockAt && frame == kLockGlyph)) {
            return false;
        }
    }
    return true;
}

// The first live widget of `menu` with the generic handler and `callback` whose glyphs read
// ASSIST, as the game builds it or (`locked`) as the lock leaves it; with its panel and place.
BYTE* find_assist(BYTE* menu, const GameAddresses& a, const void* callback, bool locked,
                  BYTE** panel_out, int32_t* index_out) {
    BYTE* found = nullptr;
    for_each_child(menu, [&](BYTE* panel, int32_t index, BYTE* child) {
        if (!live_widget(child, a.widget_handler_fn, callback)) return false;
        BYTE* glyphs[kAssistLength] = {};
        if (!glyphs_of(child, glyphs) ||
            !(locked ? shows_locked(glyphs) : glyphs_show(glyphs, {kAssist, kAssistLength}))) {
            return false;
        }
        if (panel_out != nullptr) *panel_out = panel;
        if (index_out != nullptr) *index_out = index;
        found = child;
        return true;
    });
    return found;
}

}  // namespace

BYTE* AssistLock::assist_in(BYTE* menu, const GameAddresses& a, void* expected_callback,
                            BYTE** panel_out, int32_t* index_out) {
    if (expected_callback == nullptr) return nullptr;
    return find_assist(menu, a, expected_callback, false, panel_out, index_out);
}

bool AssistLock::still_locked(const GameAddresses& a) const {
    // Leaving Video, GENERAL or the assist menu builds the options menu again (0x124D30), its
    // VERSION 1.20 maybe in ASSIST's place (docs/re/memory.md, "The assist menu").
    if (child_ == nullptr || !is_open(a.current_menu, menu_)) return false;
    BYTE* panel = nullptr;
    return find_assist(menu_, a, nullptr, true, &panel, nullptr) == child_ && panel == panel_;
}

namespace {

// The locked look: every glyph in the locked style under the locked palette (when the palette
// table gives it), the padlock in the eighth. In a translation, lock_translated carries it.
void draw_locked(BYTE* (&glyphs)[kAssistLength], const GameAddresses& a) {
    BYTE* palette = pointer_at(a.palette_table, kLockedPalette * 8);
    BYTE probe = 0;
    if (!read(palette, 0, probe)) palette = nullptr;  // none readable there: the menu's own kept
    for (BYTE* g : glyphs) {
        at<int32_t>(g, sprite::kStyle) = kLockedStyle;
        for (int t = 0; t < 6; ++t) at<float>(g, kGlyphTint[t]) = kLockedTint[t];
        if (palette != nullptr) {
            at<void*>(g, sprite::kPalette) = palette;
            at<int16_t>(g, sprite::kPaletteNumber) = kLockedPalette;
        }
    }
    // The padlock only if the glyph has that many frames, as the game's own text routine checks.
    set_frame(glyphs[kLockAt], kLockGlyph);
}

// The x where the options box's right edge starts (its top right piece's), if it reads.
bool box_right(const BYTE* panel, double& x) {
    return read(pointer_at(panel, kBoxTopRight), sprite::kX, x);
}

}  // namespace

bool AssistLock::install(const GameAddresses& a) {
    BYTE* top = pointer_at(a.current_menu);
    if (child_ != nullptr) {
        if (still_locked(a)) {
            // Still locked, its menu open. The menu takes another palette after some screens (the
            // map's 26), so the locked look is pinned again whenever it is the menu up.
            BYTE* glyphs[kAssistLength] = {};
            if (top == menu_ && glyphs_of(child_, glyphs)) {
                draw_locked(glyphs, a);
                lock_translated(a, pointer_at(child_, widget::kLabel));
            }
            return false;
        }
        restore(a);
    }
    if (top == nullptr) return false;
    BYTE* panel = nullptr;
    int32_t index = -1;
    BYTE* child = assist_in(top, a, a.assist_menu_fn, &panel, &index);
    BYTE* glyphs[kAssistLength] = {};
    if (child == nullptr || !glyphs_of(child, glyphs)) return false;
    for (int i = 0; i < kAssistLength; ++i) {
        BYTE* g = glyphs[i];
        frames_[i] = at<int32_t>(g, sprite::kFrameIndex);
        styles_[i] = at<int32_t>(g, sprite::kStyle);
        palettes_[i] = at<void*>(g, sprite::kPalette);
        palette_numbers_[i] = at<int16_t>(g, sprite::kPaletteNumber);
        for (int t = 0; t < 6; ++t) tints_[i][t] = at<float>(g, kGlyphTint[t]);
    }
    draw_locked(glyphs, a);
    at<void*>(child, widget::kCallback) = nullptr;
    menu_ = top;
    child_ = child;
    move_out(panel, index);
    panel_ = panel;  // also when not moved: the box is its border's
    lock_translated(a, pointer_at(child, widget::kLabel));
    return true;
}

void AssistLock::lock_translated(const GameAddresses& a, BYTE* label) {
    std::wstring own;
    if (!read_small_entry(a, kAssistEntry, own)) return;
    if (!hd_.holds(label)) {
        hd_.label = nullptr;
        // Built after the pixel glyphs took the locked style, which the builder copies (0x8AAB5).
        // No space before the HD padlock: its cell has its own gap (apitem.h IconSheets).
        const std::wstring gap = hd_font_on(a) ? L"" : L" ";
        if (!rebuild_label(a, label, own + gap + kHdLockIcon, {.attached = true})) return;
        float left = 0, right = 0;
        double edge = 0;
        if (!box_right(panel_, edge) || !translated_extent(a, label, left, right) ||
            right > edge) {
            rebuild_label(a, label, own, {.attached = true});
        }
        hd_.take(label);
    }
    // The locked look again each time, dimmed through the alpha, which only a build sets back
    // (docs/re/memory.md, "The assist menu").
    int32_t n = 0;
    BYTE** list = nullptr;
    if (!translated_list(label, kMaxTranslatedGlyphs, n, list)) return;
    for (int32_t i = 0; i < n; ++i) {
        BYTE* g = glyph_at(list, i);
        if (g != nullptr && writable(g, sprite::kStyle + sizeof(int32_t))) {
            at<int32_t>(g, sprite::kStyle) = kLockedHdStyle;
            at<float>(g, kGlyphAlpha) = kLockedHdAlpha;
        }
    }
}

void AssistLock::unlock_translated(const GameAddresses& a, BYTE* label) {
    const bool ours = hd_.holds(label);
    hd_.label = nullptr;
    std::wstring own;
    if (ours && read_small_entry(a, kAssistEntry, own)) {
        rebuild_label(a, label, own, {.attached = true});  // after the pixel glyphs' style
    }
}

void AssistLock::move_out(BYTE* panel, int32_t index) {
    moved_from_ = -1;
    panel_ = panel;
    int32_t selectable = 0, capacity = 0;
    BYTE** children = nullptr;
    if (!read(panel, panel::kSelectable, selectable) || !read(panel, panel::kCapacity, capacity) ||
        !read(panel, panel::kChildren, children) || index < 0 || selectable <= index ||
        selectable > capacity || !writable(children, capacity * sizeof(BYTE*))) {
        return;  // not among the selectable ones, or not as expected: left where it is
    }
    // Every child from ASSIST to the last selectable one live and numbered by its place.
    BYTE* moved[kMaxChildren] = {};
    if (!live_run(children, index, selectable, moved)) return;
    for (int32_t k = index; k + 1 < selectable; ++k) {
        children[k] = moved[k + 1];
        at<int32_t>(moved[k + 1], widget::kIndex) = k;
    }
    children[selectable - 1] = moved[index];
    at<int32_t>(moved[index], widget::kIndex) = selectable - 1;
    at<int32_t>(panel, panel::kSelectable) = selectable - 1;
    // The cursor stays on the entry it was on (ASSIST's place goes to the entry after it).
    int32_t& selected = at<int32_t>(panel, kPanelSelected);
    if (selected > index && selected < selectable) --selected;
    moved_from_ = index;
    selectable_ = selectable;
}

void AssistLock::move_back() {
    if (moved_from_ < 0 || panel_ == nullptr) return;
    // The panel it moved must still be one of its menu's panels.
    if (!lists_panel(menu_, panel_)) {
        moved_from_ = -1;
        return;
    }
    int32_t index = moved_from_, selectable = selectable_;
    moved_from_ = -1;
    int32_t now = 0, capacity = 0;
    BYTE** children = nullptr;
    if (!read(panel_, panel::kSelectable, now) || now != selectable - 1 ||
        !read(panel_, panel::kCapacity, capacity) || selectable > capacity ||
        !read(panel_, panel::kChildren, children) ||
        !writable(children, capacity * sizeof(BYTE*))) {
        return;
    }
    BYTE* moved[kMaxChildren] = {};
    if (!live_run(children, index, selectable, moved)) return;
    if (moved[selectable - 1] != child_) return;  // not as move_out left it
    for (int32_t k = selectable - 1; k > index; --k) {
        children[k] = moved[k - 1];
        at<int32_t>(moved[k - 1], widget::kIndex) = k;
    }
    children[index] = child_;
    at<int32_t>(child_, widget::kIndex) = index;
    at<int32_t>(panel_, panel::kSelectable) = selectable;
    int32_t& selected = at<int32_t>(panel_, kPanelSelected);
    if (selected >= index && selected < selectable - 1) ++selected;
}

bool AssistLock::restore(const GameAddresses& a) {
    if (child_ == nullptr) return false;
    // Only into the widget found again as the locked ASSIST of its open menu. Any other (its menu
    // closed, or its place taken by another widget, or its label changed) is forgotten unwritten:
    // a relabeled ASSIST stays shut until its menu is built again, which builds it open.
    bool put = still_locked(a);
    if (put) {
        BYTE* glyphs[kAssistLength] = {};
        if (glyphs_of(child_, glyphs)) {
            for (int i = 0; i < kAssistLength; ++i) {
                BYTE* g = glyphs[i];
                // Frames and palettes derived afresh, not from the pointers saved at install.
                set_frame(g, frames_[i]);
                at<int32_t>(g, sprite::kStyle) = styles_[i];
                // A number past the table (not the game's) keeps the pointer saved at install.
                const int16_t number = palette_numbers_[i];
                BYTE* palette = number < 0 || number >= sprite::kPaletteCount
                                    ? nullptr
                                    : pointer_at(a.palette_table, number * 8);
                BYTE probe = 0;
                if (!read(palette, 0, probe)) palette = static_cast<BYTE*>(palettes_[i]);
                at<void*>(g, sprite::kPalette) = palette;
                at<int16_t>(g, sprite::kPaletteNumber) = palette_numbers_[i];
                for (int t = 0; t < 6; ++t) {
                    at<float>(glyphs[i], kGlyphTint[t]) = tints_[i][t];
                }
            }
        }
        at<void*>(child_, widget::kCallback) = a.assist_menu_fn;
        move_back();
        // In a translation, its translated text the entry's again, in its own style.
        unlock_translated(a, pointer_at(child_, widget::kLabel));
    }
    child_ = nullptr;
    menu_ = nullptr;
    panel_ = nullptr;
    moved_from_ = -1;
    hd_.label = nullptr;  // a label at the same place later is another's
    return put;
}

}  // namespace zap
