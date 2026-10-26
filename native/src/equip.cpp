#include "equip.h"

#include <cstring>

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
// panel (dy), its mouse rectangle and its control hints (docs/re/memory.md, "The equipment
// screen"; B1).
constexpr ptrdiff_t kPanelIndex = 0x2E4, kPanelColumns = 0x2C6;
constexpr ptrdiff_t kChildRecord = 0x1F8, kChildDy = 0x1E4;
constexpr ptrdiff_t kChildRect = 0x1E8, kChildHints = 0x1D0;
// The color an item takes while selected, when its record's palette is not one of its own (1, 2,
// 14 to 19): built from this code (0xB465A to 0xB4D85, 0x1EDC00). The builder gives each such
// item one (0x12C91E on); the Data Disc's record palette is 5, so the disk needs one, or it turns
// black when selected. Navcore's, for a computer part (a first pick, tuned by eye).
constexpr ptrdiff_t kChildColor = 0x230;
constexpr int32_t kDiskColor = 0x6E;
constexpr int kMaxEntries = 16;
// The equipment screen (docs/re/memory.md, "The equipment screen"; B1, read live and from
// disassembly): panel 3 the Energy Tank, panel 6 the description box (child 0 the description,
// 0x88 glyphs; child 1 the name after the icon glyph, 0x11 glyphs).
constexpr int32_t kEquipmentPanels = 7, kEnergyPanel = 3, kTextPanel = 6, kEnergyGlyphs = 3;
constexpr int kDescriptionGlyphs = 0x88, kNameGlyphs = 0x11;
constexpr double kMove = 16.0;       // the Energy Tank's move left
constexpr int16_t kDiskDx = 32;      // the disk 32 right of the moved Energy Tank (dx 0)
constexpr int16_t kTwoColumns = 2;   // as the missile panel with both icons
constexpr size_t kWidgetSize = 0x2D0;
constexpr intptr_t kHovered = 0, kSelected = 3;

BYTE* panel_at(BYTE* menu, int index) {
    BYTE** panels = nullptr;
    BYTE* panel = nullptr;
    if (!read(menu, 0, panels) || !read(reinterpret_cast<BYTE*>(panels), index * 8, panel)) {
        return nullptr;
    }
    return panel;
}

}  // namespace

std::string disk_count_text(int count) {
    std::string digits = std::to_string(count);
    if (digits.size() < 2) digits.insert(0, 2 - digits.size(), '0');
    return " " + digits;
}

BYTE* EquipmentHook::entry_in(BYTE* menu, const EquipAddresses& a, void* callback) {
    BYTE** panels = nullptr;
    BYTE* panel = nullptr;
    int32_t count = 0, capacity = 0;
    BYTE** children = nullptr;
    if (!read(menu, 0, panels) || !read(menu, menu::kPanelCount, count) || count < 1 ||
        !read(reinterpret_cast<BYTE*>(panels), 0, panel) ||
        !read(panel, panel::kCapacity, capacity) ||
        capacity < 1 || capacity > kMaxEntries || !read(panel, panel::kChildren, children)) {
        return nullptr;
    }
    BYTE* list[kMaxEntries] = {};
    if (!safe_read(children, list, capacity * sizeof(BYTE*))) return nullptr;
    for (int i = 0; i < capacity; ++i) {
        int32_t released = 1;
        void* cb = nullptr;
        void* handler = nullptr;
        if (read(list[i], sprite::kReleased, released) && released == 0 &&
            read(list[i], widget::kCallback, cb) && cb == callback &&
            read(list[i], widget::kHandler, handler) && handler == a.widget_handler) {
            return list[i];
        }
    }
    return nullptr;
}

bool EquipmentHook::install(const EquipAddresses& a) {
    if (a.hook == nullptr) return false;
    BYTE* top = nullptr;
    if (!read(a.current_menu, 0, top)) top = nullptr;
    if (child_ != nullptr) {
        if (top == menu_ && entry_in(top, a, a.hook) == child_) return false;  // still hooked
        restore(a);
    }
    if (top == nullptr) return false;
    BYTE* child = entry_in(top, a, a.equipment_fn);
    if (child == nullptr || !writable(child + widget::kCallback, sizeof(void*))) return false;
    put(child, widget::kCallback, a.hook);
    menu_ = top;
    child_ = child;
    return true;
}

bool EquipmentHook::restore(const EquipAddresses& a) {
    if (child_ == nullptr) return false;
    bool put_back = is_open(a.current_menu, menu_) && entry_in(menu_, a, a.hook) == child_;
    if (put_back) put(child_, widget::kCallback, a.equipment_fn);
    menu_ = nullptr;
    child_ = nullptr;
    return put_back;
}

std::string EquipmentScreen::add(const EquipAddresses& a, int count) {
    if (count < 1) return "no disk yet";
    if (count > 99) return "the count is too long";
    restore(a);  // an earlier screen's, if still open (never, as the game builds one at a time)
    // The equipment screen on top, its Energy Tank panel as the builder leaves it.
    BYTE* menu = nullptr;
    BYTE* panel = nullptr;
    int32_t panel_count = 0, index = -1, capacity = 0, selectable = 0, released = 1, glyphs = 0;
    void* handler = nullptr;
    void* child_handler = nullptr;
    BYTE** children = nullptr;
    BYTE* energy = nullptr;
    BYTE* record = nullptr;
    BYTE* records = nullptr;
    BYTE* label = nullptr;
    if (!read(a.current_menu, 0, menu) || !read(menu, menu::kPanelCount, panel_count) ||
        panel_count != kEquipmentPanels || (panel = panel_at(menu, kEnergyPanel)) == nullptr ||
        panel_at(menu, kTextPanel) == nullptr || !read(panel, kPanelIndex, index) ||
        index != kEnergyPanel || !read(panel, panel::kHandler, handler) ||
        handler != a.equip_panel_fn || !read(panel, panel::kCapacity, capacity) || capacity != 1 ||
        !read(panel, panel::kSelectable, selectable) || selectable != 1 ||
        !read(panel, panel::kChildren, children) ||
        !read(reinterpret_cast<BYTE*>(children), 0, energy) ||
        !read(energy, sprite::kReleased, released) ||
        released != 0 || !read(energy, widget::kHandler, child_handler) ||
        child_handler != a.widget_handler || !read(energy, kChildRecord, record) ||
        !read(a.item_records, 0, records) || records == nullptr || record != records ||
        !read(energy, widget::kLabel, label) || !read(label, label::kLength, glyphs) ||
        glyphs != kEnergyGlyphs || !writable(panel, 0x300)) {
        return "not the equipment screen as traced";
    }
    // What the disk's widget is made from and given.
    BYTE* templates = nullptr;
    BYTE* item_template = nullptr;
    BYTE* palette = nullptr;
    int16_t palette_number = 0, disk_frame = -1, dy = 0;
    uint64_t rect = 0;
    using Malloc = void* (*)(size_t);
    using Free = void (*)(void*);
    Malloc game_malloc = nullptr;
    Free game_free = nullptr;
    double ex = 0, ey = 0, lx = 0, ly = 0;
    if (!read(a.sprite_templates, 0, templates) ||
        !read(templates, sprite::kItemTemplate, item_template) ||
        item_template == nullptr || !read(energy, sprite::kPalette, palette) ||
        palette == nullptr ||
        !read(energy, sprite::kPaletteNumber, palette_number) ||
        !read(records, kDiskItem * record::kSize + record::kFrame, disk_frame) || disk_frame < 0 ||
        !read(a.malloc_slot, 0, game_malloc) || game_malloc == nullptr ||
        !read(a.free_slot, 0, game_free) || game_free == nullptr || !read(energy, sprite::kX, ex) ||
        !read(energy, sprite::kY, ey) || !read(label, sprite::kX, lx) ||
        !read(label, sprite::kY, ly) ||
        !read(energy, kChildDy, dy) || !read(energy, kChildRect, rect)) {
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
        text.c_str(), static_cast<int>(text.size()), panel, a.widget_handler, nullptr,
        records + kDiskItem * record::kSize, 0, 1, &rect);
    BYTE* disk_label = nullptr;
    if (disk == nullptr || !writable(disk, kWidgetSize) ||
        !read(disk, widget::kLabel, disk_label) ||
        disk_label == nullptr) {
        game_free(array);
        return "the disk's widget could not be made";
    }
    put<int32_t>(disk, kChildHints, -1);
    put<int32_t>(disk, kChildColor, kDiskColor);
    reinterpret_cast<void (*)(void*, void*)>(a.sprite_sheet_fn)(disk, item_template);
    int32_t frames = 0;
    BYTE* base = nullptr;
    if (read(disk, sprite::kFrameCount, frames) && disk_frame < frames &&
        read(disk, sprite::kFrameList, base) &&
        base != nullptr) {
        put<int32_t>(disk, sprite::kFrameIndex, disk_frame);
        put(disk, sprite::kFrameShown, base + disk_frame * sprite::kFrameSize);
    }
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
    if (a.panel_hook != nullptr) {
        put(panel, panel::kHandler, a.panel_hook);
        menu_ = menu;
        panel_ = panel;
        disk_ = disk;
    }
    return "";
}

void EquipmentScreen::on_event(const EquipAddresses& a, void* widget, intptr_t event) {
    if (disk_ == nullptr || widget != disk_ || (event != kHovered && event != kSelected)) return;
    BYTE* menu = nullptr;
    if (!read(a.current_menu, 0, menu) || menu != menu_) return;
    BYTE* box = panel_at(menu, kTextPanel);
    BYTE** children = nullptr;
    BYTE* description = nullptr;
    BYTE* name = nullptr;
    BYTE* description_label = nullptr;
    BYTE* name_label = nullptr;
    int32_t description_glyphs = 0, name_glyphs = 0;
    if (!read(box, panel::kChildren, children) ||
        !read(reinterpret_cast<BYTE*>(children), 0, description) ||
        !read(reinterpret_cast<BYTE*>(children), 8, name) ||
        !read(description, widget::kLabel, description_label) ||
        !read(description_label, label::kLength, description_glyphs) ||
        description_glyphs < kDescriptionGlyphs || !read(name, widget::kLabel, name_label) ||
        !read(name_label, label::kLength, name_glyphs) || name_glyphs < 1 + kNameGlyphs) {
        return;
    }
    using Set = void (*)(void*, const char*, int, int);
    auto set = reinterpret_cast<Set>(a.text_set_fn);
    set(description_label, kDiskDescription, 0, kDescriptionGlyphs);
    set(name_label, kDiskName, 1, kNameGlyphs);
    // Its icon before the name, as the game puts each record's own (the disk's record has none):
    // the glyph the helper paints (kDiskGlyph), set as the game's text code sets a character.
    BYTE** glyphs = nullptr;
    BYTE* icon = nullptr;
    int32_t frames = 0;
    BYTE* base = nullptr;
    if (read(name_label, label::kGlyphs, glyphs) &&
        read(reinterpret_cast<BYTE*>(glyphs), 0, icon) && read(icon, sprite::kFrameCount, frames) &&
        kDiskGlyph < frames && read(icon, sprite::kFrameList, base) && base != nullptr &&
        writable(icon, sprite::kVisible + 1)) {
        put<int32_t>(icon, sprite::kFrameIndex, kDiskGlyph);
        put(icon, sprite::kFrameShown, base + kDiskGlyph * sprite::kFrameSize);
        *(icon + sprite::kVisible) = 1;
    }
}

void EquipmentScreen::tidy(const EquipAddresses& a) {
    if (panel_ != nullptr && !is_open(a.current_menu, menu_)) {
        menu_ = nullptr;
        panel_ = nullptr;
        disk_ = nullptr;
    }
}

void EquipmentScreen::restore(const EquipAddresses& a) {
    if (panel_ == nullptr) return;
    void* handler = nullptr;
    if (is_open(a.current_menu, menu_) && panel_at(menu_, kEnergyPanel) == panel_ &&
        read(panel_, panel::kHandler, handler) && handler == a.panel_hook) {
        put(panel_, panel::kHandler, a.equip_panel_fn);
    }
    menu_ = nullptr;
    panel_ = nullptr;
    disk_ = nullptr;
}

}  // namespace zap
