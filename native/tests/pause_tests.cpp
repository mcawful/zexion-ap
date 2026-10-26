// Host tests for the pause screen's disk row (pause.h): a stand-in pause screen laid out as read
// live on B1 (its KEYS panel: a 4-glyph label and nine sprites), a stand-in `jne` in code, and
// a stand-in for the game's sprite-sheet function.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "check.h"
#include "pause.h"

namespace {

int g_calls = 0;
void widget_handler() { g_calls += 1; }
void equipment_fn(void*, int) { g_calls += 2; }
void other_fn(void*, int) { g_calls += 3; }

#pragma section(".zapcode", read, execute)
__declspec(allocate(".zapcode")) unsigned char g_branch[4] = {0x75, 0x2E, 0x90, 0xC3};

// The branch's first byte as the game would run it (not folded to its initial value).
unsigned char branch() { return static_cast<volatile unsigned char*>(g_branch)[0]; }

// The branch's first byte set by the test, as the game's code: unprotected for the write.
void set_branch(unsigned char b) {
    DWORD old = 0;
    VirtualProtect(g_branch, 1, PAGE_EXECUTE_READWRITE, &old);
    static_cast<volatile unsigned char*>(g_branch)[0] = b;
    VirtualProtect(g_branch, 1, old, &old);
}

// The game's (object, template): the template's frames.
void sprite_sheet(void* o, void* t) {
    at<int32_t>(static_cast<BYTE*>(o), 0x1BC) = at<int32_t>(static_cast<BYTE*>(t), 0x1BC);
    at<BYTE*>(static_cast<BYTE*>(o), 0x1A8) = at<BYTE*>(static_cast<BYTE*>(t), 0x1A8);
}

BYTE g_frames[1024 * 0x38];

struct Pause {
    BYTE menu[0x80] = {};
    BYTE* panels[2] = {};
    BYTE entries_panel[0x300] = {};
    BYTE* entries[2] = {};
    BYTE entry[2][0x2D0] = {};
    BYTE row[0x300] = {};
    BYTE* row_children[1] = {};
    BYTE keys[0x2D0] = {};
    BYTE label[0x268] = {};
    BYTE* glyphs[4] = {};
    BYTE glyph[4][0x1D8] = {};
    BYTE* sprites[9] = {};
    BYTE sprite[9][0x1D8] = {};
    BYTE font[0x1D8] = {};
    BYTE* templates[40] = {};
    BYTE* templates_holder = nullptr;
    BYTE palette0[8] = {};
    BYTE* current = nullptr;

    Pause() {
        at<BYTE*>(menu, 0) = reinterpret_cast<BYTE*>(panels);
        at<int32_t>(menu, 8) = 2;
        panels[0] = entries_panel;
        panels[1] = row;
        at<BYTE**>(entries_panel, 0x1E0) = entries;
        at<int32_t>(entries_panel, 0x1E8) = 2;
        entries[0] = entry[0];
        entries[1] = entry[1];
        at<void*>(entry[0], 0x2C0) = reinterpret_cast<void*>(&other_fn);
        at<void*>(entry[1], 0x2C0) = reinterpret_cast<void*>(&equipment_fn);
        at<int32_t>(row, 0x1E8) = 1;
        at<BYTE**>(row, 0x1E0) = row_children;
        at<int32_t>(row, 0x1F8) = 9;
        at<BYTE**>(row, 0x1F0) = sprites;
        row_children[0] = keys;
        at<void*>(keys, 0x2C8) = reinterpret_cast<void*>(&widget_handler);
        at<BYTE*>(keys, 0x1F0) = label;
        at<int32_t>(label, 0x1D8) = 4;
        at<BYTE**>(label, 0x228) = glyphs;
        const char* text = "KEYS";
        for (int i = 0; i < 4; ++i) {
            glyphs[i] = glyph[i];
            at<int32_t>(glyph[i], 0x1BC) = 256;
            at<BYTE*>(glyph[i], 0x1A8) = g_frames;
            at<int32_t>(glyph[i], 0x1C4) = text[i];
            at<BYTE*>(glyph[i], 0x198) = palette0;
            at<int32_t>(glyph[i], 0x1D0) = 0x15;
            at<float>(glyph[i], 0x68) = 1.0f;
            glyph[i][0x120] = 1;
        }
        for (int k = 0; k < 9; ++k) {
            sprites[k] = sprite[k];
            at<int32_t>(sprite[k], 0x1BC) = k < 4 ? 390 : 912;
            at<BYTE*>(sprite[k], 0x1A8) = g_frames;
            at<int32_t>(sprite[k], 0x1C4) = k < 4 ? 167 : 380 + k;
            at<float>(sprite[k], 0x68) = 0.26f;
            at<double>(sprite[k], 0x148) = -8;
            at<double>(sprite[k], 0x150) = -8;
            at<double>(sprite[k], 0x158) = 8;
            at<double>(sprite[k], 0x160) = 8;
            sprite[k][0x120] = k == 8 ? 0 : 1;
        }
        at<int32_t>(font, 0x1BC) = 256;
        at<BYTE*>(font, 0x1A8) = g_frames;
        templates[1] = font;
        templates_holder = reinterpret_cast<BYTE*>(templates);
        current = menu;
    }

    zap::PauseAddresses addresses() {
        zap::PauseAddresses a;
        a.current_menu = reinterpret_cast<BYTE*>(&current);
        a.equipment_fn = reinterpret_cast<void*>(&equipment_fn);
        a.widget_handler = reinterpret_cast<void*>(&widget_handler);
        a.sprite_templates = reinterpret_cast<BYTE*>(&templates_holder);
        a.sprite_sheet_fn = reinterpret_cast<void*>(&sprite_sheet);
        a.keys_branch = g_branch;
        return a;
    }
    int frame(BYTE* s) { return at<int32_t>(s, 0x1C4); }
};

void test_the_row_shows_the_disks() {
    Pause p;
    zap::PauseDiskRow row;
    zap::PauseAddresses a = p.addresses();
    row.set(zap::pause_row_text(2, 0));
    row.watch(a);
    check(branch() == 0xEB && row.patched(), "the panel always built (jne to jmp)");
    // " 2 OF ? " on the eight cells: sprites 0, 1, the label's four glyphs, sprites 2, 3.
    const char* text = " 2 OF ? ";
    check(p.frame(p.sprite[0]) == text[0] && p.frame(p.sprite[1]) == text[1], "two on the left");
    for (int i = 0; i < 4; ++i) check(p.frame(p.glyph[i]) == text[2 + i], "four on the label");
    check(p.frame(p.sprite[2]) == text[6] && p.frame(p.sprite[3]) == text[7], "two on the right");
    check(at<double>(p.sprite[0], 0x40) == 128 && at<double>(p.sprite[3], 0x40) == 184 &&
              at<double>(p.sprite[3], 0x48) == 208,
          "on the label's line, either side of it");
    check(at<double>(p.sprite[0], 0x148) == 0 && at<double>(p.sprite[0], 0x130) == 208,
          "drawn from their top left, as the font is");
    check(at<int32_t>(p.sprite[0], 0x1D0) == 0x15 && at<float>(p.sprite[0], 0x68) == 1.0f &&
              at<BYTE*>(p.sprite[0], 0x198) == p.palette0,
          "drawn as the label is");
    check(p.frame(p.sprite[4]) == 829 && at<double>(p.sprite[4], 0x40) == 160 &&
              at<double>(p.sprite[4], 0x48) == 184 && p.sprite[4][0x120] == 1 &&
              at<float>(p.sprite[4], 0x68) == 1.0f,
          "the small Data Disc in the middle, white");
    check(p.sprite[5][0x120] == 0 && p.sprite[7][0x120] == 0 && p.sprite[8][0x120] == 0,
          "the other key icons hidden");
    // Turned once: a second look changes nothing.
    at<int32_t>(p.sprite[0], 0x1C4) = 'X';
    row.watch(a);
    check(p.frame(p.sprite[0]) == 'X', "turned once");

    row.set("");
    row.watch(a);
    check(branch() == 0x75 && !row.patched(), "off: the jne back");
}

void test_anything_else_is_left_alone() {
    zap::PauseDiskRow row;
    {
        Pause p;
        at<void*>(p.entry[1], 0x2C0) = reinterpret_cast<void*>(&other_fn);  // no EQUIPMENT
        row.set(zap::pause_row_text(2, 15));
        row.watch(p.addresses());
        check(p.frame(p.sprite[4]) == 384 && p.frame(p.glyph[0]) == 'K', "not the pause screen");
    }
    {
        Pause p;
        at<int32_t>(p.glyph[0], 0x1C4) = 'Z';  // not KEYS
        row.watch(p.addresses());
        check(p.frame(p.sprite[4]) == 384, "not the KEYS row");
    }
    {
        Pause p;
        at<int32_t>(p.row, 0x1F8) = 8;
        row.watch(p.addresses());
        check(p.frame(p.sprite[4]) == 384, "another sprite count");
    }
    row.set("");
    Pause p;
    row.watch(p.addresses());
    set_branch(0x74);  // not the game's jne: never written
    row.set(zap::pause_row_text(2, 15));
    row.watch(p.addresses());
    check(branch() == 0x74 && !row.patched(), "another instruction left alone");
    set_branch(0x75);
    row.restore(p.addresses());
}

void test_the_text() {
    check(zap::pause_row_text(2, 0) == " 2 OF ? ", "before the scanner");
    check(zap::pause_row_text(12, 15) == "12 OF 15", "after");
    check(zap::pause_row_text(3, 9) == " 3 OF 9 ", "centered");
}

}  // namespace

int main() {
    test_the_row_shows_the_disks();
    test_anything_else_is_left_alone();
    test_the_text();
    if (g_failures == 0) std::printf("pause tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
