#include "equip.h"

#include <cstring>

#include "files.h"  // file_disk_digits
#include "game_mem.h"

namespace zap {

const char kDiskName[] = "OS INSTALL DISK";
const char kDiskRows[8][9] = {
    "..###...", ".#...#..", "#.....#.", "#..#..#.",
    "#.....#.", ".#...#..", "..###...", "........",
};
// As the game's own descriptions: a blank first, lines of 34 characters run together.
const char kDiskDescription[] =
    " THESE FEW DISKS HOLD AN OLD BUT  "
    "SECURE OS INSTALL THAT MY PLANET  "
    "NEEDS TO RUN ITS DEFENSE NETWORK.";

namespace {

// A panel's own place in its menu's list and its columns; a widget's record, its offset in its
// panel (dy) and its mouse rectangle (docs/re/memory.md, "The equipment screen"; B1).
constexpr ptrdiff_t kPanelIndex = 0x2E4, kPanelColumns = 0x2C6;
constexpr ptrdiff_t kChildRecord = 0x1F8, kChildDy = 0x1E4;
constexpr ptrdiff_t kChildRect = 0x1E8;
// The color code a selected item takes when its record's palette is not 1, 2 or 14 to 19
// (0xB465A to 0xB4D85; the builder gives each such item one, 0x12C91E on). The Data Disc's
// palette is 5, so without one the disk turns black when selected. 0x6E is Navcore's, chosen by
// eye.
constexpr ptrdiff_t kChildColor = 0x230;
constexpr int32_t kDiskColor = 0x6E;
constexpr int kMaxEntries = 16;
// The equipment screen (docs/re/memory.md, "The equipment screen"): panel 3 the Energy Tank,
// panel 6 the description box (child 0 the description, child 1 the icon and name).
constexpr int32_t kEquipmentPanels = 7, kEnergyPanel = 3, kTextPanel = 6, kEnergyGlyphs = 3;
constexpr int kDescriptionGlyphs = 0x88, kNameGlyphs = 0x11;
constexpr double kMove = 16.0;       // the Energy Tank's move left
constexpr int16_t kDiskDx = 32;      // the disk 32 right of the moved Energy Tank (dx 0)
constexpr int16_t kTwoColumns = 2;   // as the missile panel with both icons
constexpr size_t kWidgetSize = 0x2D0;
constexpr intptr_t kHovered = 0, kSelected = 3;

}  // namespace

std::string disk_count_text(int count) { return " " + file_disk_digits(count); }

BYTE* EquipmentHook::entry_in(BYTE* menu, const GameAddresses& a, void* callback) {
    BYTE* list[kMaxEntries] = {};
    const int32_t count = children_of(panel_at(menu, 0), list, kMaxEntries);
    if (panel_count(menu) < 1 || count < 1 || count > kMaxEntries) return nullptr;
    for (int i = 0; i < count; ++i) {
        if (live_widget(list[i], a.widget_handler_fn, callback)) return list[i];
    }
    return nullptr;
}

bool EquipmentHook::install(const GameAddresses& a) {
    if (a.hooks.equipment == nullptr) return false;
    BYTE* top = pointer_at(a.current_menu);
    if (child_ != nullptr && top == menu_ && entry_in(top, a, a.hooks.equipment) == child_) {
        return false;  // still hooked
    }
    restore(a);
    if (top == nullptr) return false;
    BYTE* child = entry_in(top, a, a.equipment_fn);
    if (!swap_pointer(child, widget::kCallback, a.equipment_fn, a.hooks.equipment)) return false;
    menu_ = top;
    child_ = child;
    return true;
}

bool EquipmentHook::restore(const GameAddresses& a) {
    if (child_ == nullptr) return false;
    bool put_back =
        is_open(a.current_menu, menu_) && entry_in(menu_, a, a.hooks.equipment) == child_;
    if (put_back) put(child_, widget::kCallback, a.equipment_fn);
    menu_ = nullptr;
    child_ = nullptr;
    return put_back;
}

std::string EquipmentScreen::add(const GameAddresses& a, int count) {
    if (count < 1) return "no disk yet";
    if (count > 99) return "the count is too long";
    restore(a);  // an earlier screen's, if still open
    if (panel_ != nullptr) return "an earlier screen's handler could not be put back";
    // The equipment screen on top, its Energy Tank panel as the builder leaves it.
    BYTE* menu = pointer_at(a.current_menu);
    BYTE* panel = panel_at(menu, kEnergyPanel);
    BYTE* energy = child_at(panel, 0);
    int32_t index = -1, selectable = 0, glyphs = 0;
    BYTE** children = nullptr;
    BYTE* records = pointer_at(a.item_records);
    BYTE* label = pointer_at(energy, widget::kLabel);
    if (panel_count(menu) != kEquipmentPanels || panel == nullptr ||
        panel_at(menu, kTextPanel) == nullptr || !read(panel, kPanelIndex, index) ||
        index != kEnergyPanel || pointer_at(panel, panel::kHandler) != a.equip_panel_fn ||
        child_count(panel) != 1 || !read(panel, panel::kSelectable, selectable) ||
        selectable != 1 || !read(panel, panel::kChildren, children) ||
        !live_widget(energy, a.widget_handler_fn) || records == nullptr ||
        pointer_at(energy, kChildRecord) != records || !read(label, label::kLength, glyphs) ||
        glyphs != kEnergyGlyphs || !writable(panel, 0x300)) {
        return "not the equipment screen as traced";
    }
    // What the disk's widget is made from and given.
    BYTE* item_template = pointer_at(pointer_at(a.sprite_templates), sprite::kItemTemplate);
    BYTE* palette = pointer_at(energy, sprite::kPalette);
    int16_t palette_number = 0, disk_frame = -1, dy = 0;
    uint64_t rect = 0;
    auto game_malloc = reinterpret_cast<void* (*)(size_t)>(pointer_at(a.malloc_slot));
    auto game_free = reinterpret_cast<void (*)(void*)>(pointer_at(a.free_slot));
    double ex = 0, ey = 0, lx = 0, ly = 0;
    if (item_template == nullptr || palette == nullptr ||
        !read(energy, sprite::kPaletteNumber, palette_number) ||
        !read(records, kDiskItem * record::kSize + record::kFrame, disk_frame) || disk_frame < 0 ||
        game_malloc == nullptr || game_free == nullptr || !read(energy, sprite::kX, ex) ||
        !read(energy, sprite::kY, ey) || !read(label, sprite::kX, lx) ||
        !read(label, sprite::kY, ly) || !read(energy, kChildDy, dy) ||
        !read(energy, kChildRect, rect)) {
        return "the item sprites, the Energy Tank's look or the allocator are missing";
    }
    // The panel's new two-entry array, from the game's heap (the game frees it with the menu).
    std::string text = disk_count_text(count);
    auto* array = static_cast<BYTE**>(game_malloc(2 * sizeof(BYTE*)));
    if (array == nullptr) return "no memory for the disk";

    // The disk: a widget as the Energy Tank's, index 1, the Data Disc's record and frame.
    using New = BYTE* (*)(const char*, int, BYTE*, void*, void*, BYTE*, intptr_t, int, void*);
    // The constructor copies the label's characters (0x925D8): the helper's own string will do.
    BYTE* disk = reinterpret_cast<New>(a.widget_new_fn)(
        text.c_str(), static_cast<int>(text.size()), panel, a.widget_handler_fn, nullptr,
        records + kDiskItem * record::kSize, 0, 1, &rect);
    BYTE* disk_label = pointer_at(disk, widget::kLabel);
    if (disk == nullptr || !writable(disk, kWidgetSize) || disk_label == nullptr) {
        game_free(array);
        return "the disk's widget could not be made";
    }
    put<int32_t>(disk, sprite::kStyle, -1);  // as the Energy Tank's widget
    put<int32_t>(disk, kChildColor, kDiskColor);
    reinterpret_cast<void (*)(void*, void*)>(a.sprite_sheet_fn)(disk, item_template);
    set_frame(disk, disk_frame);
    put(disk, sprite::kPalette, palette);
    put(disk, sprite::kPaletteNumber, palette_number);
    *(disk + sprite::kVisible) = 1;

    // The Energy Tank 16 pixels left, its count with it, as the missile panel moves its icons
    // (once the disk is made, so a failure leaves it where the game put it).
    using Move = void (*)(void*, int, int, bool);
    using Place = void (*)(void*, int, int);
    reinterpret_cast<Move>(a.widget_move_fn)(energy, 0, dy, true);
    double nx = ex, ny = ey;
    read(energy, sprite::kX, nx);
    read(energy, sprite::kY, ny);
    reinterpret_cast<Place>(a.text_place_fn)(label, static_cast<int>(lx - kMove),
                                             static_cast<int>(ly));
    if (nx != ex - kMove || ny != ey) {
        game_free(array);  // never handed to the panel
        return "the Energy Tank did not move as traced";
    }

    // Two children in the panel, in a row; the game frees the array and both with the menu.
    BYTE** old = children;
    array[0] = energy;
    array[1] = disk;
    put(panel, panel::kChildren, array);
    // `old` came from the game's malloc (docs/re/memory.md, "The equipment screen"), so the
    // game's free releases it.
    game_free(old);
    put<int32_t>(panel, panel::kCapacity, 2);
    put<int32_t>(panel, panel::kSelectable, 2);
    put<int16_t>(panel, kPanelColumns, kTwoColumns);
    reinterpret_cast<Move>(a.widget_move_fn)(disk, kDiskDx, dy, true);
    double dx = 0, dyy = 0;
    read(disk, sprite::kX, dx);
    read(disk, sprite::kY, dyy);
    reinterpret_cast<Place>(a.text_place_fn)(disk_label, static_cast<int>(dx - kMove),
                                             static_cast<int>(dyy + kMove));
    reinterpret_cast<void (*)(void*)>(a.panel_link_fn)(panel);

    // The panel's handler the helper's, for the disk's text.
    if (a.hooks.equipment_panel != nullptr) {
        put(panel, panel::kHandler, a.hooks.equipment_panel);
        menu_ = menu;
        panel_ = panel;
        disk_ = disk;
    }
    return "";
}

void EquipmentScreen::on_event(const GameAddresses& a, void* widget, intptr_t event) {
    if (disk_ == nullptr || widget != disk_ || (event != kHovered && event != kSelected)) return;
    if (pointer_at(a.current_menu) != menu_) return;  // menu_: set with disk_
    BYTE* box = panel_at(menu_, kTextPanel);
    BYTE* description = child_at(box, 0);
    BYTE* name = child_at(box, 1);
    BYTE* description_label = pointer_at(description, widget::kLabel);
    BYTE* name_label = pointer_at(name, widget::kLabel);
    int32_t description_glyphs = 0, name_glyphs = 0;
    if (!read(description_label, label::kLength, description_glyphs) ||
        description_glyphs < kDescriptionGlyphs ||
        !read(name_label, label::kLength, name_glyphs) || name_glyphs < 1 + kNameGlyphs) {
        return;
    }
    using Set = void (*)(void*, const char*, int, int);
    auto set = reinterpret_cast<Set>(a.text_set_fn);
    set(description_label, kDiskDescription, 0, kDescriptionGlyphs);
    set(name_label, kDiskName, 1, kNameGlyphs);
    // Its icon before the name, as the game puts each record's own (the disk's record has none):
    // the glyph the helper paints (kDiskGlyph), set as the game's text code sets a character.
    BYTE* icon = pointer_at(pointer_at(name_label, label::kGlyphs));
    if (icon != nullptr && writable(icon, sprite::kVisible + 1) && set_frame(icon, kDiskGlyph)) {
        *(icon + sprite::kVisible) = 1;
    }
}

void EquipmentScreen::tidy(const GameAddresses& a) {
    if (!is_open(a.current_menu, menu_)) restore(a);  // closed: restore only forgets it
}

void EquipmentScreen::restore(const GameAddresses& a) {
    if (panel_ == nullptr) return;
    if (is_open(a.current_menu, menu_) && panel_at(menu_, kEnergyPanel) == panel_ &&
        pointer_at(panel_, panel::kHandler) == a.hooks.equipment_panel &&
        !swap_pointer(panel_, panel::kHandler, a.hooks.equipment_panel, a.equip_panel_fn)) {
        return;  // still the helper's: kept, so an unload refuses
    }
    menu_ = nullptr;
    panel_ = nullptr;
    disk_ = nullptr;
}

}  // namespace zap
