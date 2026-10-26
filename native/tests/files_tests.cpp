// Host tests for the file select's disk count (files.h): a stand-in file screen laid out as read
// live on B1 (three save entries, each a label of 145 glyphs, 5 rows of 29).

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "equip.h"
#include "files.h"
#include "requests.h"

namespace {

int g_calls = 0;
// Bodies of their own: the linker folds identical functions into one address.
void file_panel() { g_calls += 1; }
void file_entry() { g_calls += 2; }
void copy_entry() { g_calls += 3; }
void delete_entry() { g_calls += 4; }
void hook() { g_calls += 5; }

// One count for each of `slots`, as the client's disk_slots sets them.
std::map<int, int> each(int count, const std::set<int>& slots) {
    std::map<int, int> out;
    for (int slot : slots) out[slot] = count;
    return out;
}

BYTE g_frames[256 * 0x38];
BYTE g_palette2[8];

struct Entry {
    BYTE widget[0x2D0] = {};
    BYTE label[0x268] = {};
    BYTE* glyph_list[145] = {};
    std::vector<BYTE> glyphs = std::vector<BYTE>(145 * 0x1D8);
    std::vector<BYTE> block = std::vector<BYTE>(0x9408);
    BYTE* g(int i) { return glyphs.data() + i * 0x1D8; }
    int frame(int i) { return at<int32_t>(g(i), 0x1C4); }
    double y(int i) { return at<double>(g(i), 0x48); }
    bool boxed(int i) { return at<double>(g(i), 0x130) == y(i) && at<double>(g(i), 0x140) == y(i) + 8; }
    // The game's fill of the key glyphs, as 0xDF840 leaves them.
    void fill(bool keys) {
        for (int i = 0; i < 145; ++i) at<int32_t>(g(i), 0x1C4) = i >= 58 + 22 && i < 58 + 27 ? 0x20 : 0;
        if (keys) {
            at<int32_t>(g(85), 0x1C4) = 0x81;
            at<int32_t>(g(86), 0x1C4) = 0x82;
            for (int i : {114, 115, 143, 144}) at<int32_t>(g(i), 0x1C4) = 0x84;
        }
    }
    void make(int slot) {
        at<int32_t>(widget, 0x230) = slot;
        at<BYTE*>(widget, 0x1F8) = block.data();
        at<BYTE*>(widget, 0x1F0) = label;
        at<void*>(widget, 0x2C8) = reinterpret_cast<void*>(&file_entry);
        at<int32_t>(label, 0x1D8) = 145;
        at<int32_t>(label, 0x1DC) = 29;
        at<BYTE**>(label, 0x228) = glyph_list;
        for (int i = 0; i < 145; ++i) {
            glyph_list[i] = g(i);
            at<int32_t>(g(i), 0x1BC) = 256;
            // Laid out as 0xA79E0 lays it out: 8 a row, row 2 at y 40 (entry 0, read live).
            at<double>(g(i), 0x48) = 24.0 + 8 * (i / 29);
            at<double>(g(i), 0x130) = 24.0 + 8 * (i / 29);
            at<double>(g(i), 0x140) = 32.0 + 8 * (i / 29);
            at<BYTE*>(g(i), 0x1A8) = g_frames;
            bool keys = i == 85 || i == 86 || i == 114 || i == 115 || i == 143 || i == 144;
            at<BYTE*>(g(i), 0x198) = keys ? g_palette2 : nullptr;
            at<int16_t>(g(i), 0x1A0) = keys ? 2 : 26;
            at<int32_t>(g(i), 0x1D0) = keys ? 0x15 : 2;
        }
        block[0x935E] = 0;  // not empty
    }
};

struct Screen {
    BYTE menu[0x80] = {};
    BYTE* panels[5] = {};
    BYTE panel[0x300] = {};
    BYTE* children[4] = {};
    Entry e[3];
    BYTE* current = nullptr;
    Screen() {
        at<BYTE*>(menu, 0) = reinterpret_cast<BYTE*>(panels);
        panels[0] = panel;
        at<void*>(panel, 0x2F8) = reinterpret_cast<void*>(&file_panel);
        at<int32_t>(panel, 0x1E8) = 4;
        at<BYTE**>(panel, 0x1E0) = children;
        for (int i = 0; i < 3; ++i) {
            e[i].make(4 + i);
            children[i] = e[i].widget;
        }
        current = menu;
    }
    zap::FileAddresses addresses() {
        zap::FileAddresses a;
        a.current_menu = reinterpret_cast<BYTE*>(&current);
        a.file_panel_fn = reinterpret_cast<void*>(&file_panel);
        a.entry_fns[0] = reinterpret_cast<void*>(&file_entry);
        a.entry_fns[1] = reinterpret_cast<void*>(&copy_entry);
        a.entry_fns[2] = reinterpret_cast<void*>(&delete_entry);
        a.hook = reinterpret_cast<void*>(&hook);
        return a;
    }
};

constexpr int kDi = zap::kDiskHeaderGlyphs[0], kSk = zap::kDiskHeaderGlyphs[1];

// The keys' block: the header (85, 86), then the circles (114, 115, 143, 144).
bool cells(Entry& e, std::vector<int> want) {
    const int block[6] = {85, 86, 114, 115, 143, 144};
    for (int k = 0; k < 6; ++k) {
        if (e.frame(block[k]) != want[k]) return false;
    }
    return true;
}

void test_this_seeds_slots_show_the_disks() {
    Screen s;
    zap::FileAddresses a = s.addresses();
    s.e[0].fill(false);             // slot 4: this seed, before the scanner
    s.e[1].fill(true);              // slot 5: this seed (with keys, say)
    s.e[2].fill(true);              // slot 6: another save, with keys
    zap::FileDisks f;
    f.set_counts(each(3, {4, 5}));
    f.watch(a);
    check(cells(s.e[0], {kDi, kSk, '0', '3', 0, 0}), "slot 4: DISK and 03");
    check(cells(s.e[1], {kDi, kSk, '0', '3', 0, 0}), "slot 5: DISK and 03, the keys gone");
    check(at<BYTE*>(s.e[0].g(114), 0x198) == g_palette2 && at<int32_t>(s.e[0].g(114), 0x1D0) == 0x15,
          "drawn as the keys");
    check(s.e[0].frame(58 + 23) == 0x20, "row 2's blank columns the game's");
    check(s.e[2].frame(85) == 0x81 && s.e[2].frame(114) == 0x84, "slot 6 keeps its keys");
    check(s.e[0].y(114) == 53 && s.e[0].y(115) == 53 && s.e[0].boxed(114),
          "the count 5 pixels down from its row");
    check(s.e[0].y(143) == 56 && s.e[0].y(85) == 40 && s.e[2].y(114) == 48, "nothing else moved");
    check(at<void*>(s.e[0].widget, 0x2C8) == a.hook && f.holds_hook(), "the handlers wrapped");
    check(f.original(s.e[0].widget) == reinterpret_cast<void*>(&file_entry), "the game's kept");

    // The game fills the entry again (paging): the stand-in rewrites it after.
    s.e[0].fill(false);
    f.on_event(a, s.e[0].widget, 0xF);
    check(cells(s.e[0], {kDi, kSk, '0', '3', 0, 0}), "rewritten after the fill");

    // The entry reused for a save not of this seed (paging): the keys in their row.
    at<int32_t>(s.e[0].widget, 0x230) = 6;
    s.e[0].fill(true);
    f.watch(a);
    check(s.e[0].frame(85) == 0x81 && s.e[0].y(114) == 48 && s.e[0].boxed(114),
          "another save's keys in their row");
    at<int32_t>(s.e[0].widget, 0x230) = 4;
    s.e[0].fill(false);
    f.watch(a);
    check(s.e[0].y(114) == 53, "this seed's again: dropped");

    // A new count: the helper's own cells rewritten.
    f.set_counts(each(12, {4, 5}));
    f.watch(a);
    check(cells(s.e[1], {kDi, kSk, '1', '2', 0, 0}), "a new count: 12");

    f.set_counts({});
    f.watch(a);
    check(at<void*>(s.e[0].widget, 0x2C8) == reinterpret_cast<void*>(&file_entry) &&
              !f.holds_hook(),
          "off: the game's handlers back");
    check(s.e[0].y(114) == 48 && s.e[1].y(115) == 48 && s.e[1].boxed(115),
          "off: the count's glyphs back in their row");
    check(cells(s.e[0], {0, 0, 0, 0, 0, 0}) && cells(s.e[1], {0x81, 0x82, 0x84, 0x84, 0x84, 0x84}),
          "off: the game's frames back (slot 5's KEYS and keys)");
}

void test_only_what_it_moved_goes_back() {
    Screen s;
    zap::FileAddresses a = s.addresses();
    s.e[0].fill(false);
    at<double>(s.e[0].g(114), 0x48) = 51;  // not where the game's layout puts it
    zap::FileDisks f;
    f.set_counts(each(3, {4}));
    f.watch(a);
    check(s.e[0].y(114) == 51 && s.e[0].y(115) == 53, "a glyph out of place: left alone");
    // A fill the helper does not see (a copy, say): the game's frames, the helper's place.
    s.e[0].fill(true);
    f.watch(a);
    check(cells(s.e[0], {kDi, kSk, '0', '3', 0, 0}) && s.e[0].y(115) == 53,
          "a fill unseen: rewritten, its old frames forgotten");
    f.set_counts({});
    f.watch(a);
    check(cells(s.e[0], {0x81, 0x82, 0x84, 0x84, 0x84, 0x84}) && s.e[0].y(115) == 48,
          "off: the latest fill's frames back");
}

void test_an_entry_not_wrapped_is_not_written() {
    Screen s;
    zap::FileAddresses a = s.addresses();
    s.e[0].fill(false);
    at<void*>(s.e[0].widget, 0x2C8) = reinterpret_cast<void*>(&file_panel);  // another handler
    zap::FileDisks f;
    f.set_counts(each(3, {4}));
    f.watch(a);
    check(cells(s.e[0], {0, 0, 0, 0, 0, 0}) && s.e[0].y(114) == 48, "not wrapped: untouched");
}

void test_no_disk_shows_nothing() {
    Screen s;
    s.e[0].fill(true);  // the KEYS header (flag 39), no disk yet
    zap::FileDisks f;
    f.set_counts(each(0, {4}));
    f.watch(s.addresses());
    check(cells(s.e[0], {0, 0, 0, 0, 0, 0}), "no disk: the block blank, as with no key");
    check(s.e[0].y(114) == 48, "and the count's row where the game put it");
    f.set_counts(each(1, {4}));
    f.watch(s.addresses());
    check(cells(s.e[0], {kDi, kSk, '0', '1', 0, 0}) && s.e[0].y(114) == 53, "the first disk: shown");
    f.restore(s.addresses());
}

void test_anything_else_is_left_alone() {
    zap::FileDisks f;
    f.set_counts(each(3, {4, 5, 6}));
    {
        Screen s;
        s.e[0].fill(false);
        s.e[0].block[0x935E] = 0xFF;  // an empty slot
        f.watch(s.addresses());
        check(s.e[0].frame(85) == 0, "an empty slot: untouched");
        f.restore(s.addresses());
    }
    {
        Screen s;
        s.e[0].fill(false);
        at<int32_t>(s.e[0].g(114), 0x1C4) = 'X';  // not a key circle
        f.watch(s.addresses());
        check(s.e[0].frame(85) == 0, "something else in the keys' block: untouched");
        f.restore(s.addresses());
    }
    {
        Screen s;
        s.e[0].fill(false);
        at<int32_t>(s.e[0].label, 0x1D8) = 144;
        f.watch(s.addresses());
        check(s.e[0].frame(85) == 0 && at<void*>(s.e[0].widget, 0x2C8) ==
                                                   reinterpret_cast<void*>(&file_entry),
              "another label: untouched, not wrapped");
        f.restore(s.addresses());
    }
    {
        Screen s;
        f.watch(s.addresses());
        BYTE title[0x80] = {};
        s.current = title;  // the screen closed: freed with its entries
        f.watch(s.addresses());
        check(!f.holds_hook(), "a closed screen forgotten");
    }
}

void test_the_request() {
    int count = -1;
    std::set<int> slots;
    check(zap::parse_disk_slots("disk_slots", count, slots).empty() && slots.empty(), "off");
    check(zap::parse_disk_slots("disk_slots 3 15 4,5", count, slots).empty() && count == 3 &&
              slots == std::set<int>{4, 5},
          "a count, the number needed and the slots");
    check(zap::parse_disk_slots("disk_slots 3 15 -", count, slots).empty() && slots.empty(),
          "no slot of this seed");
    check(!zap::parse_disk_slots("disk_slots 3 15 31", count, slots).empty(), "slots 1 to 30");
    check(!zap::parse_disk_slots("disk_slots 3 15 4,x", count, slots).empty(), "numbers only");
    check(!zap::parse_disk_slots("disk_slots 3 0 4", count, slots).empty() && count == 0,
          "the number needed is still checked");
    check(zap::file_disk_digits(3) == "03" && zap::file_disk_digits(15) == "15" &&
              zap::file_disk_digits(120) == "99",
          "two digits");
    // The painted DISK keeps to the game's squashed letters' rows (2 to 6) and leaves no glyph
    // blank.
    for (const auto& rows : zap::kDiskHeaderRows) {
        bool fits = std::strcmp(rows[0], "........") == 0 && std::strcmp(rows[1], "........") == 0 &&
                    std::strcmp(rows[7], "........") == 0;
        for (int y = 0; y < 8; ++y) fits = fits && std::strlen(rows[y]) == 8;
        check(fits, "DISK in rows 2 to 6");
    }
}

}  // namespace

int main() {
    test_this_seeds_slots_show_the_disks();
    test_only_what_it_moved_goes_back();
    test_an_entry_not_wrapped_is_not_written();
    test_no_disk_shows_nothing();
    test_anything_else_is_left_alone();
    test_the_request();
    if (g_failures == 0) std::printf("files tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
