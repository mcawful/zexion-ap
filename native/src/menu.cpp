#include "menu.h"

#include <cstring>

namespace zap {

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
constexpr char kLabel[kLabelLength + 1] = "ARCHIPELAGO";
constexpr char kRandomizer[kLabelLength + 1] = "RANDOMIZER ";  // the game's own label

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

// Reads the glyph pointers of a live label, or returns false.
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
        !read(panel, kPanelHandler, handler) || handler != a.panel_handler ||
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

bool ArchipelagoChoice::menu_open(const MenuAddresses& a) const {
    BYTE* menu = nullptr;
    if (!read(a.current_menu, 0, menu)) return false;
    for (int i = 0; i < kMaxMenus && menu != nullptr; ++i) {
        if (menu == menu_) return true;
        if (!read(menu, kMenuBelow, menu)) return false;
    }
    return false;
}

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
    if (top == nullptr) return false;
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
    menu_ = top;
    child_ = child;
    return true;
}

bool ArchipelagoChoice::release(const MenuAddresses& a, WidgetCallback callback) {
    if (child_ == nullptr) return false;
    bool put = menu_open(a) && choice_in(menu_, a, reinterpret_cast<void*>(callback)) == child_;
    if (put) *reinterpret_cast<void**>(child_ + kChildCallback) = a.rando_menu;
    child_ = nullptr;
    menu_ = nullptr;
    return put;
}

bool ArchipelagoChoice::restore(const MenuAddresses& a, WidgetCallback callback) {
    if (child_ == nullptr) return false;
    bool put = put_back(a, callback);
    child_ = nullptr;
    menu_ = nullptr;
    return put;
}

}  // namespace zap
