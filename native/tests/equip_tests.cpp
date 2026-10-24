// Host tests for the OS Install Disk count on the equipment screen (equip.h): a stand-in pause
// menu and equipment screen laid out as read live on B1, and stand-ins for the game's functions
// the helper calls (moving a widget, placing and setting a text, making a widget, giving it a
// sprite sheet, linking a panel, malloc and free).

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "equip.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

template <typename T>
T& at(BYTE* p, ptrdiff_t offset) {
    return *reinterpret_cast<T*>(p + offset);
}

int g_calls = 0;
// Bodies of their own: the linker folds identical functions into one address.
void widget_handler() { g_calls += 1; }
void equipment_fn(void*, int) { g_calls += 2; }
void equip_panel_fn() { g_calls += 3; }
void hook(void*, int) { g_calls += 4; }
void other_fn(void*, int) { g_calls += 5; }
void panel_hook() { g_calls += 6; }

// The game's (widget, dx, dy, set): the offset kept if `set`, then the widget at its panel's
// position plus the offset, its label there too (0xAA8C0).
void widget_move(void* w, int dx, int dy, bool set) {
    BYTE* widget = static_cast<BYTE*>(w);
    BYTE* panel = at<BYTE*>(widget, 0x1D8);
    if (set) {
        at<int16_t>(widget, 0x1E6) = static_cast<int16_t>(dx);
        at<int16_t>(widget, 0x1E4) = static_cast<int16_t>(dy);
    }
    double x = at<int16_t>(panel, 0x1DA) + at<int16_t>(widget, 0x1E6);
    double y = at<int16_t>(panel, 0x1D8) + at<int16_t>(widget, 0x1E4);
    at<double>(widget, 0x40) = x;
    at<double>(widget, 0x48) = y;
    BYTE* label = at<BYTE*>(widget, 0x1F0);
    at<double>(label, 0x40) = x;
    at<double>(label, 0x48) = y;
}

// The game's (text, x, y): the label at (x, y) (its glyphs follow in the game).
void text_place(void* t, int x, int y) {
    BYTE* text = static_cast<BYTE*>(t);
    at<double>(text, 0x40) = x;
    at<double>(text, 0x48) = y;
}

std::map<BYTE*, std::string> g_label_text;  // what each made label was made from
std::vector<BYTE*> g_widgets;
bool g_fail_widget = false;
// The game's widget constructor (0xAA610): a widget and its label, unlinked.
BYTE* widget_new(const char* str, int glyphs, BYTE* panel, void* handler, void* callback,
                 BYTE* record, intptr_t, int index, void* rect) {
    if (g_fail_widget) return nullptr;
    BYTE* w = static_cast<BYTE*>(std::calloc(1, 0x2D0));
    BYTE* label = static_cast<BYTE*>(std::calloc(1, 0x268));
    at<int32_t>(label, 0x1D8) = glyphs;
    g_label_text[label] = std::string(str, glyphs);
    at<BYTE*>(w, 0x1F0) = label;
    at<BYTE*>(w, 0x1D8) = panel;
    at<void*>(w, 0x2C8) = handler;
    at<void*>(w, 0x2C0) = callback;
    at<BYTE*>(w, 0x1F8) = record;
    at<int32_t>(w, 0x1E0) = index;
    std::memcpy(w + 0x1E8, rect, 8);
    g_widgets.push_back(w);
    return w;
}

BYTE g_frames[256 * 0x38];
// The game's (object, template): the template's frames (0x74780).
void sprite_sheet(void* o, void* t) {
    at<int32_t>(static_cast<BYTE*>(o), 0x1BC) = at<int32_t>(static_cast<BYTE*>(t), 0x1BC);
    at<BYTE*>(static_cast<BYTE*>(o), 0x1A8) = at<BYTE*>(static_cast<BYTE*>(t), 0x1A8);
}

std::vector<BYTE*> g_linked;
void panel_link(void* panel) { g_linked.push_back(static_cast<BYTE*>(panel)); }

struct Set {
    std::string text;
    int start, count;
};
std::map<BYTE*, Set> g_set;
void text_set(void* text, const char* str, int start, int count) {
    g_set[static_cast<BYTE*>(text)] = {str, start, count};
}

std::vector<void*> g_freed;
void* game_malloc(size_t n) { return std::malloc(n); }
void game_free(void* p) {
    g_freed.push_back(p);
    std::free(p);
}

struct Screen {
    // The pause menu: panel 0's six entries, EQUIPMENT second.
    BYTE pause[0x80] = {};
    BYTE* pause_panels[2] = {};
    BYTE pause_panel[0x300] = {};
    BYTE* pause_children[6] = {};
    BYTE entries[6][0x2D0] = {};
    // The equipment screen: seven panels, panel 3 the Energy Tank, panel 6 the text box.
    BYTE equip[0x80] = {};
    BYTE* equip_panels[7] = {};
    BYTE panels[7][0x300] = {};
    BYTE** energy_children = nullptr;  // the game's malloc'd array, freed when replaced
    BYTE energy[0x2D0] = {};
    BYTE label[0x268] = {};
    BYTE* box_children[2] = {};
    BYTE box_entries[2][0x2D0] = {};
    BYTE box_labels[2][0x268] = {};
    BYTE* name_glyphs[1] = {};
    BYTE name_icon[0x1D8] = {};
    BYTE* current = nullptr;
    BYTE item_template[0x1D8] = {};
    BYTE* templates[40] = {};
    BYTE* templates_holder = nullptr;
    BYTE palette3[8] = {};
    BYTE records[51 * 100] = {};
    BYTE* records_holder = nullptr;
    void* malloc_slot = reinterpret_cast<void*>(&game_malloc);
    void* free_slot = reinterpret_cast<void*>(&game_free);

    Screen() {
        at<BYTE*>(pause, 0) = reinterpret_cast<BYTE*>(pause_panels);
        at<int32_t>(pause, 8) = 2;
        pause_panels[0] = pause_panel;
        at<BYTE**>(pause_panel, 0x1E0) = pause_children;
        at<int32_t>(pause_panel, 0x1E8) = 6;
        void* callbacks[6] = {reinterpret_cast<void*>(&other_fn),
                              reinterpret_cast<void*>(&equipment_fn),
                              reinterpret_cast<void*>(&other_fn),
                              reinterpret_cast<void*>(&other_fn),
                              reinterpret_cast<void*>(&other_fn),
                              nullptr};
        for (int i = 0; i < 6; ++i) {
            pause_children[i] = entries[i];
            at<void*>(entries[i], 0x2C0) = callbacks[i];
            at<void*>(entries[i], 0x2C8) = reinterpret_cast<void*>(&widget_handler);
        }

        at<BYTE*>(equip, 0) = reinterpret_cast<BYTE*>(equip_panels);
        at<int32_t>(equip, 8) = 7;
        at<BYTE*>(equip, 0x78) = pause;
        for (int i = 0; i < 7; ++i) {
            equip_panels[i] = panels[i];
            at<int32_t>(panels[i], 0x2E4) = i;
            at<void*>(panels[i], 0x2F8) = reinterpret_cast<void*>(&equip_panel_fn);
        }
        BYTE* p3 = panels[3];
        at<int16_t>(p3, 0x1DA) = 16;  // x
        at<int16_t>(p3, 0x1D8) = 16;  // y
        at<int32_t>(p3, 0x1E8) = 1;
        at<int32_t>(p3, 0x1EC) = 1;
        at<int32_t>(p3, 0x2C6) = 0x10001;  // one column, step 1
        energy_children = static_cast<BYTE**>(std::malloc(sizeof(BYTE*)));
        energy_children[0] = energy;
        at<BYTE**>(p3, 0x1E0) = energy_children;
        at<BYTE*>(energy, 0x1D8) = p3;
        at<int16_t>(energy, 0x1E6) = 16;
        at<int16_t>(energy, 0x1E4) = 16;
        const int16_t rect[4] = {-10, -10, 10, 10};
        std::memcpy(energy + 0x1E8, rect, sizeof(rect));
        at<double>(energy, 0x40) = 32;
        at<double>(energy, 0x48) = 32;
        at<void*>(energy, 0x2C8) = reinterpret_cast<void*>(&widget_handler);
        at<BYTE*>(energy, 0x1F8) = records;
        at<BYTE*>(energy, 0x198) = palette3;
        at<int16_t>(energy, 0x1A0) = 3;
        at<BYTE*>(energy, 0x1F0) = label;
        at<int32_t>(label, 0x1D8) = 3;
        at<double>(label, 0x40) = 16;
        at<double>(label, 0x48) = 48;
        // The text box: child 0 the description (0x88 glyphs), child 1 the name (0x12).
        BYTE* p6 = panels[6];
        at<BYTE**>(p6, 0x1E0) = box_children;
        at<int32_t>(p6, 0x1E8) = 2;
        for (int i = 0; i < 2; ++i) {
            box_children[i] = box_entries[i];
            at<BYTE*>(box_entries[i], 0x1F0) = box_labels[i];
        }
        at<int32_t>(box_labels[0], 0x1D8) = 0x88;
        at<int32_t>(box_labels[1], 0x1D8) = 0x12;
        name_glyphs[0] = name_icon;
        at<BYTE**>(box_labels[1], 0x228) = name_glyphs;
        at<int32_t>(name_icon, 0x1BC) = 256;
        at<BYTE*>(name_icon, 0x1A8) = g_frames;

        at<int32_t>(item_template, 0x1BC) = 152;
        at<BYTE*>(item_template, 0x1A8) = g_frames;
        templates[34] = item_template;
        templates_holder = reinterpret_cast<BYTE*>(templates);
        at<int16_t>(records, 44 * 100 + 2) = 136;
        records_holder = records;
        current = pause;
    }

    zap::EquipAddresses addresses() {
        zap::EquipAddresses a;
        a.current_menu = reinterpret_cast<BYTE*>(&current);
        a.widget_handler = reinterpret_cast<void*>(&widget_handler);
        a.equipment_fn = reinterpret_cast<void*>(&equipment_fn);
        a.equip_panel_fn = reinterpret_cast<void*>(&equip_panel_fn);
        a.widget_move_fn = reinterpret_cast<void*>(&widget_move);
        a.text_place_fn = reinterpret_cast<void*>(&text_place);
        a.widget_new_fn = reinterpret_cast<void*>(&widget_new);
        a.sprite_sheet_fn = reinterpret_cast<void*>(&sprite_sheet);
        a.panel_link_fn = reinterpret_cast<void*>(&panel_link);
        a.text_set_fn = reinterpret_cast<void*>(&text_set);
        a.malloc_slot = reinterpret_cast<BYTE*>(&malloc_slot);
        a.free_slot = reinterpret_cast<BYTE*>(&free_slot);
        a.sprite_templates = reinterpret_cast<BYTE*>(&templates_holder);
        a.item_records = reinterpret_cast<BYTE*>(&records_holder);
        a.hook = reinterpret_cast<void*>(&hook);
        a.panel_hook = reinterpret_cast<void*>(&panel_hook);
        return a;
    }
    void* equipment_callback() { return at<void*>(entries[1], 0x2C0); }
};

void test_the_hook_follows_the_pause_menu() {
    Screen s;
    zap::EquipmentHook h;
    zap::EquipAddresses a = s.addresses();
    check(h.install(a) && s.equipment_callback() == a.hook, "EQUIPMENT points at the hook");
    check(at<void*>(s.entries[0], 0x2C0) == reinterpret_cast<void*>(&other_fn),
          "and nothing else changes");
    check(!h.install(a) && h.installed(), "hooked once");
    s.current = s.equip;  // the equipment screen on top: the pause menu below, still open
    check(!h.install(a) && s.equipment_callback() == a.equipment_fn && !h.installed(),
          "the game's callback back once the pause menu is not on top");
    s.current = s.pause;
    check(h.install(a), "hooked again on top");
    check(h.restore(a) && s.equipment_callback() == a.equipment_fn, "restored");
    check(!h.restore(a), "restores once");

    // The pause menu closed (freed with its widgets, which still look intact): forgotten.
    h.install(a);
    BYTE title[0x80] = {};
    s.current = title;
    check(!h.restore(a) && s.equipment_callback() == a.hook && !h.installed(),
          "a closed menu is forgotten, not written");

    // A released widget or another handler: not taken.
    Screen r;
    at<int32_t>(r.entries[1], 0x1C8) = 2;
    zap::EquipmentHook h2;
    check(!h2.install(r.addresses()), "a released widget is left alone");
    Screen o;
    at<void*>(o.entries[1], 0x2C8) = reinterpret_cast<void*>(&equip_panel_fn);
    check(!h2.install(o.addresses()), "another handler is left alone");
}

void test_the_disk_is_a_second_widget_beside_the_energy_tank() {
    Screen s;
    zap::EquipAddresses a = s.addresses();
    s.current = s.equip;
    g_widgets.clear();
    g_freed.clear();
    g_linked.clear();
    zap::EquipmentScreen screen;
    BYTE** old = s.energy_children;
    std::string why = screen.add(a, 3);
    check(why.empty(), why.c_str());
    BYTE* p3 = s.panels[3];
    check(at<double>(s.energy, 0x40) == 16 && at<double>(s.energy, 0x48) == 32,
          "the Energy Tank 16 to the left");
    check(at<double>(s.label, 0x40) == 0 && at<double>(s.label, 0x48) == 48, "its count with it");
    check(g_widgets.size() == 1, "one widget made");
    BYTE* disk = g_widgets.empty() ? s.energy : g_widgets[0];
    BYTE** children = at<BYTE**>(p3, 0x1E0);
    check(children[0] == s.energy && children[1] == disk, "the Energy Tank, then the disk");
    check(at<int32_t>(p3, 0x1E8) == 2 && at<int32_t>(p3, 0x1EC) == 2, "both reachable");
    check(at<int16_t>(p3, 0x2C6) == 2 && at<int16_t>(p3, 0x2C8) == 1,
          "in a row, as the missile panel with both icons");
    check(g_freed.size() == 1 && g_freed[0] == old, "the panel's one-widget array freed");
    check(at<BYTE*>(disk, 0x1F8) == s.records + 44 * 100 && at<int32_t>(disk, 0x1E0) == 1 &&
              at<void*>(disk, 0x2C8) == a.widget_handler && at<void*>(disk, 0x2C0) == nullptr,
          "the Data Disc's record, index 1, the generic handler and no callback");
    check(at<int16_t>(disk, 0x1E8) == -10 && at<int16_t>(disk, 0x1EE) == 10,
          "the Energy Tank's mouse rectangle");
    BYTE* disk_label = at<BYTE*>(disk, 0x1F0);
    check(g_label_text[disk_label] == " 03", "its label the count, as the bomb count");
    check(at<double>(disk, 0x40) == 48 && at<double>(disk, 0x48) == 32, "the disk at (48, 32)");
    check(at<double>(disk_label, 0x40) == 32 && at<double>(disk_label, 0x48) == 48,
          "its count under it, on the Energy Tank count's row");
    check(at<int32_t>(disk, 0x1C4) == 136 && at<BYTE*>(disk, 0x1B0) == g_frames + 136 * 0x38,
          "the Data Disc's frame from the item sprites");
    check(at<BYTE*>(disk, 0x198) == s.palette3 && at<int16_t>(disk, 0x1A0) == 3 &&
              disk[0x120] == 1,
          "the Energy Tank's palette, shown");
    check(at<int32_t>(disk, 0x1D0) == -1, "no control hints, as the Energy Tank");
    check(at<int32_t>(disk, 0x230) == 0x6E, "a color for when it is selected");
    check(g_linked.size() == 1 && g_linked[0] == p3, "the panel linked again");
    check(at<void*>(p3, 0x2F8) == a.panel_hook && screen.holds_hook(),
          "the panel's handler the helper's");

    // Hovering the disk: its name and description; anything else is the game's.
    g_set.clear();
    screen.on_event(a, s.energy, 0);
    screen.on_event(a, disk, 2);
    check(g_set.empty(), "the Energy Tank, or the disk left: nothing written");
    screen.on_event(a, disk, 0);
    check(g_set[s.box_labels[0]].text == zap::kDiskDescription &&
              g_set[s.box_labels[0]].start == 0 && g_set[s.box_labels[0]].count == 0x88,
          "the description in the box");
    check(g_set[s.box_labels[1]].text == "OS INSTALL DISK" && g_set[s.box_labels[1]].start == 1,
          "the name after its icon glyph");
    check(at<int32_t>(s.name_icon, 0x1C4) == 0xFB &&
              at<BYTE*>(s.name_icon, 0x1B0) == g_frames + 0xFB * 0x38 && s.name_icon[0x120] == 1,
          "and the icon the helper paints in that glyph");
    g_set.clear();
    screen.on_event(a, disk, 3);
    check(g_set.size() == 2, "and when selected with the keys");

    screen.restore(a);
    check(at<void*>(p3, 0x2F8) == a.equip_panel_fn && !screen.holds_hook(), "handler back");
    check(screen.add(a, 3) != "", "added once: the panel has two widgets now");
}

void test_a_closed_screen_is_forgotten_unwritten() {
    Screen s;
    zap::EquipAddresses a = s.addresses();
    s.current = s.equip;
    zap::EquipmentScreen screen;
    check(screen.add(a, 1).empty(), "added");
    s.current = s.pause;  // the equipment screen closed: freed with its panels
    screen.tidy(a);
    check(!screen.holds_hook() && at<void*>(s.panels[3], 0x2F8) == a.panel_hook,
          "forgotten without a write");
}

void test_an_unexpected_screen_is_left_alone() {
    zap::EquipmentScreen screen;
    Screen none;
    none.current = none.equip;
    check(!screen.add(none.addresses(), 0).empty() && at<double>(none.energy, 0x40) == 32 &&
              at<int32_t>(none.panels[3], 0x1E8) == 1,
          "no disk yet: the screen as the game built it");

    Screen s;
    zap::EquipAddresses a = s.addresses();
    s.current = s.pause;  // not the equipment screen
    check(!screen.add(a, 1).empty() && at<double>(s.energy, 0x40) == 32,
          "another menu: nothing moved");
    s.current = s.equip;
    at<int32_t>(s.label, 0x1D8) = 4;
    check(!screen.add(a, 1).empty() && at<double>(s.energy, 0x40) == 32,
          "another count label: nothing moved");
    at<int32_t>(s.label, 0x1D8) = 3;
    at<void*>(s.panels[3], 0x2F8) = reinterpret_cast<void*>(&widget_handler);
    check(!screen.add(a, 1).empty() && at<double>(s.energy, 0x40) == 32,
          "another panel handler: nothing moved");

    Screen f;
    f.current = f.equip;
    g_fail_widget = true;
    check(!screen.add(f.addresses(), 1).empty() && at<int32_t>(f.panels[3], 0x1E8) == 1 &&
              !screen.holds_hook() && at<double>(f.energy, 0x40) == 32,
          "no widget made: the panel keeps its one, the Energy Tank where it was");
    g_fail_widget = false;
}

void test_the_count_text() {
    check(zap::disk_count_text(3) == " 03", " 03");
    check(zap::disk_count_text(41) == " 41", " 41");
    check(std::strlen(zap::kDiskDescription) <= 0x88, "the description fits the box");
}

}  // namespace

int main() {
    test_the_hook_follows_the_pause_menu();
    test_the_disk_is_a_second_widget_beside_the_energy_tank();
    test_a_closed_screen_is_forgotten_unwritten();
    test_an_unexpected_screen_is_left_alone();
    test_the_count_text();
    if (g_failures == 0) std::printf("equip tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
