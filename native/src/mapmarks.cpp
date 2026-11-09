#include "mapmarks.h"

#include <algorithm>
#include <cstring>

#include "apitem.h"  // kStandInNames, kStandInProgression
#include "game_mem.h"
#include "translation.h"

namespace zap {

namespace {

constexpr int kObscureMajors = 29, kObscureMinors = 38, kObscureClass = 41;
constexpr uint8_t kClassLimit = 0x24;  // values below hold an item's class in value % 12
constexpr uint8_t kRevealed = 0xC;     // the base of a square a hint or map revealed
constexpr uint8_t kMinorShown = 1, kMajorShown = 2, kMinorObscured = 7, kMajorObscured = 9;

}  // namespace

Obscure obscure_settings(const BYTE* section) {
    if (section == nullptr) return {};
    const BYTE* t = section + rando::kSettings;
    return {t[kObscureMajors] != 0, t[kObscureMinors] != 0, t[kObscureClass] != 0};
}

uint8_t important_value(uint8_t value, Obscure obscure, bool ended) {
    if (value >= kClassLimit) return value;
    const auto base = static_cast<uint8_t>(value - value % 12);
    auto as_major = [base](bool obscured) {
        return static_cast<uint8_t>(base + (obscured ? kMajorObscured : kMajorShown));
    };
    switch (value % 12) {
        case kMinorShown: {
            // Shown as a minor: shown as a major too, except under Majors on with Minors and
            // Class off, where the game obscures a major a hint or map revealed (base 0xC) and
            // shows one seen directly (base 0x18); memory.md lists what this misses.
            const bool majors_only = obscure.majors && !obscure.minors && !obscure.by_class;
            return as_major(majors_only && !ended && base == kRevealed);
        }
        case kMinorObscured:
            // Obscured as a minor: as a major, obscured too if majors are, else shown.
            return as_major((obscure.majors || obscure.by_class) && !ended);
        default:
            return value;
    }
}

int mark_important(const BYTE* section, BYTE* grid, bool ended) {
    if (section == nullptr || grid == nullptr) return 0;
    const Obscure obscure = obscure_settings(section);
    int changed = 0;
    const int locations = rando::locations(section);
    for (int i = 0; i < locations; ++i) {
        if (section[rando::kItemTable + i] != kStandInProgression) continue;
        auto x = field<uint16_t>(section, 4 * i), y = field<uint16_t>(section, 4 * i + 2);
        if (x >= kMapSide || y >= kMapSide) continue;
        BYTE& cell = grid[y * kMapSide + x];
        uint8_t raised = important_value(cell, obscure, ended);
        if (raised != cell) {
            cell = raised;
            ++changed;
        }
    }
    return changed;
}

namespace {

// The game's write (B1, 0x19E5A5), dl the square's value, r9w the item's ID, the store
// `mov [rcx+r14+disp32], al` at kSeenStore (its disp32 the grid's, left as read):
//   cmp dl, 0x18; jl low; sub r9w, 3; cmp r9w, 0x20; setbe al; add al, 0x19; jmp store
//   low: cmp dl, 0xC; jl end; sub r9w, 3; cmp r9w, 0x20; setbe al; add al, 0xD
//   store: mov [rcx+r14+disp32], al
//   end:
constexpr size_t kSeenStore = 42, kSeenStoreOpcode = 4;
constexpr BYTE kSeenGame[kSeenStore + kSeenStoreOpcode] = {
    0x80, 0xFA, 0x18, 0x7C, 0x11, 0x66, 0x41, 0x83, 0xE9, 0x03, 0x66, 0x41,
    0x83, 0xF9, 0x20, 0x0F, 0x96, 0xC0, 0x04, 0x19, 0xEB, 0x14, 0x80, 0xFA,
    0x0C, 0x7C, 0x17, 0x66, 0x41, 0x83, 0xE9, 0x03, 0x66, 0x41, 0x83, 0xF9,
    0x20, 0x0F, 0x96, 0xC0, 0x04, 0x0D, 0x42, 0x88, 0x84, 0x31};
// The same, ID 48 a major too (48 - 3 = 0x2D), every branch ending where the game's does, r9w
// and dl left as the game's leaves them:
//   cmp dl, 0xC; jl end; mov al, 0x19; cmp dl, 0x18; jge id; mov al, 0xD
//   id: sub r9w, 3; cmp r9w, 0x20; jbe major; cmp r9w, 0x2D; jne store
//   major: inc al
//   store: mov [rcx+r14+disp32], al; nop (7 bytes)
//   end:
constexpr size_t kSeenOurStore = 35;
constexpr BYTE kSeenOurs[kSeenOurStore] = {
    0x80, 0xFA, 0x0C, 0x7C, 0x2D, 0xB0, 0x19, 0x80, 0xFA, 0x18, 0x7D, 0x02,
    0xB0, 0x0D, 0x66, 0x41, 0x83, 0xE9, 0x03, 0x66, 0x41, 0x83, 0xF9, 0x20,
    0x76, 0x07, 0x66, 0x41, 0x83, 0xF9, 0x2D, 0x75, 0x02, 0xFE, 0xC0};
constexpr size_t kSeenStoreSize = 8;
constexpr BYTE kNop7[] = {0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00};
static_assert(kSeenOurStore + kSeenStoreSize + sizeof(kNop7) == kSeenMarkerSize, "as laid out");
static_assert(kSeenStore + kSeenStoreSize == kSeenMarkerSize, "the game's, as laid out");

}  // namespace

bool seen_marker_patch(const BYTE (&game)[kSeenMarkerSize], BYTE (&out)[kSeenMarkerSize]) {
    if (std::memcmp(game, kSeenGame, sizeof(kSeenGame)) != 0) return false;
    std::memcpy(out, kSeenOurs, sizeof(kSeenOurs));
    std::memcpy(out + kSeenOurStore, game + kSeenStore, kSeenStoreSize);
    std::memcpy(out + kSeenOurStore + kSeenStoreSize, kNop7, sizeof(kNop7));
    return true;
}

namespace {

// The map child's two handler slots, both given the map's handler by its builder
// (docs/re/memory.md, "Map markers").
constexpr ptrdiff_t kChildHandlers[2] = {0x168, 0x178};
constexpr ptrdiff_t kInfoPanel = 0x300, kInfoChild = 0x8;
constexpr ptrdiff_t kScale = 0x158;  // on the info child's label (widget::kLabel)
// The cursor widget is the panel's fourth child (16-bit +0x1E6 the cell's row, +0x1E4 its
// column); the cursor's name is the info panel's fourth child.
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

// The panel holding the scale and the cursor's name: [[[panel + 0x300]] + 8]
// (docs/re/memory.md, "Map markers").
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

std::string map_name(const Label& label) {
    std::string name = in_capitals(label.first_text());
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.size() > kMapNameMax) name.resize(kMapNameMax);
    return name;
}

bool cursor_shows_stand_in(const int32_t (&frames)[kMapNameMax]) {
    std::string text;
    for (int32_t f : frames) text += f > 0 && f < 0x80 ? static_cast<char>(f) : ' ';
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text == kStandInNames[0] || text == kStandInNames[1];
}

bool MapCursorNames::open(const GameAddresses& a) {
    BYTE *menu = nullptr, *panel = nullptr, *child = nullptr;
    return map_menu(a.current_menu, pointer_at(a.map_handler), menu, panel, child) ||
           map_menu(a.current_menu, a.hooks.map, menu, panel, child);
}

void MapCursorNames::watch(const GameAddresses& a, bool on) {
    void* handler = nullptr;
    if (!safe_read(a.map_handler, &handler, sizeof(handler))) return;
    if (child_ != nullptr && !still_wrapped(a)) {
        // Map closed, or the game rewrote both slots (the navigation room's reveal): forget it;
        // rewrapped below if it is still the map.
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

void MapCursorNames::after(const GameAddresses& a, const std::map<int, Label>& labels,
                           const std::function<std::wstring(const Label&)>& hd_name) {
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
    const std::string name = map_name(it->second);
    using Set = void (*)(void*, const char*, int, int);
    reinterpret_cast<Set>(a.text_set_fn)(text, name.c_str(), 0, static_cast<int>(kMapNameMax));
    // In a translation the cursor shows the text the game just built from ItemNames 47 or 48,
    // built again as the label's first line, without its icon (translation.h).
    rebuild_label(a, text, hd_name(it->second));
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
