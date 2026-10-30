#include "menu.h"

#include <cmath>
#include <initializer_list>
#include <cstring>

#include "game_mem.h"

namespace zap {

// The padlock: 7 wide, like the font's letters.
const char kLockRows[8][9] = {
    "..###...", ".#...#..", ".#...#..", "#######.",
    "###.###.", "###.###.", "#######.", "........",
};

namespace {

constexpr ptrdiff_t kPanelIndex = 3;  // the game-mode panel, in its menu's panel list
// The hover border (docs/re/memory.md, "The game-mode panel"): the panel's handler sizes it
// 11 x 8 + 8 = 96 wide from 8 left of the label (0xAE531 to 0xAE545; width at +0x2B8), out of
// eight pieces pointed to from +0x278 (top-left, top, top-right, right, bottom-right, bottom,
// bottom-left, left). Each piece has its x at +0x40 and its drawn rectangle from +0x128 to
// +0x138; the top and bottom edges also keep their length (+0x114, float), their right (+0x158)
// and their quad's right corners (+0xA8, +0xB8). Even around RANDOMIZER's ten letters and a
// blank, it ends on ARCHIPELAGO's last letter.
constexpr ptrdiff_t kPanelBorderPieces = 0x278, kPanelBorderWidth = 0x2B8;
constexpr int kBorderPieces = 8;
constexpr int kTop = 1, kTopRight = 2, kRight = 3, kBottomRight = 4, kBottom = 5;
constexpr ptrdiff_t kPieceLeft = 0x128, kPieceRight = 0x138;  // x: sprite::kX
constexpr ptrdiff_t kEdgeLength = 0x114, kEdgeLocalRight = 0x158;
constexpr ptrdiff_t kEdgeQuadRight1 = 0xA8, kEdgeQuadRight2 = 0xB8;
constexpr int32_t kGameBorderWidth = kLabelLength * 8 + 8;  // 96
constexpr double kEdgeAsLaid = 80.1;  // the top-right piece's x past the label's (read live)
constexpr double kBorderMove = 8.0;   // one character: as far past the label as on its left
constexpr char kLabel[kLabelLength + 1] = "ARCHIPELAGO";
constexpr char kRandomizer[kLabelLength + 1] = "RANDOMIZER ";  // the game's own label
constexpr int32_t kMaxPanels = 16, kMaxChildren = 64;
constexpr ptrdiff_t kPanelSelected = 0x2DC;  // 32-bit: the selected child, -1 for none
constexpr int16_t kLockedPalette = 0;  // as the controls screen's STYLE: AUTO
// A glyph's color tint: two RGB sets at +0x68 and +0x78 (their alphas at +0x74 and +0x84 left
// alone), 1.0 for a white entry; STYLE: AUTO's gray, as read live.
constexpr ptrdiff_t kGlyphTint[6] = {0x68, 0x6C, 0x70, 0x78, 0x7C, 0x80};
constexpr float kLockedTint[6] = {0.412f, 0.439f, 0.494f, 0.412f, 0.439f, 0.494f};
constexpr int32_t kLockedStyle = 5;  // as the controls screen's STYLE: AUTO
constexpr int kLockAt = 7;  // ASSIST, a blank, the padlock
constexpr char kAssist[kAssistLength + 1] = "ASSIST\0\0\0\0\0";

// Reads the glyph pointers of a live label of N glyphs, or returns false.
template <int N>
bool glyphs_of(BYTE* child, BYTE* (&glyphs)[N]) {
    if (!label_glyphs(pointer_at(child, widget::kLabel), N, glyphs)) return false;
    for (BYTE* g : glyphs) {
        int32_t released = 1;
        if (!read(g, sprite::kReleased, released) || released != 0 ||
            frame_list(g, 'Z') == nullptr) {
            return false;
        }
    }
    return true;
}

// Whether `wanted` (not null) is one of `menu`'s panels (up to kMaxPanels).
bool lists_panel(const BYTE* menu, const BYTE* wanted) {
    const int32_t panels = panel_count(menu);
    for (int32_t p = 0; panels <= kMaxPanels && p < panels; ++p) {
        if (panel_at(menu, p) == wanted) return true;
    }
    return false;
}

// Calls fn(panel, index, child) for each child of each of `menu`'s panels (up to kMaxPanels)
// whose children read (up to kMaxChildren of them), skipping those that hold none, until fn
// returns true; returns whether one did.
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

// Children `from` to `to` - 1 of a panel's list into `run`, each live and numbered by its place
// (widget::kIndex); false if any is not.
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

BYTE* ArchipelagoChoice::choice_in(BYTE* menu, const GameAddresses& a, void* expected_callback) {
    BYTE* panel = panel_at(menu, kPanelIndex);
    void* handler = pointer_at(panel, panel::kHandler);
    if (handler == nullptr ||
        (handler != a.panel_handler_fn && (a.hooks.panel == nullptr || handler != a.hooks.panel)) ||
        child_count(panel) != 4) {
        return nullptr;
    }
    BYTE* child = child_at(panel, 1);
    return live_widget(child, a.widget_handler_fn, expected_callback) ? child : nullptr;
}

bool ArchipelagoChoice::menu_open(const GameAddresses& a) const {
    return is_open(a.current_menu, menu_);
}

bool ArchipelagoChoice::put_back(const GameAddresses& a, WidgetCallback callback) {
    // Only into a widget whose menu is still open, still holding this helper's callback.
    if (!menu_open(a) || choice_in(menu_, a, reinterpret_cast<void*>(callback)) != child_) {
        return false;
    }
    BYTE* glyphs[kLabelLength] = {};
    if (glyphs_of(child_, glyphs)) {
        for (int i = 0; i < kLabelLength; ++i) {
            at<int32_t>(glyphs[i], sprite::kFrameIndex) = frames_[i];
            at<BYTE*>(glyphs[i], sprite::kFrameShown) = frame_ptrs_[i];
        }
    }
    // The callback goes back even if the label changed shape: the widget is alive and would
    // otherwise keep pointing into the helper.
    at<void*>(child_, widget::kCallback) = a.rando_menu_fn;
    return true;
}

void ArchipelagoChoice::put_handler_back(const GameAddresses& a) {
    if (hook_panel_ == nullptr) return;
    if (!is_open(a.current_menu, hook_menu_)) {
        // Closed: its panels were freed with it (docs/re/memory.md, "The game-mode panel").
        hook_menu_ = hook_panel_ = nullptr;
        return;
    }
    // Only on the panel still in the menu's place: one the menu no longer lists is not known
    // to be alive, so it stays remembered (and the helper loaded) until the menu closes.
    void* handler = pointer_at(hook_panel_, panel::kHandler);
    if (panel_at(hook_menu_, kPanelIndex) != hook_panel_ || handler == nullptr) return;
    if (handler == a.hooks.panel &&
        !swap_pointer(hook_panel_, panel::kHandler, a.hooks.panel, a.panel_handler_fn)) {
        return;  // still the helper's: kept, so an unload refuses
    }
    hook_menu_ = hook_panel_ = nullptr;
}

bool ArchipelagoChoice::install(const GameAddresses& a, WidgetCallback callback) {
    BYTE* top = pointer_at(a.current_menu);
    if (child_ != nullptr && top == menu_ &&
        choice_in(top, a, reinterpret_cast<void*>(callback)) == child_) {
        return false;  // still installed on the menu that is up
    }
    restore(a, callback);
    if (top == nullptr || hook_panel_ != nullptr) return false;
    BYTE* child = choice_in(top, a, a.rando_menu_fn);
    BYTE* glyphs[kLabelLength] = {};
    if (child == nullptr || !glyphs_of(child, glyphs)) return false;
    // A label already reading ARCHIPELAGO (left by `release`) saves RANDOMIZER as its original.
    const bool already = glyphs_show(glyphs, kLabel);
    for (int i = 0; i < kLabelLength; ++i) {
        BYTE* g = glyphs[i];
        if (already) {
            int32_t r = static_cast<unsigned char>(kRandomizer[i]);
            frames_[i] = r;
            frame_ptrs_[i] = at<BYTE*>(g, sprite::kFrameList) + r * sprite::kFrameSize;
        } else {
            frames_[i] = at<int32_t>(g, sprite::kFrameIndex);
            frame_ptrs_[i] = at<BYTE*>(g, sprite::kFrameShown);
        }
        set_frame(g, static_cast<unsigned char>(kLabel[i]));  // glyphs_of: count > 'Z'
    }
    at<void*>(child, widget::kCallback) = reinterpret_cast<void*>(callback);
    // The panel's handler the helper's, so the hover border is fitted in the frame it is laid
    // out (only over the game's own; checked by choice_in on the way).
    BYTE* panel = panel_at(top, kPanelIndex);
    if (a.hooks.panel != nullptr &&
        swap_pointer(panel, panel::kHandler, a.panel_handler_fn, a.hooks.panel)) {
        hook_menu_ = top;
        hook_panel_ = panel;
    }
    menu_ = top;
    child_ = child;
    return true;
}

bool ArchipelagoChoice::fit_border(const GameAddresses& a) {
    if (child_ == nullptr || !menu_open(a)) return false;
    BYTE* panel = panel_at(menu_, kPanelIndex);
    int32_t selected = -1, width = 0;
    BYTE* label = nullptr;
    double label_x = 0;
    if (child_at(panel, 1) != child_ || !read(panel, kPanelSelected, selected) || selected != 1 ||
        !read(panel, kPanelBorderWidth, width) || width != kGameBorderWidth ||
        !read(child_, widget::kLabel, label) || !read(label, sprite::kX, label_x)) {
        return false;
    }
    BYTE* pieces[kBorderPieces] = {};
    if (!safe_read(panel + kPanelBorderPieces, pieces, sizeof(pieces))) return false;
    for (BYTE* piece : pieces) {
        BYTE probe[kEdgeLocalRight + 8];
        if (piece == nullptr || !safe_read(piece, probe, sizeof(probe)) ||
            !writable(piece, sizeof(probe))) {
            return false;
        }
    }
    // Only a border as the game laid it out around this label: its top-right piece where the
    // top edge ends, 80.1 past the label's x (once moved, it is 8 further and left alone).
    double right_x = 0;
    read(pieces[kTopRight], sprite::kX, right_x);
    if (std::fabs(right_x - (label_x + kEdgeAsLaid)) > 0.5) return false;
    for (int i : {kTopRight, kRight, kBottomRight}) {
        for (ptrdiff_t field : {sprite::kX, kPieceLeft, kPieceRight}) {
            at<double>(pieces[i], field) += kBorderMove;
        }
    }
    for (int i : {kTop, kBottom}) {
        at<float>(pieces[i], kEdgeLength) += static_cast<float>(kBorderMove);
        for (ptrdiff_t field : {kEdgeLocalRight, kPieceRight, kEdgeQuadRight1, kEdgeQuadRight2}) {
            at<double>(pieces[i], field) += kBorderMove;
        }
    }
    return true;
}

bool ArchipelagoChoice::release(const GameAddresses& a, WidgetCallback callback) {
    put_handler_back(a);
    if (child_ == nullptr) return false;
    bool put = menu_open(a) && choice_in(menu_, a, reinterpret_cast<void*>(callback)) == child_;
    if (put) at<void*>(child_, widget::kCallback) = a.rando_menu_fn;
    child_ = nullptr;
    menu_ = nullptr;
    return put;
}

bool ArchipelagoChoice::restore(const GameAddresses& a, WidgetCallback callback) {
    put_handler_back(a);
    if (child_ == nullptr) return false;
    bool put = put_back(a, callback);
    child_ = nullptr;
    menu_ = nullptr;
    return put;
}

BYTE* AssistLock::assist_in(BYTE* menu, const GameAddresses& a, void* expected_callback,
                            BYTE** panel_out, int32_t* index_out) {
    if (expected_callback == nullptr) return nullptr;
    BYTE* found = nullptr;
    for_each_child(menu, [&](BYTE* panel, int32_t index, BYTE* child) {
        if (!live_widget(child, a.widget_handler_fn, expected_callback)) return false;
        BYTE* glyphs[kAssistLength] = {};
        if (!glyphs_of(child, glyphs) || !glyphs_show(glyphs, {kAssist, kAssistLength})) {
            return false;
        }
        if (panel_out != nullptr) *panel_out = panel;
        if (index_out != nullptr) *index_out = index;
        found = child;
        return true;
    });
    return found;
}

bool AssistLock::still_locked(const GameAddresses& a) const {
    if (child_ == nullptr) return false;
    bool locked = false;
    for_each_child(menu_, [&](BYTE*, int32_t, BYTE* child) {
        if (child != child_) return false;
        locked = live_widget(child, a.widget_handler_fn, nullptr);  // its callback cleared
        return true;  // found: the first holding it decides
    });
    return locked;
}

namespace {

// The locked look: every glyph in the locked style under the locked palette (when the palette
// table gives it), the padlock in the eighth.
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
    // The padlock only if the glyph has that many frames, as the game's own text routine
    // checks a character against +0x1BC.
    set_frame(glyphs[kLockAt], kLockGlyph);
}

}  // namespace

bool AssistLock::install(const GameAddresses& a) {
    BYTE* top = pointer_at(a.current_menu);
    if (child_ != nullptr) {
        if (is_open(a.current_menu, menu_) && still_locked(a)) {
            // Still locked, with its menu open (on top or under another). The options menu takes
            // another palette after some screens (the map's 26), so the locked look is pinned
            // again whenever it is the menu up.
            BYTE* glyphs[kAssistLength] = {};
            if (top == menu_ && glyphs_of(child_, glyphs)) draw_locked(glyphs, a);
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
    return true;
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
    // The callback goes back even if the label changed meanwhile: the widget is alive, and ASSIST
    // would otherwise stay shut.
    bool put = is_open(a.current_menu, menu_) && still_locked(a);
    if (put) {
        BYTE* glyphs[kAssistLength] = {};
        if (glyphs_of(child_, glyphs)) {
            for (int i = 0; i < kAssistLength; ++i) {
                BYTE* g = glyphs[i];
                // Frames and palettes derived afresh from what the glyph holds now and the
                // palette table, not the pointers saved at install.
                set_frame(g, frames_[i]);
                at<int32_t>(g, sprite::kStyle) = styles_[i];
                BYTE* palette = palette_numbers_[i] < 0
                                    ? nullptr
                                    : pointer_at(a.palette_table, palette_numbers_[i] * 8);
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
    }
    child_ = nullptr;
    menu_ = nullptr;
    panel_ = nullptr;
    moved_from_ = -1;
    return put;
}

}  // namespace zap
