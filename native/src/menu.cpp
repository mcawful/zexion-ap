#include "menu.h"

#include <cstring>

namespace zap {

namespace {

constexpr ptrdiff_t kPanelIndex = 3;
constexpr ptrdiff_t kPanelChildren = 0x1E0, kPanelCapacity = 0x1E8, kPanelHandler = 0x2F8;
constexpr ptrdiff_t kChildLabel = 0x1F0, kChildCallback = 0x2C0, kChildHandler = 0x2C8;
constexpr ptrdiff_t kLabelCapacity = 0x1D8, kLabelGlyphs = 0x228;
constexpr ptrdiff_t kGlyphFrameTable = 0x1A8, kGlyphFrame = 0x1B0, kGlyphFrameCount = 0x1BC;
constexpr ptrdiff_t kGlyphFrameIndex = 0x1C4;
constexpr ptrdiff_t kFrameSize = 0x38;
constexpr char kLabel[kLabelLength + 1] = "ARCHIPELAGO";

template <typename T>
bool read(const BYTE* base, ptrdiff_t offset, T& out) {
    return base != nullptr && safe_read(base + offset, &out, sizeof(T));
}

// Reads the glyph pointers of a label, or returns false.
bool glyphs_of(BYTE* child, BYTE* (&glyphs)[kLabelLength]) {
    BYTE* label = nullptr;
    int32_t capacity = 0;
    BYTE** list = nullptr;
    if (!read(child, kChildLabel, label) || !read(label, kLabelCapacity, capacity) ||
        capacity != kLabelLength || !read(label, kLabelGlyphs, list) ||
        !safe_read(list, glyphs, sizeof(glyphs))) {
        return false;
    }
    for (BYTE* g : glyphs) {
        int32_t count = 0;
        BYTE* table = nullptr;
        if (!read(g, kGlyphFrameCount, count) || !read(g, kGlyphFrameTable, table) || count <= 'Z' ||
            table == nullptr) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool safe_read(const void* from, void* to, size_t size) {
    __try {
        std::memcpy(to, from, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

BYTE* ArchipelagoChoice::find_choice(const MenuAddresses& a, void* expected_callback) const {
    BYTE* menu = nullptr;
    BYTE** panels = nullptr;
    BYTE* panel = nullptr;
    void* handler = nullptr;
    int32_t capacity = 0;
    BYTE** children = nullptr;
    BYTE* child = nullptr;
    void* callback = nullptr;
    void* child_handler = nullptr;
    if (!read(a.current_menu, 0, menu) || !read(menu, 0, panels) ||
        !read(reinterpret_cast<BYTE*>(panels), kPanelIndex * 8, panel) ||
        !read(panel, kPanelHandler, handler) || handler != a.panel_handler ||
        !read(panel, kPanelCapacity, capacity) || capacity != 4 ||
        !read(panel, kPanelChildren, children) ||
        !read(reinterpret_cast<BYTE*>(children), 8, child) ||
        !read(child, kChildCallback, callback) || callback != expected_callback ||
        !read(child, kChildHandler, child_handler) || child_handler != a.widget_handler) {
        return nullptr;
    }
    return child;
}

bool ArchipelagoChoice::put_back(const MenuAddresses& a, WidgetCallback callback) {
    void* current = nullptr;
    void* handler = nullptr;
    BYTE* glyphs[kLabelLength] = {};
    // Only a widget that still holds this helper's callback and the widget handler, with the
    // same label shape: anything else is no longer the choice this relabeled.
    if (!read(child_, kChildCallback, current) || current != reinterpret_cast<void*>(callback) ||
        !read(child_, kChildHandler, handler) || handler != a.widget_handler ||
        !glyphs_of(child_, glyphs)) {
        return false;
    }
    for (int i = 0; i < kLabelLength; ++i) {
        *reinterpret_cast<int32_t*>(glyphs[i] + kGlyphFrameIndex) = frames_[i];
        *reinterpret_cast<BYTE**>(glyphs[i] + kGlyphFrame) = frame_ptrs_[i];
    }
    *reinterpret_cast<void**>(child_ + kChildCallback) = a.rando_menu;
    return true;
}

bool ArchipelagoChoice::install(const MenuAddresses& a, WidgetCallback callback) {
    if (child_ != nullptr) {
        if (find_choice(a, reinterpret_cast<void*>(callback)) == child_) {
            return false;  // still installed on the panel that is up
        }
        // The panel is no longer the one up: put RANDOMIZER back at once, while the widget most
        // likely still exists, rather than keep a pointer into a menu the game may free.
        put_back(a, callback);
        child_ = nullptr;
    }
    BYTE* child = find_choice(a, a.rando_menu);
    BYTE* glyphs[kLabelLength] = {};
    if (child == nullptr || !glyphs_of(child, glyphs)) return false;
    for (int i = 0; i < kLabelLength; ++i) {
        BYTE* g = glyphs[i];
        frames_[i] = *reinterpret_cast<int32_t*>(g + kGlyphFrameIndex);
        frame_ptrs_[i] = *reinterpret_cast<BYTE**>(g + kGlyphFrame);
        int32_t c = static_cast<unsigned char>(kLabel[i]);
        *reinterpret_cast<int32_t*>(g + kGlyphFrameIndex) = c;
        *reinterpret_cast<BYTE**>(g + kGlyphFrame) =
            *reinterpret_cast<BYTE**>(g + kGlyphFrameTable) + c * kFrameSize;
    }
    *reinterpret_cast<void**>(child + kChildCallback) = reinterpret_cast<void*>(callback);
    child_ = child;
    return true;
}

bool ArchipelagoChoice::restore(const MenuAddresses& a, WidgetCallback callback) {
    if (child_ == nullptr) return false;
    bool put = put_back(a, callback);
    child_ = nullptr;
    return put;
}

}  // namespace zap
