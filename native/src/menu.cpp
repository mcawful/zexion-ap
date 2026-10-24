#include "menu.h"

#include <cmath>
#include <initializer_list>
#include <cstring>

namespace zap {

// Padlock 1 of lab/icons/locks.png, the owner's pick: 7 wide like the font's letters.
const char kLockRows[8][9] = {
    "..###...", ".#...#..", ".#...#..", "#######.",
    "###.###.", "###.###.", "#######.", "........",
};

namespace {

constexpr ptrdiff_t kMenuBelow = 0x78;
constexpr int kMaxMenus = 64;  // far more than the game ever stacks
constexpr ptrdiff_t kPanelIndex = 3;
constexpr ptrdiff_t kPanelChildren = 0x1E0, kPanelCapacity = 0x1E8, kPanelHandler = 0x2F8;
constexpr ptrdiff_t kReleased = 0x1C8;  // widgets and glyphs: nonzero once released
constexpr ptrdiff_t kChildLabel = 0x1F0, kChildCallback = 0x2C0, kChildHandler = 0x2C8;
constexpr ptrdiff_t kLabelCapacity = 0x1D8, kLabelGlyphs = 0x228;
constexpr ptrdiff_t kGlyphFrameTable = 0x1A8, kGlyphFrame = 0x1B0, kGlyphFrameCount = 0x1BC;
constexpr ptrdiff_t kGlyphFrameIndex = 0x1C4;
constexpr ptrdiff_t kFrameSize = 0x38;
// The border the panel draws round the button under the mouse (docs/re/memory.md, "The
// game-mode panel"): on hover the panel's handler makes it 8 pixels wider than the label's 11
// glyphs (0xAE531 to 0xAE545), from 8 pixels left of the label, out of eight 8 x 8 pieces whose
// pointers the panel holds from +0x278: top-left, top, top-right, right, bottom-right, bottom,
// bottom-left, left. Each is an object with its x at +0x40 and the x of the rectangle it is
// drawn in from +0x128 to +0x138 (doubles); the top and bottom edges also keep their length at
// +0x114 (a float), their right at +0x158, and the right-hand corners of the quad they are drawn
// as at +0xA8 and +0xB8 (doubles, read live: 80.1, as the length). That is even round RANDOMIZER, whose eleventh
// glyph is a blank, but puts the right-hand pieces on ARCHIPELAGO's last letter.
constexpr ptrdiff_t kPanelBorderPieces = 0x278, kPanelBorderWidth = 0x2B8;
constexpr int kBorderPieces = 8;
constexpr int kTop = 1, kTopRight = 2, kRight = 3, kBottomRight = 4, kBottom = 5;
constexpr ptrdiff_t kPieceX = 0x40, kPieceLeft = 0x128, kPieceRight = 0x138;
constexpr ptrdiff_t kEdgeLength = 0x114, kEdgeLocalRight = 0x158;
constexpr ptrdiff_t kEdgeQuadRight1 = 0xA8, kEdgeQuadRight2 = 0xB8;
constexpr int32_t kGameBorderWidth = kLabelLength * 8 + 8;  // 96
constexpr double kEdgeAsLaid = 80.1;  // the game's top edge, from the label's x (read live)
constexpr double kBorderMove = 8.0;   // one character: as far past the label as on its left
constexpr char kLabel[kLabelLength + 1] = "ARCHIPELAGO";
constexpr char kRandomizer[kLabelLength + 1] = "RANDOMIZER ";  // the game's own label
constexpr ptrdiff_t kMenuPanelCount = 8;  // 32-bit, after the panel list at +0
constexpr int32_t kMaxPanels = 16, kMaxChildren = 64;
constexpr ptrdiff_t kPanelSelectable = 0x1EC;  // 32-bit: the first this many children are selectable
constexpr ptrdiff_t kPanelSelected = 0x2DC;    // 32-bit: the selected child, -1 for none
constexpr ptrdiff_t kChildIndex = 0x1E0;       // 32-bit: a child's place among its panel's
constexpr ptrdiff_t kGlyphStyle = 0x1D0;  // 0x15 as the options menu builds ASSIST
constexpr ptrdiff_t kGlyphPalette = 0x198, kGlyphPaletteNumber = 0x1A0;  // object, 16-bit number
constexpr int16_t kLockedPalette = 0;  // as the controls screen's STYLE: AUTO
// A glyph's color tint: two RGB sets at +0x68 and +0x78 (their alphas at +0x74 and +0x84 left
// alone), 1.0 for a white entry; STYLE: AUTO's grey, as read live.
constexpr ptrdiff_t kGlyphTint[6] = {0x68, 0x6C, 0x70, 0x78, 0x7C, 0x80};
constexpr float kLockedTint[6] = {0.412f, 0.439f, 0.494f, 0.412f, 0.439f, 0.494f};
constexpr int32_t kLockedStyle = 5;  // as the controls screen's STYLE: AUTO
constexpr int kLockAt = 7;  // ASSIST, a blank, the padlock
constexpr char kAssist[kAssistLength + 1] = "ASSIST\0\0\0\0\0";

template <typename T>
bool read(const BYTE* base, ptrdiff_t offset, T& out) {
    return base != nullptr && safe_read(base + offset, &out, sizeof(T));
}

bool readable(const BYTE* p, size_t size) {
    const BYTE* end = p + size;
    while (p < end) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(p, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT) {
            return false;
        }
        DWORD protect = info.Protect;
        if ((protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
            (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
            return false;
        }
        p = static_cast<const BYTE*>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

int copy_or_fault(const void* from, void* to, size_t size) {
    __try {
        std::memcpy(to, from, size);
        return 1;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                 : EXCEPTION_CONTINUE_SEARCH) {
        return 0;
    }
}

// Reads the glyph pointers of a live label of N glyphs, or returns false.
template <int N>
bool glyphs_of(BYTE* child, BYTE* (&glyphs)[N]) {
    BYTE* label = nullptr;
    int32_t capacity = 0;
    BYTE** list = nullptr;
    if (!read(child, kChildLabel, label) || !read(label, kLabelCapacity, capacity) ||
        capacity != N || !read(label, kLabelGlyphs, list) ||
        !safe_read(list, glyphs, sizeof(glyphs))) {
        return false;
    }
    for (BYTE* g : glyphs) {
        int32_t released = 1, count = 0;
        BYTE* table = nullptr;
        if (!read(g, kReleased, released) || released != 0 || !read(g, kGlyphFrameCount, count) ||
            !read(g, kGlyphFrameTable, table) || count <= 'Z' || table == nullptr) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool writable(const void* at, size_t size) {
    const BYTE* p = static_cast<const BYTE*>(at);
    const BYTE* end = p + size;
    while (p < end) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(p, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
            (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) == 0) {
            return false;
        }
        p = static_cast<const BYTE*>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

bool safe_read(const void* from, void* to, size_t size) {
    // The query keeps the read off guard pages (another thread's stack), which an exception
    // handler would disarm; the handler covers memory freed in between.
    return from != nullptr && readable(static_cast<const BYTE*>(from), size) &&
           copy_or_fault(from, to, size) == 1;
}

BYTE* ArchipelagoChoice::choice_in(BYTE* menu, const MenuAddresses& a, void* expected_callback) {
    BYTE** panels = nullptr;
    BYTE* panel = nullptr;
    void* handler = nullptr;
    int32_t capacity = 0;
    BYTE** children = nullptr;
    BYTE* child = nullptr;
    int32_t released = 1;
    void* callback = nullptr;
    void* child_handler = nullptr;
    if (!read(menu, 0, panels) ||
        !read(reinterpret_cast<BYTE*>(panels), kPanelIndex * 8, panel) ||
        !read(panel, kPanelHandler, handler) ||
        (handler != a.panel_handler && (a.panel_hook == nullptr || handler != a.panel_hook)) ||
        !read(panel, kPanelCapacity, capacity) || capacity != 4 ||
        !read(panel, kPanelChildren, children) ||
        !read(reinterpret_cast<BYTE*>(children), 8, child) || !read(child, kReleased, released) ||
        released != 0 || !read(child, kChildCallback, callback) ||
        callback != expected_callback || !read(child, kChildHandler, child_handler) ||
        child_handler != a.widget_handler) {
        return nullptr;
    }
    return child;
}

namespace {

// Whether `wanted` is still on the list of open menus.
bool is_open(const MenuAddresses& a, const BYTE* wanted) {
    BYTE* menu = nullptr;
    if (!read(a.current_menu, 0, menu)) return false;
    for (int i = 0; i < kMaxMenus && menu != nullptr; ++i) {
        if (menu == wanted) return true;
        if (!read(menu, kMenuBelow, menu)) return false;
    }
    return false;
}

}  // namespace

bool ArchipelagoChoice::menu_open(const MenuAddresses& a) const { return is_open(a, menu_); }

bool ArchipelagoChoice::put_back(const MenuAddresses& a, WidgetCallback callback) {
    // Only into a widget whose menu is still open, still holding this helper's callback.
    if (!menu_open(a) || choice_in(menu_, a, reinterpret_cast<void*>(callback)) != child_) {
        return false;
    }
    BYTE* glyphs[kLabelLength] = {};
    if (glyphs_of(child_, glyphs)) {
        for (int i = 0; i < kLabelLength; ++i) {
            *reinterpret_cast<int32_t*>(glyphs[i] + kGlyphFrameIndex) = frames_[i];
            *reinterpret_cast<BYTE**>(glyphs[i] + kGlyphFrame) = frame_ptrs_[i];
        }
    }
    // The callback goes back even if the label changed shape: the widget is alive and would
    // otherwise keep pointing into the helper.
    *reinterpret_cast<void**>(child_ + kChildCallback) = a.rando_menu;
    return true;
}

BYTE* ArchipelagoChoice::panel_of(BYTE* menu) {
    BYTE** panels = nullptr;
    BYTE* panel = nullptr;
    if (!read(menu, 0, panels) || !read(reinterpret_cast<BYTE*>(panels), kPanelIndex * 8, panel)) {
        return nullptr;
    }
    return panel;
}

void ArchipelagoChoice::put_handler_back(const MenuAddresses& a) {
    if (hook_panel_ == nullptr) return;
    if (!is_open(a, hook_menu_)) {
        // Closed: its panels were freed with it (docs/re/memory.md, "The game-mode panel").
        hook_menu_ = hook_panel_ = nullptr;
        return;
    }
    // Only on the panel still in the menu's place: one the menu no longer lists is not known
    // to be alive, so it stays remembered (and the helper loaded) until the menu closes.
    void* handler = nullptr;
    if (panel_of(hook_menu_) != hook_panel_ || !read(hook_panel_, kPanelHandler, handler)) return;
    if (handler == a.panel_hook) {
        *reinterpret_cast<void**>(hook_panel_ + kPanelHandler) = a.panel_handler;
    }
    hook_menu_ = hook_panel_ = nullptr;
}

bool ArchipelagoChoice::install(const MenuAddresses& a, WidgetCallback callback) {
    BYTE* top = nullptr;
    if (!read(a.current_menu, 0, top)) top = nullptr;
    if (child_ != nullptr) {
        if (top == menu_ && choice_in(top, a, reinterpret_cast<void*>(callback)) == child_) {
            return false;  // still installed on the menu that is up
        }
        put_back(a, callback);
        child_ = nullptr;
        menu_ = nullptr;
    }
    put_handler_back(a);
    if (top == nullptr || hook_panel_ != nullptr) return false;
    BYTE* child = choice_in(top, a, a.rando_menu);
    BYTE* glyphs[kLabelLength] = {};
    if (child == nullptr || !glyphs_of(child, glyphs)) return false;
    // A label left reading ARCHIPELAGO by an earlier release (should its menu ever outlive the
    // new game) is not taken for the original: RANDOMIZER is.
    bool already = true;
    for (int i = 0; i < kLabelLength; ++i) {
        if (*reinterpret_cast<int32_t*>(glyphs[i] + kGlyphFrameIndex) !=
            static_cast<unsigned char>(kLabel[i])) {
            already = false;
        }
    }
    for (int i = 0; i < kLabelLength; ++i) {
        BYTE* g = glyphs[i];
        if (already) {
            int32_t r = static_cast<unsigned char>(kRandomizer[i]);
            frames_[i] = r;
            frame_ptrs_[i] = *reinterpret_cast<BYTE**>(g + kGlyphFrameTable) + r * kFrameSize;
        } else {
            frames_[i] = *reinterpret_cast<int32_t*>(g + kGlyphFrameIndex);
            frame_ptrs_[i] = *reinterpret_cast<BYTE**>(g + kGlyphFrame);
        }
        int32_t c = static_cast<unsigned char>(kLabel[i]);
        *reinterpret_cast<int32_t*>(g + kGlyphFrameIndex) = c;
        *reinterpret_cast<BYTE**>(g + kGlyphFrame) =
            *reinterpret_cast<BYTE**>(g + kGlyphFrameTable) + c * kFrameSize;
    }
    *reinterpret_cast<void**>(child + kChildCallback) = reinterpret_cast<void*>(callback);
    // The panel's handler the helper's, so the hover border is fitted in the frame it is laid
    // out (only over the game's own; checked by choice_in on the way).
    BYTE* panel = panel_of(top);
    void* handler = nullptr;
    if (panel != nullptr && a.panel_hook != nullptr && read(panel, kPanelHandler, handler) &&
        handler == a.panel_handler) {
        *reinterpret_cast<void**>(panel + kPanelHandler) = a.panel_hook;
        hook_menu_ = top;
        hook_panel_ = panel;
    }
    menu_ = top;
    child_ = child;
    return true;
}

bool ArchipelagoChoice::fit_border(const MenuAddresses& a) {
    if (child_ == nullptr || !menu_open(a)) return false;
    BYTE** panels = nullptr;
    BYTE* panel = nullptr;
    BYTE** children = nullptr;
    BYTE* child = nullptr;
    int32_t selected = -1, width = 0;
    BYTE* label = nullptr;
    double label_x = 0;
    if (!read(menu_, 0, panels) ||
        !read(reinterpret_cast<BYTE*>(panels), kPanelIndex * 8, panel) ||
        !read(panel, kPanelChildren, children) ||
        !read(reinterpret_cast<BYTE*>(children), 8, child) || child != child_ ||
        !read(panel, kPanelSelected, selected) || selected != 1 ||
        !read(panel, kPanelBorderWidth, width) || width != kGameBorderWidth ||
        !read(child_, kChildLabel, label) || !read(label, kPieceX, label_x)) {
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
    // Only a border as the game laid it out round this label: its top-right piece where the
    // top edge ends, 80.1 past the label's x (once moved, it is 8 further and left alone).
    double right_x = 0;
    read(pieces[kTopRight], kPieceX, right_x);
    if (std::fabs(right_x - (label_x + kEdgeAsLaid)) > 0.5) return false;
    for (int i : {kTopRight, kRight, kBottomRight}) {
        for (ptrdiff_t field : {kPieceX, kPieceLeft, kPieceRight}) {
            *reinterpret_cast<double*>(pieces[i] + field) += kBorderMove;
        }
    }
    for (int i : {kTop, kBottom}) {
        *reinterpret_cast<float*>(pieces[i] + kEdgeLength) += static_cast<float>(kBorderMove);
        for (ptrdiff_t field : {kEdgeLocalRight, kPieceRight, kEdgeQuadRight1, kEdgeQuadRight2}) {
            *reinterpret_cast<double*>(pieces[i] + field) += kBorderMove;
        }
    }
    return true;
}

bool ArchipelagoChoice::release(const MenuAddresses& a, WidgetCallback callback) {
    put_handler_back(a);
    if (child_ == nullptr) return false;
    bool put = menu_open(a) && choice_in(menu_, a, reinterpret_cast<void*>(callback)) == child_;
    if (put) *reinterpret_cast<void**>(child_ + kChildCallback) = a.rando_menu;
    child_ = nullptr;
    menu_ = nullptr;
    return put;
}

bool ArchipelagoChoice::restore(const MenuAddresses& a, WidgetCallback callback) {
    put_handler_back(a);
    if (child_ == nullptr) return false;
    bool put = put_back(a, callback);
    child_ = nullptr;
    menu_ = nullptr;
    return put;
}

BYTE* AssistLock::assist_in(BYTE* menu, const MenuAddresses& a, void* expected_callback,
                            BYTE** panel_out, int32_t* index_out) {
    BYTE** panels = nullptr;
    int32_t count = 0;
    if (expected_callback == nullptr || !read(menu, 0, panels) ||
        !read(menu, kMenuPanelCount, count) || count <= 0 || count > kMaxPanels) {
        return nullptr;
    }
    for (int32_t p = 0; p < count; ++p) {
        BYTE* panel = nullptr;
        int32_t capacity = 0;
        BYTE** children = nullptr;
        if (!read(reinterpret_cast<BYTE*>(panels), p * 8, panel) || panel == nullptr ||
            !read(panel, kPanelCapacity, capacity) || capacity <= 0 ||
            capacity > kMaxChildren || !read(panel, kPanelChildren, children)) {
            continue;
        }
        for (int32_t c = 0; c < capacity; ++c) {
            BYTE* child = nullptr;
            int32_t released = 1;
            void* callback = nullptr;
            void* handler = nullptr;
            if (!read(reinterpret_cast<BYTE*>(children), c * 8, child) || child == nullptr ||
                !read(child, kReleased, released) || released != 0 ||
                !read(child, kChildCallback, callback) || callback != expected_callback ||
                !read(child, kChildHandler, handler) || handler != a.widget_handler) {
                continue;
            }
            BYTE* glyphs[kAssistLength] = {};
            if (!glyphs_of(child, glyphs)) continue;
            bool reads_assist = true;
            for (int i = 0; i < kAssistLength; ++i) {
                int32_t ch = 0;
                if (!read(glyphs[i], kGlyphFrameIndex, ch) ||
                    ch != static_cast<unsigned char>(kAssist[i])) {
                    reads_assist = false;
                }
            }
            if (reads_assist) {
                if (panel_out != nullptr) *panel_out = panel;
                if (index_out != nullptr) *index_out = c;
                return child;
            }
        }
    }
    return nullptr;
}

bool AssistLock::still_locked(const MenuAddresses& a) const {
    BYTE** panels = nullptr;
    int32_t count = 0;
    if (child_ == nullptr || !read(menu_, 0, panels) || !read(menu_, kMenuPanelCount, count) ||
        count <= 0 || count > kMaxPanels) {
        return false;
    }
    for (int32_t p = 0; p < count; ++p) {
        BYTE* panel = nullptr;
        int32_t capacity = 0;
        BYTE** children = nullptr;
        if (!read(reinterpret_cast<BYTE*>(panels), p * 8, panel) || panel == nullptr ||
            !read(panel, kPanelCapacity, capacity) || capacity <= 0 ||
            capacity > kMaxChildren || !read(panel, kPanelChildren, children)) {
            continue;
        }
        for (int32_t c = 0; c < capacity; ++c) {
            BYTE* child = nullptr;
            if (!read(reinterpret_cast<BYTE*>(children), c * 8, child) || child != child_) {
                continue;
            }
            int32_t released = 1;
            void* held = a.assist_menu;
            void* handler = nullptr;
            return read(child, kReleased, released) && released == 0 &&
                   read(child, kChildCallback, held) && held == nullptr &&
                   read(child, kChildHandler, handler) && handler == a.widget_handler;
        }
    }
    return false;
}

namespace {

// The locked look: every glyph in the locked style under the locked palette (when the palette
// table gives it), the padlock in the eighth.
void draw_locked(BYTE* (&glyphs)[kAssistLength], const MenuAddresses& a) {
    BYTE* palette = nullptr;
    BYTE probe = 0;
    if (!read(a.palette_table, kLockedPalette * 8, palette) || !read(palette, 0, probe)) {
        palette = nullptr;  // no palette object readable there: the menu's own is kept
    }
    for (BYTE* g : glyphs) {
        *reinterpret_cast<int32_t*>(g + kGlyphStyle) = kLockedStyle;
        for (int t = 0; t < 6; ++t) *reinterpret_cast<float*>(g + kGlyphTint[t]) = kLockedTint[t];
        if (palette != nullptr) {
            *reinterpret_cast<void**>(g + kGlyphPalette) = palette;
            *reinterpret_cast<int16_t*>(g + kGlyphPaletteNumber) = kLockedPalette;
        }
    }
    // The padlock only if the glyph has that many frames, as the game's own text routine
    // checks a character against +0x1BC.
    BYTE* lock = glyphs[kLockAt];
    if (*reinterpret_cast<int32_t*>(lock + kGlyphFrameCount) <= kLockGlyph) return;
    *reinterpret_cast<int32_t*>(lock + kGlyphFrameIndex) = kLockGlyph;
    *reinterpret_cast<BYTE**>(lock + kGlyphFrame) =
        *reinterpret_cast<BYTE**>(lock + kGlyphFrameTable) + kLockGlyph * kFrameSize;
}

}  // namespace

bool AssistLock::install(const MenuAddresses& a) {
    BYTE* top = nullptr;
    if (!read(a.current_menu, 0, top)) top = nullptr;
    if (child_ != nullptr) {
        if (is_open(a, menu_) && still_locked(a)) {
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
    BYTE* child = assist_in(top, a, a.assist_menu, &panel, &index);
    BYTE* glyphs[kAssistLength] = {};
    if (child == nullptr || !glyphs_of(child, glyphs)) return false;
    for (int i = 0; i < kAssistLength; ++i) {
        BYTE* g = glyphs[i];
        frames_[i] = *reinterpret_cast<int32_t*>(g + kGlyphFrameIndex);
        frame_ptrs_[i] = *reinterpret_cast<BYTE**>(g + kGlyphFrame);
        styles_[i] = *reinterpret_cast<int32_t*>(g + kGlyphStyle);
        palettes_[i] = *reinterpret_cast<void**>(g + kGlyphPalette);
        palette_numbers_[i] = *reinterpret_cast<int16_t*>(g + kGlyphPaletteNumber);
        for (int t = 0; t < 6; ++t) tints_[i][t] = *reinterpret_cast<float*>(g + kGlyphTint[t]);
    }
    draw_locked(glyphs, a);
    *reinterpret_cast<void**>(child + kChildCallback) = nullptr;
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
    if (!read(panel, kPanelSelectable, selectable) || !read(panel, kPanelCapacity, capacity) ||
        !read(panel, kPanelChildren, children) || index < 0 || selectable <= index ||
        selectable > capacity || !writable(children, capacity * sizeof(BYTE*))) {
        return;  // not among the selectable ones, or not as expected: left where it is
    }
    // Every child from ASSIST to the last selectable one live and numbered by its place.
    BYTE* moved[kMaxChildren] = {};
    for (int32_t k = index; k < selectable; ++k) {
        int32_t released = 1, place = -1;
        if (!read(reinterpret_cast<BYTE*>(children), k * 8, moved[k]) || moved[k] == nullptr ||
            !read(moved[k], kReleased, released) || released != 0 ||
            !read(moved[k], kChildIndex, place) || place != k) {
            return;
        }
    }
    for (int32_t k = index; k + 1 < selectable; ++k) {
        children[k] = moved[k + 1];
        *reinterpret_cast<int32_t*>(moved[k + 1] + kChildIndex) = k;
    }
    children[selectable - 1] = moved[index];
    *reinterpret_cast<int32_t*>(moved[index] + kChildIndex) = selectable - 1;
    *reinterpret_cast<int32_t*>(panel + kPanelSelectable) = selectable - 1;
    // The cursor stays on the entry it was on (ASSIST's place goes to the entry after it).
    int32_t* selected = reinterpret_cast<int32_t*>(panel + kPanelSelected);
    if (*selected > index && *selected < selectable) --*selected;
    moved_from_ = index;
    selectable_ = selectable;
}

void AssistLock::move_back() {
    if (moved_from_ < 0 || panel_ == nullptr) return;
    // The panel it moved must still be one of its menu's panels.
    BYTE** panels = nullptr;
    int32_t count = 0;
    bool listed = false;
    if (read(menu_, 0, panels) && read(menu_, kMenuPanelCount, count) && count > 0 &&
        count <= kMaxPanels) {
        for (int32_t p = 0; p < count && !listed; ++p) {
            BYTE* panel = nullptr;
            listed = read(reinterpret_cast<BYTE*>(panels), p * 8, panel) && panel == panel_;
        }
    }
    if (!listed) {
        moved_from_ = -1;
        return;
    }
    int32_t index = moved_from_, selectable = selectable_;
    moved_from_ = -1;
    int32_t now = 0, capacity = 0;
    BYTE** children = nullptr;
    if (!read(panel_, kPanelSelectable, now) || now != selectable - 1 ||
        !read(panel_, kPanelCapacity, capacity) || selectable > capacity ||
        !read(panel_, kPanelChildren, children) ||
        !writable(children, capacity * sizeof(BYTE*))) {
        return;
    }
    BYTE* moved[kMaxChildren] = {};
    for (int32_t k = index; k < selectable; ++k) {
        int32_t released = 1, place = -1;
        if (!read(reinterpret_cast<BYTE*>(children), k * 8, moved[k]) || moved[k] == nullptr ||
            !read(moved[k], kReleased, released) || released != 0 ||
            !read(moved[k], kChildIndex, place) || place != k) {
            return;
        }
    }
    if (moved[selectable - 1] != child_) return;  // not as move_out left it
    for (int32_t k = selectable - 1; k > index; --k) {
        children[k] = moved[k - 1];
        *reinterpret_cast<int32_t*>(moved[k - 1] + kChildIndex) = k;
    }
    children[index] = child_;
    *reinterpret_cast<int32_t*>(child_ + kChildIndex) = index;
    *reinterpret_cast<int32_t*>(panel_ + kPanelSelectable) = selectable;
    int32_t* selected = reinterpret_cast<int32_t*>(panel_ + kPanelSelected);
    if (*selected >= index && *selected < selectable - 1) ++*selected;
}

bool AssistLock::restore(const MenuAddresses& a) {
    if (child_ == nullptr) return false;
    // The callback goes back even if the label changed meanwhile: the widget is alive, and ASSIST
    // would otherwise stay shut.
    bool put = is_open(a, menu_) && still_locked(a);
    if (put) {
        BYTE* glyphs[kAssistLength] = {};
        if (glyphs_of(child_, glyphs)) {
            for (int i = 0; i < kAssistLength; ++i) {
                BYTE* g = glyphs[i];
                // Frames and palettes derived afresh from what the glyph holds now and the
                // palette table, not the pointers saved at install.
                *reinterpret_cast<int32_t*>(g + kGlyphFrameIndex) = frames_[i];
                *reinterpret_cast<BYTE**>(g + kGlyphFrame) =
                    *reinterpret_cast<BYTE**>(g + kGlyphFrameTable) + frames_[i] * kFrameSize;
                *reinterpret_cast<int32_t*>(g + kGlyphStyle) = styles_[i];
                BYTE* palette = nullptr;
                BYTE probe = 0;
                if (palette_numbers_[i] < 0 ||
                    !read(a.palette_table, palette_numbers_[i] * 8, palette) ||
                    !read(palette, 0, probe)) {
                    palette = static_cast<BYTE*>(palettes_[i]);
                }
                *reinterpret_cast<void**>(g + kGlyphPalette) = palette;
                *reinterpret_cast<int16_t*>(g + kGlyphPaletteNumber) = palette_numbers_[i];
                for (int t = 0; t < 6; ++t) {
                    *reinterpret_cast<float*>(glyphs[i] + kGlyphTint[t]) = tints_[i][t];
                }
            }
        }
        *reinterpret_cast<void**>(child_ + kChildCallback) = a.assist_menu;
        move_back();
    }
    child_ = nullptr;
    menu_ = nullptr;
    panel_ = nullptr;
    moved_from_ = -1;
    return put;
}

}  // namespace zap
