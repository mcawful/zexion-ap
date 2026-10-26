// Host tests for the map's markers and names (mapmarks.h): a stand-in randomizer section and map
// grid, and a stand-in map menu with its cursor, scale and name text.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "mapmarks.h"

namespace {

struct Save {
    std::vector<BYTE> section = std::vector<BYTE>(0x908);
    std::vector<BYTE> grid = std::vector<BYTE>(54 * 54);
    // Location i at (x, y), holding `item`.
    void place(int i, int x, int y, BYTE item) {
        uint16_t xy[2] = {static_cast<uint16_t>(x), static_cast<uint16_t>(y)};
        std::memcpy(section.data() + 4 * i, xy, sizeof(xy));
        section[0x26C + i] = item;
    }
    BYTE& at(int x, int y) { return grid[y * 54 + x]; }
};

void test_the_values() {
    // Shown minors become shown majors, in each base (0, 0xC, 0x18).
    check(zap::important_value(0x01, false, false) == 0x02, "1 -> 2");
    check(zap::important_value(0x0D, false, false) == 0x0E, "0xD -> 0xE");
    check(zap::important_value(0x19, false, false) == 0x1A, "0x19 -> 0x1A");
    // Obscured minors: obscured majors when majors are obscured, else shown majors.
    check(zap::important_value(0x13, true, false) == 0x15, "0x13 -> 0x15, majors obscured");
    check(zap::important_value(0x13, false, false) == 0x0E, "0x13 -> 0xE, majors shown");
    check(zap::important_value(0x1F, true, true) == 0x1A, "finished game: shown");
    // Majors, collected items, unrevealed squares and values above the classes stay.
    for (BYTE v : std::vector<BYTE>{0x00, 0x02, 0x09, 0x0E, 0x15, 0x1A, 0x1B, 0x22, 0x24, 0x30}) {
        check(zap::important_value(v, true, false) == v, "left alone");
    }
}

void test_only_the_stand_ins_squares() {
    Save s;
    s.place(0, 3, 4, 48);   // the progression stand-in
    s.place(1, 5, 6, 47);   // another player's filler
    s.place(2, 7, 8, 0);    // an Energy Tank (minor in the game too)
    s.place(3, 9, 10, 48);  // a stand-in already collected
    s.at(3, 4) = 0x19;
    s.at(5, 6) = 0x19;
    s.at(7, 8) = 0x19;
    s.at(9, 10) = 0x1B;
    check(zap::mark_important(s.section.data(), s.grid.data(), false) == 1, "one square changed");
    check(s.at(3, 4) == 0x1A, "the stand-in's square important");
    check(s.at(5, 6) == 0x19 && s.at(7, 8) == 0x19, "others basic");
    check(s.at(9, 10) == 0x1B, "collected left alone");
    check(zap::mark_important(s.section.data(), s.grid.data(), false) == 0, "once");
}

void test_obscure_majors_from_the_settings() {
    Save s;
    s.place(0, 1, 1, 48);
    s.at(1, 1) = 0x13;  // obscured as a minor
    s.section[0x87B + 29] = 1;
    zap::mark_important(s.section.data(), s.grid.data(), false);
    check(s.at(1, 1) == 0x15, "obscured as a major");
}

void test_a_cell_off_the_map_is_skipped() {
    Save s;
    s.place(0, 60, 2, 48);
    check(zap::mark_important(s.section.data(), s.grid.data(), false) == 0, "skipped");
    check(zap::mark_important(nullptr, s.grid.data(), false) == 0, "no section");
}

void test_map_names() {
    check(zap::map_name("\xFB" "OS INSTALL DISK") == "OS INSTALL DISK", "own disk");
    check(zap::map_name("\xEE" "PROGRESSION ITEM\nFOR ALICE") == "FOR ALICE", "long: FOR part");
    check(zap::map_name("\xEE" "SLIDE MOD\n  FOR BOB  ") == "SLIDE MOD FOR BOB", "fits: both");
    check(zap::map_name("AN ITEM NAME THAT IS FAR TOO LONG") == "AN ITEM NAME THAT", "cut");
}

void test_the_stand_in_names() {
    int32_t frames[17] = {};
    const char* chunk = "SCRAP CHUNK";
    for (int i = 0; chunk[i]; ++i) frames[i] = chunk[i];
    check(zap::cursor_shows_stand_in(frames), "SCRAP CHUNK");
    frames[0] = 'X';
    check(!zap::cursor_shows_stand_in(frames), "something else");
    int32_t blank[17] = {};
    check(!zap::cursor_shows_stand_in(blank), "blank");
}

// A page of its own, so it can be made read-only.
struct Page {
    BYTE* p = static_cast<BYTE*>(
        VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Page() = default;
    Page(const Page&) = delete;
    ~Page() { VirtualFree(p, 0, MEM_RELEASE); }
    BYTE* data() { return p; }
    void read_only(bool on) {
        DWORD old = 0;
        VirtualProtect(p, 0x1000, on ? PAGE_READONLY : PAGE_READWRITE, &old);
    }
};

// The map's menu as the helper walks it: the menu, its first panel, the panel's children (the
// first holding the handler, the fourth the cursor), and the info panel ([[panel + 0x300]] + 8)
// with the scale (+0x1F0, through one more pointer) and the name widget (its fourth child).
// Built in place: it points into itself.
struct MapMenu {
    std::vector<BYTE> cursor = std::vector<BYTE>(0x200);
    Page first;  // the child holding the handler, in both slots
    std::vector<BYTE> panel = std::vector<BYTE>(0x400), scale = std::vector<BYTE>(0x200);
    std::vector<BYTE> info = std::vector<BYTE>(0x400), widget = std::vector<BYTE>(0x400);
    std::vector<BYTE> text = std::vector<BYTE>(0x268);
    std::vector<std::vector<BYTE>> glyph_objects;
    BYTE* glyphs[17] = {};
    BYTE* children[4] = {};
    BYTE* info_children[4] = {};
    BYTE* panels[1] = {};
    BYTE* top_panels = nullptr;
    BYTE* holder = nullptr;  // what current_menu holds: the menu, whose +0 is its panel list
    BYTE *a = nullptr, *b[2] = {}, *e = nullptr;
    int handler = 0;
    explicit MapMenu(double s) {
        children[0] = first.data();
        children[3] = cursor.data();
        void* h = &handler;
        std::memcpy(first.data() + 0x168, &h, sizeof(h));  // both slots, as the builder sets them
        std::memcpy(first.data() + 0x178, &h, sizeof(h));
        BYTE* list = reinterpret_cast<BYTE*>(children);
        std::memcpy(panel.data() + 0x1E0, &list, sizeof(list));
        panels[0] = panel.data();
        top_panels = reinterpret_cast<BYTE*>(panels);
        holder = reinterpret_cast<BYTE*>(&top_panels);
        e = scale.data();
        std::memcpy(scale.data() + 0x158, &s, sizeof(s));
        BYTE* d = reinterpret_cast<BYTE*>(&e);
        std::memcpy(info.data() + 0x1F0, &d, sizeof(d));
        b[1] = info.data();
        a = reinterpret_cast<BYTE*>(b);
        BYTE* pa = reinterpret_cast<BYTE*>(&a);
        std::memcpy(panel.data() + 0x300, &pa, sizeof(pa));
        info_children[3] = widget.data();
        BYTE* ilist = reinterpret_cast<BYTE*>(info_children);
        std::memcpy(info.data() + 0x1E0, &ilist, sizeof(ilist));
        BYTE* t = text.data();
        std::memcpy(widget.data() + 0x1F0, &t, sizeof(t));
        int32_t n = 17;
        std::memcpy(text.data() + 0x1D8, &n, 4);
        glyph_objects.resize(17, std::vector<BYTE>(0x1D8));
        for (int i = 0; i < 17; ++i) glyphs[i] = glyph_objects[i].data();
        BYTE* g = reinterpret_cast<BYTE*>(glyphs);
        std::memcpy(text.data() + 0x228, &g, sizeof(g));
    }
    MapMenu(const MapMenu&) = delete;
    BYTE* current_menu() { return reinterpret_cast<BYTE*>(&holder); }
    void at(int16_t x, int16_t y) {
        std::memcpy(cursor.data() + 0x1E6, &x, 2);
        std::memcpy(cursor.data() + 0x1E4, &y, 2);
    }
    void* slot() {  // both slots' handler, or null if they differ
        void* h = slot_at(0x168);
        return h == slot_at(0x178) ? h : nullptr;
    }
    void* slot_at(ptrdiff_t at) {
        void* h = nullptr;
        std::memcpy(&h, first.data() + at, sizeof(h));
        return h;
    }
    void set_slot(ptrdiff_t at, void* h) { std::memcpy(first.data() + at, &h, sizeof(h)); }
    void show(const char* s) {  // as 0xA61C0 writes the 17 glyphs
        size_t n = std::strlen(s);
        for (size_t i = 0; i < 17; ++i) {
            int32_t f = i < n ? static_cast<uint8_t>(s[i]) : 0;
            std::memcpy(glyphs[i] + 0x1C4, &f, 4);
        }
    }
    std::string shown() {
        std::string out;
        for (int i = 0; i < 17; ++i) {
            int32_t f = 0;
            std::memcpy(&f, glyphs[i] + 0x1C4, 4);
            out += f ? static_cast<char>(f) : ' ';
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }
};

MapMenu* g_set_target = nullptr;
int g_sets = 0;
void fake_text_set(void* text, const char* s, int first, int count) {
    ++g_sets;
    if (g_set_target != nullptr && text == g_set_target->text.data() && first == 0 && count == 17) {
        g_set_target->show(s);
    }
}
int g_hook = 0;

// The helper's addresses for `m`, the map's handler held at `handler_slot`.
zap::MapAddresses addresses(MapMenu& m, void*& handler_slot) {
    zap::MapAddresses a;
    a.current_menu = m.current_menu();
    a.map_handler = reinterpret_cast<BYTE*>(&handler_slot);
    a.text_set_fn = reinterpret_cast<void*>(&fake_text_set);
    a.hook = &g_hook;
    return a;
}

// The cursor's square, as the name the helper writes for it: row 17, column 13 labeled.
void test_the_cursor_square() {
    const std::map<int, std::string> labels = {{17 * 54 + 13, "\xFB" "OS INSTALL DISK"}};
    for (double scale : {40.0, 20.0}) {
        MapMenu m(scale);
        void* handler_slot = &m.handler;
        zap::MapAddresses a = addresses(m, handler_slot);
        zap::MapCursorNames names;
        names.watch(a, true);
        g_set_target = &m;
        // Zoomed in: (165 + 5) / 10 = 17, (100 + 4) / 8 = 13; out: (380 + 11) / 23 = 17,
        // (220 + 8) / 17 = 13.
        const bool fine = scale == 40.0;
        m.at(fine ? 165 : 380, fine ? 100 : 220);
        m.show("SCRAP CHUNK");
        names.after(a, labels);
        check(m.shown() == "OS INSTALL DISK", fine ? "zoomed in" : "zoomed out");
        m.at(fine ? 175 : 403, fine ? 100 : 220);  // row 18
        m.show("SCRAP CHUNK");
        names.after(a, labels);
        check(m.shown() == "SCRAP CHUNK", fine ? "zoomed in: row 18" : "zoomed out: row 18");
        names.restore(a);
        g_set_target = nullptr;
    }
    MapMenu m(40.0);
    int other = 0;
    void* handler_slot = &other;  // the top menu is not the map
    zap::MapAddresses a = addresses(m, handler_slot);
    zap::MapCursorNames names;
    names.watch(a, true);
    check(!names.holds_hook() && m.slot() == &m.handler, "another menu");
    a.map_handler = nullptr;
    names.watch(a, true);
    check(!names.holds_hook() && m.slot() == &m.handler, "no handler known");
}

// No stand-in given (no hooks before configure): nothing is wrapped, nothing written.
void test_no_stand_in_wraps_nothing() {
    MapMenu m(40.0);
    void* handler_slot = &m.handler;
    zap::MapAddresses a = addresses(m, handler_slot);
    a.hook = nullptr;
    zap::MapCursorNames names;
    names.watch(a, true);
    check(m.slot() == &m.handler && !names.holds_hook(), "no stand-in: the game's handler");
    m.set_slot(0x178, nullptr);  // a null slot is not the stand-in either
    names.watch(a, true);
    check(m.slot_at(0x168) == &m.handler && m.slot_at(0x178) == nullptr && !names.holds_hook(),
          "a null slot left alone");
}

// The game's own writes over one slot, and a child that cannot be written.
void test_one_slot_and_a_read_only_child() {
    MapMenu m(40.0);
    void* handler_slot = &m.handler;
    zap::MapAddresses a = addresses(m, handler_slot);
    zap::MapCursorNames names;
    names.watch(a, true);
    check(m.slot() == &g_hook, "both slots wrapped");
    // The game's reveal writes +0x168 only: +0x178 still holds the stand-in, so it stays wrapped.
    int reveal = 0;
    m.set_slot(0x168, &reveal);
    names.watch(a, true);
    check(names.holds_hook() && m.slot_at(0x168) == &reveal && m.slot_at(0x178) == &g_hook,
          "one slot the stand-in: still wrapped, the other left as the game wrote it");
    names.restore(a);
    check(!names.holds_hook() && m.slot_at(0x168) == &reveal && m.slot_at(0x178) == &m.handler,
          "restored: only the slot holding the stand-in put back");
    // Only +0x168 the stand-in now.
    m.set_slot(0x168, &m.handler);
    names.watch(a, true);
    m.set_slot(0x178, &reveal);
    names.watch(a, true);
    check(names.holds_hook() && m.slot_at(0x168) == &g_hook, "+0x168 alone: still wrapped");
    // Read-only: kept as the helper's (so an unload refuses), then put back once writable.
    m.first.read_only(true);
    names.restore(a);
    check(names.holds_hook() && m.slot_at(0x168) == &g_hook, "a read-only child: kept");
    m.first.read_only(false);
    names.restore(a);
    check(!names.holds_hook() && m.slot_at(0x168) == &m.handler && m.slot_at(0x178) == &reveal,
          "writable again: put back");
    // And not wrapped while it cannot be written.
    m.first.read_only(true);
    names.watch(a, true);
    check(!names.holds_hook() && m.slot_at(0x168) == &m.handler, "a read-only child not wrapped");
    m.first.read_only(false);
}

void test_the_handler_wrapped_and_the_name_rewritten() {
    MapMenu m(40.0);
    void* handler_slot = &m.handler;  // what map_handler holds: the game's handler
    zap::MapAddresses a = addresses(m, handler_slot);
    zap::MapCursorNames names;
    names.watch(a, false);
    check(m.slot() == &m.handler && !names.holds_hook(), "off: the game's handler");
    names.watch(a, true);
    check(m.slot() == &g_hook && names.holds_hook(), "on: the helper's stand-in");
    check(names.original(m.first.data()) == &m.handler, "the game's handler to call");
    check(names.original(m.panel.data()) == nullptr, "only for the map's child");
    const std::map<int, std::string> labels = {
        {17 * 54 + 13, "\xFB" "OS INSTALL DISK"},
        {17 * 54 + 14, "\xEE" "PROGRESSION ITEM\nFOR ALICE"}};
    g_set_target = &m;
    m.at(165, 100);
    m.show("SCRAP CHUNK");  // the game's handler wrote the record's name
    names.after(a, labels);
    check(m.shown() == "OS INSTALL DISK", "rewritten in the same frame");
    m.at(165, 108);  // (108 + 4) / 8 = 14
    m.show("SCRAP CHUNK");
    names.after(a, labels);
    check(m.shown() == "FOR ALICE", "another player's");
    m.show("CURRENT LOCATION");
    g_sets = 0;
    names.after(a, labels);
    check(g_sets == 0 && m.shown() == "CURRENT LOCATION", "another text left alone");
    m.at(165, 116);  // no label
    m.show("SCRAP CHUNK");
    names.after(a, labels);
    check(m.shown() == "SCRAP CHUNK", "no label: the game's");
    names.watch(a, false);
    check(m.slot() == &m.handler && !names.holds_hook(), "off again: put back");
    names.watch(a, true);
    names.restore(a);
    check(m.slot() == &m.handler && !names.holds_hook(), "restored before an unload");
    names.watch(a, true);
    // The navigation room's reveal puts the game's handlers back in both slots: wrapped again.
    void* game_handler = &m.handler;
    std::memcpy(m.first.data() + 0x168, &game_handler, sizeof(game_handler));
    std::memcpy(m.first.data() + 0x178, &game_handler, sizeof(game_handler));
    names.watch(a, true);
    check(m.slot() == &g_hook && names.holds_hook(), "wrapped again after the game's own");
    m.holder = nullptr;  // the map closed: its menu gone
    names.watch(a, true);
    check(!names.holds_hook(), "a closed map's child forgotten");
    g_set_target = nullptr;
}

}  // namespace

int main() {
    test_map_names();
    test_the_stand_in_names();
    test_the_cursor_square();
    test_the_handler_wrapped_and_the_name_rewritten();
    test_no_stand_in_wraps_nothing();
    test_one_slot_and_a_read_only_child();
    test_the_values();
    test_only_the_stand_ins_squares();
    test_obscure_majors_from_the_settings();
    test_a_cell_off_the_map_is_skipped();
    if (g_failures == 0) std::printf("mapmarks tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
