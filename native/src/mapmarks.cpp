#include "mapmarks.h"

#include <algorithm>
#include <cstring>

#include "game_mem.h"

namespace zap {

namespace {

constexpr int kLocations = 154;
constexpr ptrdiff_t kItemTable = 0x26C;
constexpr ptrdiff_t kSettings = 0x87B;  // +0xCDDF4B less the section's +0xCDD6D0
constexpr int kObscureMajors = 29;
constexpr uint8_t kClassLimit = 0x24;  // values below hold an item's class in value % 12
constexpr uint8_t kMinorShown = 1, kMajorShown = 2, kMinorObscured = 7, kMajorObscured = 9;

}  // namespace

uint8_t important_value(uint8_t value, bool obscure_majors, bool ended) {
    if (value >= kClassLimit) return value;
    switch (value % 12) {
        case kMinorShown:
            return static_cast<uint8_t>(value + (kMajorShown - kMinorShown));
        case kMinorObscured:
            // Obscured as a minor: as a major, obscured too if majors are, else shown (0x7EECE).
            return static_cast<uint8_t>(obscure_majors && !ended
                                            ? value + (kMajorObscured - kMinorObscured)
                                            : value - (kMinorObscured - kMajorShown));
        default:
            return value;
    }
}

int mark_important(const BYTE* section, BYTE* grid, bool ended) {
    if (section == nullptr || grid == nullptr) return 0;
    const bool obscure_majors = section[kSettings + kObscureMajors] != 0;
    int changed = 0;
    for (int i = 0; i < kLocations; ++i) {
        if (section[kItemTable + i] != kProgressionStandIn) continue;
        auto x = field<uint16_t>(section, 4 * i), y = field<uint16_t>(section, 4 * i + 2);
        if (x >= kMapSide || y >= kMapSide) continue;
        BYTE& cell = grid[y * kMapSide + x];
        uint8_t raised = important_value(cell, obscure_majors, ended);
        if (raised != cell) {
            cell = raised;
            ++changed;
        }
    }
    return changed;
}

namespace {

// The map child's two handler slots: the game's per-frame loop calls +0x168 (0x7ABF3, the child
// its argument), and 0x46B4F calls +0x178; the map's builder puts its handler in both (0x16E951),
// read live holding 0x169E50 in both, 2026-10-25.
constexpr ptrdiff_t kChildHandlers[2] = {0x168, 0x178};
constexpr ptrdiff_t kInfoPanel = 0x300, kInfoChild = 0x8;
constexpr ptrdiff_t kScale = 0x158;  // on the info child's label (widget::kLabel)
constexpr ptrdiff_t kCursorChild = 0x18, kCursorX = 0x1E6, kCursorY = 0x1E4;
constexpr ptrdiff_t kNameChild = 0x18;
constexpr double kFineScale = 40.0;
const char* const kStandInNames[2] = {"SCRAP ENERGY", "SCRAP CHUNK"};

// The map's menu, its first panel and that panel's first child, if the top menu is the map's
// (its handler `handler`).
bool map_menu(BYTE* current_menu, void* handler, BYTE*& menu, BYTE*& panel, BYTE*& child) {
    BYTE *panels = nullptr, *children = nullptr;
    void* slot = nullptr;
    return handler != nullptr && read(current_menu, 0, menu) && read(menu, menu::kPanels, panels) &&
           read(panels, 0, panel) && read(panel, panel::kChildren, children) &&
           read(children, 0, child) &&
           ((read(child, kChildHandlers[0], slot) && slot == handler) ||
            (read(child, kChildHandlers[1], slot) && slot == handler));
}

// The panel holding the scale and the cursor's name: [[panel + 0x300]] + 8, as 0x16B843 to
// 0x16B85F and 0x169EF1 to 0x169F3B read it.
BYTE* info_panel(BYTE* panel) {
    BYTE *a = nullptr, *b = nullptr, *c = nullptr;
    if (!read(panel, kInfoPanel, a) || !read(a, 0, b) || !read(b, kInfoChild, c)) return nullptr;
    return c;
}

}  // namespace

namespace {

// The cursor's cell on the open map whose first panel is `panel`, or -1.
int cursor_cell(BYTE* panel) {
    BYTE *children = nullptr, *cursor = nullptr;
    int16_t cx = 0, cy = 0;
    if (!read(panel, panel::kChildren, children) || !read(children, kCursorChild, cursor) ||
        !read(cursor, kCursorX, cx) || !read(cursor, kCursorY, cy)) {
        return -1;
    }
    // [[[[[panel + 0x300]] + 8] + 0x1F0]] + 0x158 (read live 40.0 zoomed in, 2026-10-25).
    BYTE* info = info_panel(panel);
    BYTE *d = nullptr, *e = nullptr;
    double scale = 0;
    const bool fine = read(info, widget::kLabel, d) && read(d, 0, e) && read(e, kScale, scale) &&
                      scale == kFineScale;
    const int row = fine ? (cx + 5) / 10 : (cx + 11) / 23;   // the cell's y
    const int column = fine ? (cy + 4) / 8 : (cy + 8) / 17;  // the cell's x
    if (row < 0 || row >= kMapSide || column < 0 || column >= kMapSide) return -1;
    return row * kMapSide + column;
}

}  // namespace

MapCursor map_cursor(BYTE* current_menu, void* map_handler) {
    MapCursor out;
    BYTE *menu = nullptr, *panel = nullptr, *first = nullptr;
    if (!map_menu(current_menu, map_handler, menu, panel, first)) return out;
    out.open = true;
    out.cell = cursor_cell(panel);
    return out;
}

std::string map_name(const std::string& label) {
    std::string flat;
    for (unsigned char ch : label) {
        const bool font = ch >= 0x20 && ch < 0x60;
        const char put = font ? static_cast<char>(ch) : ' ';
        if (put == ' ' && (flat.empty() || flat.back() == ' ')) continue;
        flat += put;
    }
    while (!flat.empty() && flat.back() == ' ') flat.pop_back();
    if (flat.size() <= kMapNameMax) return flat;
    size_t at = flat.find(" FOR ");
    std::string part = at == std::string::npos ? flat : flat.substr(at + 1);
    if (part.size() > kMapNameMax) part.resize(kMapNameMax);
    return part;
}

bool cursor_shows_stand_in(const int32_t (&frames)[kMapNameMax]) {
    std::string text;
    for (int32_t f : frames) text += f > 0 && f < 0x80 ? static_cast<char>(f) : ' ';
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text == kStandInNames[0] || text == kStandInNames[1];
}

void MapCursorNames::watch(const MapAddresses& a, bool on) {
    void* handler = nullptr;
    if (a.map_handler == nullptr || !safe_read(a.map_handler, &handler, sizeof(handler))) return;
    if (child_ != nullptr && !still_wrapped(a)) {
        // The map closed (its menu and child went with it), another menu took its address, or
        // the game put a handler of its own in both slots (the navigation room's reveal, then
        // the map's handler again, 0x16DA80 and 0x16D550): wrapped again below if it is the map.
        child_ = nullptr;
        menu_ = nullptr;
    }
    if (on && child_ == nullptr) {
        BYTE *menu = nullptr, *panel = nullptr, *child = nullptr;
        constexpr size_t kBothSlots = kChildHandlers[1] - kChildHandlers[0] + sizeof(void*);
        if (map_menu(a.current_menu, handler, menu, panel, child) &&
            writable(child + kChildHandlers[0], kBothSlots)) {
            for (ptrdiff_t at : kChildHandlers) {
                void* slot = nullptr;
                if (read(child, at, slot) && slot == handler) {
                    std::memcpy(child + at, &a.hook, sizeof(void*));
                }
            }
            child_ = child;
            menu_ = menu;
            original_ = handler;
        }
    } else if (!on && child_ != nullptr) {
        restore(a);
    }
}

bool MapCursorNames::still_wrapped(const MapAddresses& a) const {
    if (!is_open(a.current_menu, menu_)) return false;
    BYTE *panels = nullptr, *panel = nullptr, *children = nullptr, *first = nullptr;
    if (!read(menu_, menu::kPanels, panels) || !read(panels, 0, panel) ||
        !read(panel, panel::kChildren, children) || !read(children, 0, first) || first != child_) {
        return false;
    }
    for (ptrdiff_t at : kChildHandlers) {
        void* slot = nullptr;
        if (read(child_, at, slot) && slot == a.hook) return true;
    }
    return false;
}

void MapCursorNames::after(const MapAddresses& a, const std::map<int, std::string>& labels) {
    if (child_ == nullptr || a.text_set_fn == nullptr) return;
    BYTE *menu = nullptr, *panel = nullptr, *child = nullptr;
    if (!map_menu(a.current_menu, a.hook, menu, panel, child)) return;
    auto it = labels.find(cursor_cell(panel));
    if (it == labels.end()) return;
    BYTE* info = info_panel(panel);
    BYTE *children = nullptr, *widget = nullptr, *text = nullptr;
    BYTE** glyphs = nullptr;
    int32_t length = 0;
    if (!read(info, panel::kChildren, children) || !read(children, kNameChild, widget) ||
        !read(widget, widget::kLabel, text) || !read(text, label::kLength, length) ||
        length < static_cast<int32_t>(kMapNameMax) || !read(text, label::kGlyphs, glyphs)) {
        return;
    }
    int32_t frames[kMapNameMax] = {};
    for (size_t i = 0; i < kMapNameMax; ++i) {
        BYTE* g = nullptr;
        if (!read(reinterpret_cast<BYTE*>(glyphs), static_cast<ptrdiff_t>(i * sizeof(BYTE*)), g) ||
            !read(g, sprite::kFrameIndex, frames[i])) {
            return;
        }
    }
    // Another text (the player's room, a marker, unknown) is left alone.
    if (!cursor_shows_stand_in(frames)) return;
    const std::string name = map_name(it->second);
    using Set = void (*)(void*, const char*, int, int);
    reinterpret_cast<Set>(a.text_set_fn)(text, name.c_str(), 0, static_cast<int>(kMapNameMax));
}

void MapCursorNames::restore(const MapAddresses& a) {
    if (child_ == nullptr) return;
    if (still_wrapped(a)) {
        for (ptrdiff_t at : kChildHandlers) {
            void* slot = nullptr;
            if (!read(child_, at, slot) || slot != a.hook) continue;
            if (!writable(child_ + at, sizeof(void*))) {
                return;  // could not put it back: kept, so the unload refuses
            }
            std::memcpy(child_ + at, &original_, sizeof(void*));
        }
    }
    child_ = nullptr;
    menu_ = nullptr;
}

}  // namespace zap
