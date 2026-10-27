#include "mapmarks.h"

#include <algorithm>
#include <cstring>

#include "apitem.h"  // kStandInNames, kStandInProgression
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
        if (section[kItemTable + i] != kStandInProgression) continue;
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
// The cursor widget, the panel's fourth child: its 16-bit +0x1E6 gives the cell's row (y) and
// +0x1E4 its column (x), as the map's handler computes them. The cursor's name: the info
// panel's fourth child.
constexpr int kCursorChild = 3, kNameChild = 3;
constexpr ptrdiff_t kCursorRow = 0x1E6, kCursorColumn = 0x1E4;
constexpr double kFineScale = 40.0;

// Whether either of `child`'s handler slots holds `handler` (never true for none).
bool holds(const BYTE* child, const void* handler) {
    return handler != nullptr && (pointer_at(child, kChildHandlers[0]) == handler ||
                                  pointer_at(child, kChildHandlers[1]) == handler);
}

// The map's menu, its first panel and that panel's first child, if the top menu is the map's
// (its handler `handler`).
bool map_menu(BYTE* current_menu, void* handler, BYTE*& menu, BYTE*& panel, BYTE*& child) {
    menu = pointer_at(current_menu);
    panel = panel_at(menu, 0);
    child = child_at(panel, 0);
    return holds(child, handler);
}

// The panel holding the scale and the cursor's name: [[[panel + 0x300]] + 8], as 0x16B843 to
// 0x16B85F and 0x169EF1 to 0x169F3B read it.
BYTE* info_panel(BYTE* panel) {
    return pointer_at(pointer_at(pointer_at(panel, kInfoPanel)), kInfoChild);
}

// The cursor's cell on the open map whose first panel is `panel`, or -1.
int cursor_cell(BYTE* panel) {
    BYTE* cursor = child_at(panel, kCursorChild);
    int16_t at_row = 0, at_column = 0;
    if (!read(cursor, kCursorRow, at_row) || !read(cursor, kCursorColumn, at_column)) return -1;
    // [[[[[panel + 0x300]] + 8] + 0x1F0]] + 0x158.
    BYTE* label = pointer_at(pointer_at(info_panel(panel), widget::kLabel));
    double scale = 0;
    const bool fine = read(label, kScale, scale) && scale == kFineScale;
    const int row = fine ? (at_row + 5) / 10 : (at_row + 11) / 23;
    const int column = fine ? (at_column + 4) / 8 : (at_column + 8) / 17;
    if (row < 0 || row >= kMapSide || column < 0 || column >= kMapSide) return -1;
    return row * kMapSide + column;
}

}  // namespace

std::string map_name(const std::string& label, int width) {
    const size_t first = width > 0 ? static_cast<size_t>(width) : label.size();
    std::string flat;
    for (unsigned char ch : label.substr(0, first)) {
        const bool font = ch >= 0x20 && ch < 0x60;
        const char put = font ? static_cast<char>(ch) : ' ';
        if (put == ' ' && (flat.empty() || flat.back() == ' ')) continue;
        flat += put;
    }
    while (!flat.empty() && flat.back() == ' ') flat.pop_back();
    if (flat.size() > kMapNameMax) flat.resize(kMapNameMax);
    return flat;
}

bool cursor_shows_stand_in(const int32_t (&frames)[kMapNameMax]) {
    std::string text;
    for (int32_t f : frames) text += f > 0 && f < 0x80 ? static_cast<char>(f) : ' ';
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text == kStandInNames[0] || text == kStandInNames[1];
}

void MapCursorNames::watch(const GameAddresses& a, bool on) {
    void* handler = nullptr;
    if (!safe_read(a.map_handler, &handler, sizeof(handler))) return;
    if (child_ != nullptr && !still_wrapped(a)) {
        // No longer the open map's child holding the wrapper (map closed, or the game rewrote
        // both slots, e.g. the navigation room's reveal): forget it; rewrapped below if it is
        // still the map.
        child_ = nullptr;
        menu_ = nullptr;
    }
    if (on && child_ == nullptr && a.hooks.map != nullptr) {
        BYTE *menu = nullptr, *panel = nullptr, *child = nullptr;
        constexpr size_t kBothSlots = kChildHandlers[1] - kChildHandlers[0] + sizeof(void*);
        if (map_menu(a.current_menu, handler, menu, panel, child) &&
            writable(child + kChildHandlers[0], kBothSlots)) {
            for (ptrdiff_t at : kChildHandlers) swap_pointer(child, at, handler, a.hooks.map);
            child_ = child;
            menu_ = menu;
            original_ = handler;
        }
    } else if (!on && child_ != nullptr) {
        restore(a);
    }
}

bool MapCursorNames::still_wrapped(const GameAddresses& a) const {
    return is_open(a.current_menu, menu_) && child_at(panel_at(menu_, 0), 0) == child_ &&
           holds(child_, a.hooks.map);
}

void MapCursorNames::after(const GameAddresses& a, const std::map<int, std::string>& labels,
                           const std::map<int, int>& widths) {
    if (child_ == nullptr || a.text_set_fn == nullptr) return;
    BYTE *menu = nullptr, *panel = nullptr, *child = nullptr;
    if (!map_menu(a.current_menu, a.hooks.map, menu, panel, child)) return;
    auto it = labels.find(cursor_cell(panel));
    if (it == labels.end()) return;
    BYTE* text = pointer_at(child_at(info_panel(panel), kNameChild), widget::kLabel);
    BYTE* glyphs = pointer_at(text, label::kGlyphs);
    int32_t length = 0;
    if (!read(text, label::kLength, length) || length < static_cast<int32_t>(kMapNameMax)) {
        return;
    }
    int32_t frames[kMapNameMax] = {};
    for (size_t i = 0; i < kMapNameMax; ++i) {
        BYTE* g = pointer_at(glyphs, static_cast<ptrdiff_t>(i * sizeof(BYTE*)));
        if (!read(g, sprite::kFrameIndex, frames[i])) return;
    }
    // Another text (the player's room, a marker, unknown) is left alone.
    if (!cursor_shows_stand_in(frames)) return;
    const auto width = widths.find(it->first);
    const std::string name = map_name(it->second, width == widths.end() ? 0 : width->second);
    using Set = void (*)(void*, const char*, int, int);
    reinterpret_cast<Set>(a.text_set_fn)(text, name.c_str(), 0, static_cast<int>(kMapNameMax));
}

void MapCursorNames::restore(const GameAddresses& a) {
    if (child_ == nullptr) return;
    if (still_wrapped(a)) {
        for (ptrdiff_t at : kChildHandlers) {
            if (pointer_at(child_, at) == a.hooks.map &&
                !swap_pointer(child_, at, a.hooks.map, original_)) {
                return;  // could not put it back: kept, so the unload refuses
            }
        }
    }
    child_ = nullptr;
    menu_ = nullptr;
}

}  // namespace zap
