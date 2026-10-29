// Host tests for the file select's entries (files.h), the disk count and the AP logo: a stand-in
// file screen laid out as read live on B1 (three save entries, each a label of 145 glyphs, 5 rows
// of 29).

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "apitem.h"
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

// The keys' block: the header (85, 86), then the circles (114, 115, 143, 144).
constexpr int kKeyCells[6] = {85, 86, 114, 115, 143, 144};
// The logo's cells: columns 16 and 17 of rows 0 and 1.
constexpr int kLogoCells[4] = {16, 17, 45, 46};

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
    // The game's fill, as 0xDF840 leaves it: rows 0 and 1 spaces but FILE NN, the keys if any.
    void fill(bool keys) {
        for (int i = 0; i < 145; ++i) {
            at<int32_t>(g(i), 0x1C4) = i < 58 || (i >= 58 + 22 && i < 58 + 27) ? 0x20 : 0;
        }
        for (int c = 0; c < 7; ++c) at<int32_t>(g(8 + c), 0x1C4) = "FILE 04"[c];
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
            bool keys = std::count(std::begin(kKeyCells), std::end(kKeyCells), i) != 0;
            at<BYTE*>(g(i), 0x198) = keys ? g_palette2 : nullptr;
            at<int16_t>(g(i), 0x1A0) = keys ? 2 : 26;
            // Rows 0 and 1 (FILE NN) white, as the keys; the rest another style.
            at<int32_t>(g(i), 0x1D0) = keys || i < 58 ? 0x15 : 2;
        }
        block[0x935E] = 0;  // not empty
        at<uint16_t>(block.data(), 0x9400) = 1;  // a randomizer save
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
    zap::GameAddresses addresses() {
        zap::GameAddresses a;
        a.current_menu = reinterpret_cast<BYTE*>(&current);
        a.file_panel_fn = reinterpret_cast<BYTE*>(&file_panel);
        a.file_entry_fn = reinterpret_cast<BYTE*>(&file_entry);
        a.copy_entry_fn = reinterpret_cast<BYTE*>(&copy_entry);
        a.delete_entry_fn = reinterpret_cast<BYTE*>(&delete_entry);
        a.hooks.file_entry = reinterpret_cast<void*>(&hook);
        return a;
    }
};

constexpr int kDi = zap::kDiskHeaderGlyphs[0], kSk = zap::kDiskHeaderGlyphs[1];

// The logo's cells show the logo (else the game's spaces), and in which rows: raised 4 px.
bool logo(Entry& e) {
    for (int k = 0; k < 4; ++k) {
        if (e.frame(kLogoCells[k]) != zap::kApLogoGlyphs[k]) return false;
    }
    return true;
}
bool spaces(Entry& e) {
    for (int i : kLogoCells) {
        if (e.frame(i) != 0x20) return false;
    }
    return true;
}
bool raised(Entry& e) {
    return e.y(16) == e.y(15) - 4 && e.y(17) == e.y(15) - 4 && e.y(45) == e.y(44) - 4 &&
           e.y(46) == e.y(44) - 4 && e.boxed(16) && e.boxed(46);
}
bool in_rows(Entry& e) {
    return e.y(16) == e.y(15) && e.y(46) == e.y(44) && e.boxed(16) && e.boxed(46);
}

// The keys' block shows `want`.
bool cells(Entry& e, std::vector<int> want) {
    for (int k = 0; k < 6; ++k) {
        if (e.frame(kKeyCells[k]) != want[k]) return false;
    }
    return true;
}

void test_this_seeds_slots_show_the_disks() {
    Screen s;
    zap::GameAddresses a = s.addresses();
    s.e[0].fill(false);             // slot 4: this seed, before the scanner
    s.e[1].fill(true);              // slot 5: this seed (with keys, say)
    s.e[2].fill(true);              // slot 6: another save, with keys
    zap::FileEntries f;
    f.set_counts({{4, 3}, {5, 3}});
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
    check(at<void*>(s.e[0].widget, 0x2C8) == a.hooks.file_entry && f.holds_hook(), "the handlers wrapped");
    check(f.original(s.e[0].widget) == reinterpret_cast<void*>(&file_entry), "the game's kept");

    // The game fills the entry again (paging): the helper's handler rewrites it after event 0xF.
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
    f.set_counts({{4, 12}, {5, 12}});
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

// COPY FILE's and DELETE FILE's entries: wrapped as the file select's, each with its own handler
// put back.
void test_copy_and_delete_entries() {
    Screen s;
    zap::GameAddresses a = s.addresses();
    auto* copy = reinterpret_cast<void*>(&copy_entry);
    auto* del = reinterpret_cast<void*>(&delete_entry);
    at<void*>(s.e[0].widget, 0x2C8) = copy;
    at<void*>(s.e[1].widget, 0x2C8) = del;
    for (Entry& e : s.e) e.fill(false);
    zap::FileEntries f;
    f.set_counts({{4, 3}, {5, 3}});
    f.watch(a);
    check(cells(s.e[0], {kDi, kSk, '0', '3', 0, 0}) && cells(s.e[1], {kDi, kSk, '0', '3', 0, 0}),
          "COPY FILE's and DELETE FILE's entries: DISK and 03");
    check(f.original(s.e[0].widget) == copy && f.original(s.e[1].widget) == del,
          "each one's handler kept");
    f.set_counts({});
    f.watch(a);
    check(at<void*>(s.e[0].widget, 0x2C8) == copy && at<void*>(s.e[1].widget, 0x2C8) == del,
          "off: their handlers back");
}

void test_only_what_it_moved_goes_back() {
    Screen s;
    zap::GameAddresses a = s.addresses();
    s.e[0].fill(false);
    at<double>(s.e[0].g(114), 0x48) = 51;  // not where the game's layout puts it
    zap::FileEntries f;
    f.set_counts({{4, 3}});
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
    zap::GameAddresses a = s.addresses();
    s.e[0].fill(false);
    at<void*>(s.e[0].widget, 0x2C8) = reinterpret_cast<void*>(&file_panel);  // another handler
    zap::FileEntries f;
    f.set_counts({{4, 3}});
    f.watch(a);
    check(cells(s.e[0], {0, 0, 0, 0, 0, 0}) && s.e[0].y(114) == 48, "not wrapped: untouched");
}

void test_no_disk_shows_nothing() {
    Screen s;
    s.e[0].fill(true);  // the KEYS header (flag 39), no disk yet
    zap::FileEntries f;
    f.set_counts({{4, 0}});
    f.watch(s.addresses());
    check(cells(s.e[0], {0, 0, 0, 0, 0, 0}), "no disk: the block blank, as with no key");
    check(s.e[0].y(114) == 48, "and the count's row where the game put it");
    f.set_counts({{4, 1}});
    f.watch(s.addresses());
    check(cells(s.e[0], {kDi, kSk, '0', '1', 0, 0}) && s.e[0].y(114) == 53, "the first disk: shown");
    f.restore(s.addresses());
}

void test_anything_else_is_left_alone() {
    zap::FileEntries f;
    f.set_counts({{4, 3}, {5, 3}, {6, 3}});
    struct Case {
        const char* what;
        void (*spoil)(Screen&);
        bool not_wrapped;  // its handler left as the game's too
    };
    const Case cases[] = {
        {"an empty slot: untouched", [](Screen& s) { s.e[0].block[0x935E] = 0xFF; }, false},
        {"something else in the keys' block (not a key circle): untouched",
         [](Screen& s) { at<int32_t>(s.e[0].g(114), 0x1C4) = 'X'; }, false},
        {"another label: untouched, not wrapped",
         [](Screen& s) { at<int32_t>(s.e[0].label, 0x1D8) = 144; }, true},
    };
    for (const Case& c : cases) {
        Screen s;
        s.e[0].fill(false);
        c.spoil(s);
        f.watch(s.addresses());
        const bool games = at<void*>(s.e[0].widget, 0x2C8) == reinterpret_cast<void*>(&file_entry);
        check(s.e[0].frame(85) == 0 && (games || !c.not_wrapped), c.what);
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

void test_ap_saves_show_the_logo() {
    Screen s;
    zap::GameAddresses a = s.addresses();
    for (Entry& e : s.e) e.fill(true);
    zap::FileEntries f;
    f.set_ap_slots({4, 6});  // slot 5 a normal or plain randomizer save
    check(f.on() && f.marks_on() && !f.disks_on(), "on, with no disk count");
    f.watch(a);
    check(logo(s.e[0]) && raised(s.e[0]) && logo(s.e[2]), "slots 4 and 6: the logo, 4 px up");
    check(spaces(s.e[1]) && in_rows(s.e[1]), "slot 5: left alone");
    check(s.e[0].frame(15) == 0x20 && s.e[0].frame(8) == 'F' && s.e[0].y(8) == 24,
          "FILE NN and the cells beside the logo as they were");
    check(s.e[0].frame(85) == 0x81 && s.e[0].frame(114) == 0x84 && s.e[0].y(114) == 48,
          "the keys as they were");
    check(at<void*>(s.e[1].widget, 0x2C8) == a.hooks.file_entry, "every entry's handler wrapped");

    // A fill (paging): the logo again after event 0xF, its glyphs still raised.
    s.e[0].fill(true);
    f.on_event(a, s.e[0].widget, 0xF);
    check(logo(s.e[0]) && raised(s.e[0]), "rewritten after the fill");
    // The entry reused for slot 5: the game's spaces, back in their rows.
    at<int32_t>(s.e[0].widget, 0x230) = 5;
    s.e[0].fill(true);
    f.on_event(a, s.e[0].widget, 0xF);
    check(spaces(s.e[0]) && in_rows(s.e[0]), "another save: the game's cells, in their rows");
    at<int32_t>(s.e[0].widget, 0x230) = 4;
    s.e[0].fill(true);
    f.watch(a);  // a fill the handler did not see (a copy)
    check(logo(s.e[0]) && raised(s.e[0]), "an AP save again, from the watch");

    // With the disk count: both, each in its own cells.
    f.set_counts({{4, 3}});
    f.watch(a);
    check(logo(s.e[0]) && s.e[0].frame(85) == zap::kDiskHeaderGlyphs[0] &&
              s.e[0].frame(114) == '0' && s.e[0].frame(115) == '3' && s.e[0].y(114) == 53,
          "the logo and the disk count together");
    f.set_counts({});
    f.watch(a);
    check(logo(s.e[0]) && s.e[0].frame(85) == 0x81 && s.e[0].y(114) == 48,
          "the count off: the keys back, the logo kept");

    // A save no longer an AP one (its seed file gone, say): the logo goes at once.
    f.set_ap_slots({6});
    f.watch(a);
    check(spaces(s.e[0]) && in_rows(s.e[0]) && logo(s.e[2]), "slot 4 no longer marked");

    f.set_ap_slots({});
    f.watch(a);
    check(spaces(s.e[2]) && in_rows(s.e[2]) && !f.holds_hook() &&
              at<void*>(s.e[2].widget, 0x2C8) == reinterpret_cast<void*>(&file_entry),
          "off: the game's cells, rows and handlers back");
}

void test_the_logo_put_back_before_an_unload() {
    Screen s;
    zap::GameAddresses a = s.addresses();
    s.e[0].fill(false);
    zap::FileEntries f;
    f.set_ap_slots({4});
    f.set_counts({{4, 2}});
    f.watch(a);
    check(logo(s.e[0]) && raised(s.e[0]), "marked");
    f.restore(a);
    check(spaces(s.e[0]) && in_rows(s.e[0]) && s.e[0].frame(85) == 0 && s.e[0].y(114) == 48 &&
              !f.holds_hook(),
          "restore: the cells, rows, keys' block and handlers the game's, with it still on");
}

void test_the_logo_only_where_it_keeps_its_colors() {
    zap::FileEntries f;
    f.set_ap_slots({4});
    struct Case {
        const char* what;
        void (*spoil)(Entry&);
    };
    const Case cases[] = {
        {"an empty slot", [](Entry& e) { e.block[0x935E] = 0xFF; }},
        {"a normal save (copied over the slot since the last look)",
         [](Entry& e) { at<uint16_t>(e.block.data(), 0x9400) = 0; }},
        {"a cell not blank", [](Entry& e) { at<int32_t>(e.g(45), 0x1C4) = 'X'; }},
        {"a shader palette (colors lost)", [](Entry& e) { at<int16_t>(e.g(17), 0x1A0) = 2; }},
        {"a style not white (colors tinted)", [](Entry& e) { at<int32_t>(e.g(46), 0x1D0) = 0x17; }},
        {"a cell whose frames stop before the logo's last glyph",
         [](Entry& e) { at<int32_t>(e.g(16), 0x1BC) = zap::kApLogoGlyphs[3]; }},
    };
    for (const Case& c : cases) {
        Screen s;
        s.e[0].fill(false);
        c.spoil(s.e[0]);
        f.watch(s.addresses());
        check(!logo(s.e[0]) && s.e[0].frame(16) == 0x20 && in_rows(s.e[0]), c.what);
        f.restore(s.addresses());
    }
}

void test_the_logo_image() {
    // 16 rows of 16, every pixel transparent or a known color, over four glyphs two by two in
    // the font.
    bool fits = true;
    for (const auto& row : zap::kApLogoRows) {
        fits = fits && std::strlen(row) == 16;
        for (int x = 0; x < 16 && fits; ++x) {
            uint8_t rgb[3];
            fits = row[x] == '.' || zap::glyph_rgb(row[x], rgb);
        }
    }
    check(fits, "the logo: 16 x 16, known colors");
    check(zap::kApLogoX == 96 && zap::kApLogoY == 1040, "the logo at glyph 0x8C's place");
    uint8_t rgb[3];
    check(zap::glyph_rgb('#', rgb) && rgb[0] == 255 && rgb[2] == 255 && !zap::glyph_rgb('.', rgb),
          "white and transparent as before");
}

void test_the_new_games_fade() {
    // The logo whitened through a new game's 40 frames, from 1/40 to all white; else not.
    check(zap::new_game_white(true, 40) == 6 && zap::new_game_white(true, 20) == 133 &&
              zap::new_game_white(true, 1) == 255,
          "whiter frame by frame, all white on the last");
    check(zap::new_game_white(false, 20) == 0 && zap::new_game_white(true, 0) == 0 &&
              zap::new_game_white(true, 41) == 0 && zap::new_game_white(true, -1) == 0,
          "no new game queued, or its timer out of range: its colors");
}

void test_the_request() {
    std::map<int, int> counts{{1, 1}};
    check(zap::parse_disk_slots("disk_slots", counts).empty() && counts.empty(), "off");
    check(zap::parse_disk_slots("disk_slots 3 15 4,5", counts).empty() &&
              counts == std::map<int, int>{{4, 3}, {5, 3}},
          "a count, the number needed and the slots");
    check(zap::parse_disk_slots("disk_slots 3 15 -", counts).empty() && counts.empty(),
          "no slot of this seed");
    check(!zap::parse_disk_slots("disk_slots 3 15 31", counts).empty(), "slots 1 to 30");
    check(!zap::parse_disk_slots("disk_slots 3 15 4,x", counts).empty(), "numbers only");
    check(!zap::parse_disk_slots("disk_slots 3 0 4", counts).empty() && counts.empty(),
          "the number needed is still checked");
    check(zap::file_disk_digits(3) == "03" && zap::file_disk_digits(15) == "15" &&
              zap::file_disk_digits(120) == "99",
          "two digits");
    // The painted DISK keeps to the game's squashed letters' rows (2 to 6).
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
    test_copy_and_delete_entries();
    test_only_what_it_moved_goes_back();
    test_an_entry_not_wrapped_is_not_written();
    test_no_disk_shows_nothing();
    test_anything_else_is_left_alone();
    test_ap_saves_show_the_logo();
    test_the_logo_put_back_before_an_unload();
    test_the_logo_only_where_it_keeps_its_colors();
    test_the_logo_image();
    test_the_new_games_fade();
    test_the_request();
    return report("files tests");
}
