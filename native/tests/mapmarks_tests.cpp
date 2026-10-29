// Host tests for the map's markers and names (mapmarks.h): a stand-in randomizer section and map
// grid, and a stand-in map menu with its cursor, scale and name text.

#include <windows.h>

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

// The Obscure settings: Majors, Minors, Class.
constexpr zap::Obscure kNone{}, kPresets{true, true, false}, kMajorsOnly{true, false, false},
    kMinorsOnly{false, true, false}, kClass{false, false, true}, kAll{true, true, true},
    kMajorsAndClass{true, false, true};

void test_the_values() {
    // Each value as the game would have set it for a major. Bases: 0xC a square a hint or map
    // revealed, 0x18 one seen directly.
    struct Row {
        BYTE in;
        zap::Obscure obscure;
        bool finished;
        BYTE out;
        const char* what;
    };
    for (const Row& r : {
             // Shown minors become shown majors, in each base.
             Row{0x01, kNone, false, 0x02, "1 -> 2"},
             Row{0x0D, kNone, false, 0x0E, "0xD -> 0xE"},
             Row{0x19, kNone, false, 0x1A, "0x19 -> 0x1A"},
             // Obscured minors: obscured majors if majors are obscured, else shown majors.
             Row{0x13, kPresets, false, 0x15, "0x13 -> 0x15, majors obscured"},
             Row{0x1F, kPresets, false, 0x21, "0x1F -> 0x21, seen after a hint"},
             Row{0x13, kMinorsOnly, false, 0x0E, "0x13 -> 0xE, majors shown"},
             Row{0x1F, kPresets, true, 0x1A, "finished game: shown"},
             // Minors obscured too: a shown minor was a mode-3 reveal, a major shown there.
             Row{0x0D, kPresets, false, 0x0E, "presets: shown stays shown"},
             Row{0x19, kPresets, false, 0x1A, "presets: seen, shown"},
             // Majors on, Minors and Class off: a hinted square is obscured, a seen one shown.
             Row{0x0D, kMajorsOnly, false, 0x15, "majors only: hinted -> obscured major"},
             Row{0x19, kMajorsOnly, false, 0x1A, "majors only: seen -> shown major"},
             Row{0x01, kMajorsOnly, false, 0x02, "majors only: base 0 shown"},
             Row{0x0D, kMajorsOnly, true, 0x0E, "majors only, finished game: shown"},
             // Under Obscure Class the game sent the minor down the major's path unless the
             // mode was 3 (or the player's room revealed it), where a major is shown too.
             Row{0x0D, kClass, false, 0x0E, "class: shown stays shown"},
             Row{0x0D, kMajorsAndClass, false, 0x0E, "majors and class: shown stays shown"},
             Row{0x0D, kAll, false, 0x0E, "all three: shown stays shown"},
             Row{0x13, kClass, false, 0x15, "class: obscured stays obscured"},
             Row{0x0D, kMinorsOnly, false, 0x0E, "minors only: shown stays shown"},
         }) {
        check(zap::important_value(r.in, r.obscure, r.finished) == r.out, r.what);
    }
    // Majors, collected items, unrevealed squares and values above the classes stay, under
    // every setting.
    for (zap::Obscure o : {kNone, kPresets, kMajorsOnly, kMinorsOnly, kClass, kAll}) {
        for (BYTE v :
             std::vector<BYTE>{0x00, 0x02, 0x09, 0x0C, 0x0E, 0x15, 0x18, 0x1A, 0x1B, 0x21, 0x22,
                               0x24, 0x30, 0x33, 0x81}) {
            check(zap::important_value(v, o, false) == v, "left alone");
        }
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

void test_secret_shuffles_155th() {
    // The list's 155th entry, in the four bytes before the item table, is a location only with
    // Secret Shuffle (t37): game_layout.h, rando::locations.
    Save s;
    s.place(154, 20, 48, 48);
    s.at(20, 48) = 0x19;
    check(zap::mark_important(s.section.data(), s.grid.data(), false) == 0,
          "without Secret Shuffle: not a location");
    s.section[0x87B + 37] = 1;
    check(zap::mark_important(s.section.data(), s.grid.data(), false) == 1 &&
              s.at(20, 48) == 0x1A,
          "with it: its stand-in marked");
}

void test_obscure_majors_from_the_settings() {
    Save s;
    s.place(0, 1, 1, 48);
    s.at(1, 1) = 0x13;  // obscured as a minor
    s.section[0x87B + 29] = 1;
    zap::mark_important(s.section.data(), s.grid.data(), false);
    check(s.at(1, 1) == 0x15, "obscured as a major");
}

void test_all_three_settings_read() {
    const zap::Obscure none = zap::obscure_settings(nullptr);
    check(!none.majors && !none.minors && !none.by_class, "no section: all off");
    Save s;
    s.section[0x87B + 29] = 1;
    s.section[0x87B + 38] = 1;
    s.section[0x87B + 41] = 1;
    const zap::Obscure all = zap::obscure_settings(s.section.data());
    check(all.majors && all.minors && all.by_class, "t29, t38, t41");
    s.section[0x87B + 38] = 0;
    s.section[0x87B + 41] = 0;
    const zap::Obscure majors = zap::obscure_settings(s.section.data());
    check(majors.majors && !majors.minors && !majors.by_class, "t29 alone");
}

void test_majors_on_minors_off() {
    // A hinted stand-in (0xD) and a seen one (0x19): obscured and shown majors, as the game's
    // majors there.
    Save s;
    s.place(0, 1, 1, 48);
    s.place(1, 2, 2, 48);
    s.at(1, 1) = 0x0D;
    s.at(2, 2) = 0x19;
    s.section[0x87B + 29] = 1;
    check(zap::mark_important(s.section.data(), s.grid.data(), false) == 2, "both changed");
    check(s.at(1, 1) == 0x15, "hinted: an obscured major");
    check(s.at(2, 2) == 0x1A, "seen: a shown major");
    // With Minors or Class on too, a shown minor in a hinted square was shown on purpose.
    for (int setting : {38, 41}) {
        Save t;
        t.place(0, 1, 1, 48);
        t.at(1, 1) = 0x0D;
        t.section[0x87B + 29] = 1;
        t.section[0x87B + setting] = 1;
        zap::mark_important(t.section.data(), t.grid.data(), false);
        check(t.at(1, 1) == 0x0E, "minors or class on: a shown major");
    }
    // After the ending nothing is obscured.
    Save e;
    e.place(0, 1, 1, 48);
    e.at(1, 1) = 0x0D;
    e.section[0x87B + 29] = 1;
    zap::mark_important(e.section.data(), e.grid.data(), true);
    check(e.at(1, 1) == 0x0E, "finished: a shown major");
}

void test_a_cell_off_the_map_is_skipped() {
    Save s;
    s.place(0, 60, 2, 48);
    check(zap::mark_important(s.section.data(), s.grid.data(), false) == 0, "skipped");
    check(zap::mark_important(nullptr, s.grid.data(), false) == 0, "no section");
}

void test_map_names() {
    check(zap::map_name("\xFB" "OS INSTALL DISK", 16) == "OS INSTALL DISK", "own disk");
    // Two lines of 17: the first only, never whom the item is for.
    check(zap::map_name("\xEE" "PROGRESSION ITEM    FOR ALICE", 17) == "PROGRESSION ITEM",
          "another game's item: the first line");
    check(zap::map_name("\xEE" "SLIDE MOD  FOR BOB", 10) == "SLIDE MOD", "a Zexion item's name");
    check(zap::map_name("AN ITEM NAME THAT IS FAR TOO LONG", 0) == "AN ITEM NAME THAT", "cut");
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

void fake_text_set(void* text, const char* s, int first, int count);
int g_hook = 0;

// The map menu as the helper walks it (mapmarks.cpp): first panel, its children (first holds
// the handler, fourth the cursor), the info panel and its scale and name widget, and what the
// helper's map_handler holds (the map's handler, unless a test says otherwise). Not copyable:
// it points into itself.
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
    void* map_handler = &handler;
    explicit MapMenu(double s) {
        children[0] = first.data();
        children[3] = cursor.data();
        set_slot(0x168, &handler);  // both slots, as the builder sets them
        set_slot(0x178, &handler);
        put(panel.data(), 0x1E0, children);
        panels[0] = panel.data();
        top_panels = reinterpret_cast<BYTE*>(panels);
        holder = reinterpret_cast<BYTE*>(&top_panels);
        e = scale.data();
        put(scale.data(), 0x158, s);
        put(info.data(), 0x1F0, &e);
        b[1] = info.data();
        a = reinterpret_cast<BYTE*>(b);
        put(panel.data(), 0x300, &a);
        info_children[3] = widget.data();
        put(info.data(), 0x1E0, info_children);
        put(widget.data(), 0x1F0, text.data());
        put<int32_t>(text.data(), 0x1D8, 17);
        glyph_objects.resize(17, std::vector<BYTE>(0x1D8));
        for (int i = 0; i < 17; ++i) glyphs[i] = glyph_objects[i].data();
        put(text.data(), 0x228, glyphs);
    }
    MapMenu(const MapMenu&) = delete;
    BYTE* current_menu() { return reinterpret_cast<BYTE*>(&holder); }
    // The helper's addresses for this map, the map's handler held at map_handler.
    zap::GameAddresses addresses() {
        zap::GameAddresses addresses;
        addresses.current_menu = current_menu();
        addresses.map_handler = reinterpret_cast<BYTE*>(&map_handler);
        addresses.text_set_fn = reinterpret_cast<BYTE*>(&fake_text_set);
        addresses.hooks.map = &g_hook;
        return addresses;
    }
    void at(int16_t row, int16_t column) {
        put(cursor.data(), 0x1E6, row);
        put(cursor.data(), 0x1E4, column);
    }
    void* slot() {  // both slots' handler, or null if they differ
        void* h = slot_at(0x168);
        return h == slot_at(0x178) ? h : nullptr;
    }
    void* slot_at(ptrdiff_t offset) { return get<void*>(first.data(), offset); }
    void set_slot(ptrdiff_t offset, void* h) { put(first.data(), offset, h); }
    void show(const char* s) {  // as 0xA61C0 writes the 17 glyphs
        size_t n = std::strlen(s);
        for (size_t i = 0; i < 17; ++i) {
            put<int32_t>(glyphs[i], 0x1C4, i < n ? static_cast<uint8_t>(s[i]) : 0);
        }
    }
    std::string shown() {
        std::string out;
        for (BYTE* g : glyphs) {
            const auto f = get<int32_t>(g, 0x1C4);
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

// The cursor's square, as the name the helper writes for it: row 17, column 13 labeled.
void test_the_cursor_square() {
    const std::map<int, std::string> labels = {{17 * 54 + 13, "\xFB" "OS INSTALL DISK"}};
    const std::map<int, int> widths = {{17 * 54 + 13, 16}};
    for (double scale : {40.0, 20.0}) {
        MapMenu m(scale);
        zap::GameAddresses a = m.addresses();
        zap::MapCursorNames names;
        names.watch(a, true);
        g_set_target = &m;
        // Zoomed in: (165 + 5) / 10 = 17, (100 + 4) / 8 = 13; out: (380 + 11) / 23 = 17,
        // (220 + 8) / 17 = 13.
        const bool fine = scale == 40.0;
        m.at(fine ? 165 : 380, fine ? 100 : 220);
        m.show("SCRAP CHUNK");
        names.after(a, labels, widths);
        check(m.shown() == "OS INSTALL DISK", fine ? "zoomed in" : "zoomed out");
        m.at(fine ? 175 : 403, fine ? 100 : 220);  // row 18
        m.show("SCRAP CHUNK");
        names.after(a, labels, widths);
        check(m.shown() == "SCRAP CHUNK", fine ? "zoomed in: row 18" : "zoomed out: row 18");
        names.restore(a);
        g_set_target = nullptr;
    }
    MapMenu m(40.0);
    int other = 0;
    m.map_handler = &other;  // the top menu is not the map
    zap::GameAddresses a = m.addresses();
    zap::MapCursorNames names;
    names.watch(a, true);
    check(!names.holds_hook() && m.slot() == &m.handler, "another menu");
    a.map_handler = nullptr;
    names.watch(a, true);
    check(!names.holds_hook() && m.slot() == &m.handler, "no handler known");
}

// No wrapper given (no hooks before configure): nothing is wrapped, nothing written.
void test_no_stand_in_wraps_nothing() {
    MapMenu m(40.0);
    zap::GameAddresses a = m.addresses();
    a.hooks.map = nullptr;
    zap::MapCursorNames names;
    names.watch(a, true);
    check(m.slot() == &m.handler && !names.holds_hook(), "no wrapper: the game's handler");
    m.set_slot(0x178, nullptr);  // a null slot is not the wrapper either
    names.watch(a, true);
    check(m.slot_at(0x168) == &m.handler && m.slot_at(0x178) == nullptr && !names.holds_hook(),
          "a null slot left alone");
}

// A write over one slot, and a child that cannot be written.
void test_one_slot_and_a_read_only_child() {
    MapMenu m(40.0);
    zap::GameAddresses a = m.addresses();
    zap::MapCursorNames names;
    names.watch(a, true);
    check(m.slot() == &g_hook, "both slots wrapped");
    // A write over +0x168 only: +0x178 still holds the wrapper, so it stays wrapped.
    int reveal = 0;
    m.set_slot(0x168, &reveal);
    names.watch(a, true);
    check(names.holds_hook() && m.slot_at(0x168) == &reveal && m.slot_at(0x178) == &g_hook,
          "one slot the wrapper: still wrapped, the other left as the game wrote it");
    names.restore(a);
    check(!names.holds_hook() && m.slot_at(0x168) == &reveal && m.slot_at(0x178) == &m.handler,
          "restored: only the slot holding the wrapper put back");
    // Only +0x168 the wrapper now.
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
    zap::GameAddresses a = m.addresses();  // map_handler holding the game's handler
    zap::MapCursorNames names;
    names.watch(a, false);
    check(m.slot() == &m.handler && !names.holds_hook(), "off: the game's handler");
    names.watch(a, true);
    check(m.slot() == &g_hook && names.holds_hook(), "on: the helper's wrapper");
    check(names.original(m.first.data()) == &m.handler, "the game's handler to call");
    check(names.original(m.panel.data()) == nullptr, "only for the map's child");
    const std::map<int, std::string> labels = {
        {17 * 54 + 13, "\xFB" "OS INSTALL DISK"},
        {17 * 54 + 14, "\xEE" "PROGRESSION ITEM    FOR ALICE"}};
    const std::map<int, int> widths = {{17 * 54 + 13, 16}, {17 * 54 + 14, 17}};
    g_set_target = &m;
    m.at(165, 100);
    m.show("SCRAP CHUNK");  // the game's handler wrote the record's name
    names.after(a, labels, widths);
    check(m.shown() == "OS INSTALL DISK", "rewritten in the same frame");
    m.at(165, 108);  // (108 + 4) / 8 = 14
    m.show("SCRAP CHUNK");
    names.after(a, labels, widths);
    check(m.shown() == "PROGRESSION ITEM", "another player's: what it is, not whose");
    m.show("CURRENT LOCATION");
    g_sets = 0;
    names.after(a, labels, widths);
    check(g_sets == 0 && m.shown() == "CURRENT LOCATION", "another text left alone");
    m.at(165, 116);  // no label
    m.show("SCRAP CHUNK");
    names.after(a, labels, widths);
    check(m.shown() == "SCRAP CHUNK", "no label: the game's");
    names.watch(a, false);
    check(m.slot() == &m.handler && !names.holds_hook(), "off again: put back");
    names.watch(a, true);
    names.restore(a);
    check(m.slot() == &m.handler && !names.holds_hook(), "restored before an unload");
    names.watch(a, true);
    // The navigation room's reveal puts the game's handlers back in both slots: wrapped again.
    m.set_slot(0x168, &m.handler);
    m.set_slot(0x178, &m.handler);
    names.watch(a, true);
    check(m.slot() == &g_hook && names.holds_hook(), "wrapped again after the game's own");
    m.holder = nullptr;  // the map closed: its menu gone
    names.watch(a, true);
    check(!names.holds_hook(), "a closed map's child forgotten");
    g_set_target = nullptr;
}

// The item update's write of an on-screen item's square as on B1 (mapmarks.h).
constexpr BYTE kSeenGame[zap::kSeenMarkerSize] = {
    0x80, 0xFA, 0x18, 0x7C, 0x11, 0x66, 0x41, 0x83, 0xE9, 0x03, 0x66, 0x41, 0x83,
    0xF9, 0x20, 0x0F, 0x96, 0xC0, 0x04, 0x19, 0xEB, 0x14, 0x80, 0xFA, 0x0C, 0x7C,
    0x17, 0x66, 0x41, 0x83, 0xE9, 0x03, 0x66, 0x41, 0x83, 0xF9, 0x20, 0x0F, 0x96,
    0xC0, 0x04, 0x0D, 0x42, 0x88, 0x84, 0x31, 0xFD, 0x4F, 0x01, 0x00};

// The write run on its own: run(value, id, out) sets dl = value, r9 = id and r14 so that the
// store (its disp32 the game's, in both) lands on *out (rcx 0), runs the 50 bytes, and returns
// r9 as they leave it.
//   mov r11, r14; mov r14, r8; sub r14, disp32; mov r9, rdx; mov rdx, rcx; xor ecx, ecx;
//   mov al, 0x77; <the write>; mov rax, r9; mov r14, r11; ret
// No stack is touched, so it needs no unwind information.
class Runner {
public:
    explicit Runner(const BYTE (&write)[zap::kSeenMarkerSize]) {
        page_ = static_cast<BYTE*>(
            VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        const BYTE before[] = {0x4D, 0x89, 0xF3, 0x4D, 0x89, 0xC6, 0x49, 0x81, 0xEE, 0, 0, 0,
                               0,    0x49, 0x89, 0xD1, 0x48, 0x89, 0xCA, 0x31, 0xC9, 0xB0, 0x77};
        const BYTE after[] = {0x4C, 0x89, 0xC8, 0x4D, 0x89, 0xDE, 0xC3};
        std::memcpy(page_, before, sizeof(before));
        std::memcpy(page_ + 9, kSeenGame + 46, 4);  // the store's disp32
        std::memcpy(page_ + sizeof(before), write, zap::kSeenMarkerSize);
        std::memcpy(page_ + sizeof(before) + zap::kSeenMarkerSize, after, sizeof(after));
        FlushInstructionCache(GetCurrentProcess(), page_, 4096);
    }
    ~Runner() { VirtualFree(page_, 0, MEM_RELEASE); }
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;
    uint64_t run(uint64_t value, uint64_t id, BYTE* out) const {
        using Fn = uint64_t (*)(uint64_t, uint64_t, BYTE*);
        return reinterpret_cast<Fn>(static_cast<void*>(page_))(value, id, out);
    }

private:
    BYTE* page_ = nullptr;
};

void test_the_seen_marker_runs_as_the_games() {
    BYTE ours[zap::kSeenMarkerSize] = {};
    check(zap::seen_marker_patch(kSeenGame, ours), "built from the game's bytes");
    check(std::memcmp(ours + 35, kSeenGame + 42, 8) == 0, "the game's store, as it is");
    const Runner game(kSeenGame), patched(ours);
    int differ = 0, wrong = 0;
    for (int value = 0; value < 256; ++value) {
        for (int id : {0, 1, 2, 3, 20, 35, 36, 44, 45, 46, 47, 48, 49, 50, 0x2D, 0x30 + 0x10000}) {
            BYTE a = 0xEE, b = 0xEE;
            const uint64_t ra = game.run(value, id, &a), rb = patched.run(value, id, &b);
            if (ra != rb) ++wrong;  // r9 as the game's leaves it
            if (a == b) continue;
            ++differ;
            // Only item 48 differs, in a revealed square: a major's class where the game's
            // gives a minor's (value signed, as the game's jl reads it).
            const auto v = static_cast<int8_t>(value);
            const BYTE want = v >= 0x18 ? 0x1A : 0x0E;
            if ((id & 0xFFFF) != 48 || v < 0xC || a != want - 1 || b != want) ++wrong;
        }
    }
    check(wrong == 0, "as the game's but for item 48", std::to_string(wrong));
    // Item 48 at each value from 0xC to 0x7F, as ID 48 and 0x10030 (r9w 48 either way).
    check(differ == 2 * (0x80 - 0xC), "item 48 a major in every revealed square",
          std::to_string(differ));
    BYTE a = 0xEE;
    game.run(0x19, 3, &a);
    check(a == 0x1A, "the stand-in runner runs the game's write");
    game.run(0x0B, 48, &a);
    check(a == 0x1A, "an unrevealed square is not written");
}

void test_the_seen_marker_refuses_other_bytes() {
    BYTE game[zap::kSeenMarkerSize], out[zap::kSeenMarkerSize] = {};
    for (size_t i = 0; i < 46; ++i) {
        std::memcpy(game, kSeenGame, sizeof(game));
        game[i] ^= 0x01;
        if (zap::seen_marker_patch(game, out)) {
            check(false, "a byte other than the game's taken", std::to_string(i));
            break;
        }
    }
    std::memcpy(game, kSeenGame, sizeof(game));
    game[47] = 0x12;  // another grid: the store copied as it is
    check(zap::seen_marker_patch(game, out) && out[35 + 5] == 0x12, "the store's disp32 kept");
}

void test_the_seen_marker_patched_and_put_back() {
    auto* site = static_cast<BYTE*>(
        VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READ));
    auto put = [site](const BYTE* bytes) {
        DWORD old = 0;
        VirtualProtect(site, 4096, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(site, bytes, zap::kSeenMarkerSize);
        VirtualProtect(site, 4096, old, &old);
    };
    put(kSeenGame);
    BYTE ours[zap::kSeenMarkerSize] = {};
    zap::seen_marker_patch(kSeenGame, ours);
    zap::SeenMarker m;
    m.watch(nullptr, true);
    check(!m.patched(), "no site: nothing");
    m.watch(site, false);
    check(!m.patched() && std::memcmp(site, kSeenGame, sizeof(kSeenGame)) == 0, "off: the game's");
    m.watch(site, true);
    check(m.patched() && m.error().empty() && std::memcmp(site, ours, sizeof(ours)) == 0,
          "on: patched");
    DWORD protect = 0;
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(site, &info, sizeof(info));
    protect = info.Protect;
    check(protect == PAGE_EXECUTE_READ, "its protection put back");
    m.watch(site, true);
    check(m.patched() && std::memcmp(site, ours, sizeof(ours)) == 0, "on again: as it was");
    m.watch(site, false);
    check(!m.patched() && std::memcmp(site, kSeenGame, sizeof(kSeenGame)) == 0, "off: put back");
    m.watch(site, true);
    m.restore();
    check(!m.patched() && std::memcmp(site, kSeenGame, sizeof(kSeenGame)) == 0,
          "restored: the game's");
    // Something else's bytes there: never patched over, and its own once it rewrites ours.
    BYTE other[zap::kSeenMarkerSize];
    std::memcpy(other, kSeenGame, sizeof(other));
    other[0] = 0xE9;
    put(other);
    m.watch(site, true);
    check(!m.patched() && m.error() == "the on-screen marker reads otherwise" &&
              std::memcmp(site, other, sizeof(other)) == 0,
          "another's bytes: left alone");
    put(kSeenGame);
    m.watch(site, true);
    check(m.patched() && m.error().empty(), "the game's again: patched");
    put(other);
    m.restore();
    check(!m.patched() && std::memcmp(site, other, sizeof(other)) == 0,
          "rewritten by another: not put back over it");
    VirtualFree(site, 0, MEM_RELEASE);
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
    test_secret_shuffles_155th();
    test_obscure_majors_from_the_settings();
    test_all_three_settings_read();
    test_majors_on_minors_off();
    test_a_cell_off_the_map_is_skipped();
    test_the_seen_marker_runs_as_the_games();
    test_the_seen_marker_refuses_other_bytes();
    test_the_seen_marker_patched_and_put_back();
    return report("mapmarks tests");
}
