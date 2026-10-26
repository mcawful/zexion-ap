// Stand-in menus for the native tests, laid out as traced on B1: the game-mode panel (menu.h:
// the most recent menu, its panel list, the panel, four children, RANDOMIZER's label and its 11
// glyphs, each with a frame table), the pause menu's options, and the pause screen (pause.h).
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "menu.h"

namespace stand_in {

constexpr int kFrames = 256;  // the font's 256 glyphs

// One game-mode panel: the menu, its panel list, the panel, four children, RANDOMIZER's label
// and its 11 glyphs, each with a frame table.
struct ModePanel {
    void* const panel_handler_ptr;
    void* const widget_handler_ptr;
    void* const rando_menu_ptr;
    BYTE menu[0x80] = {};  // its panel list at +0, the menu below at +0x78
    BYTE* panels[4] = {};
    BYTE panel[0x300] = {};
    BYTE* children[4] = {};
    BYTE child[0x2D0] = {};
    BYTE label[0x230] = {};
    BYTE* glyph_list[zap::kLabelLength] = {};
    BYTE glyphs[zap::kLabelLength][0x1D0] = {};
    std::vector<BYTE> table = std::vector<BYTE>(kFrames * 0x38);
    BYTE* current = nullptr;  // what current_menu holds

    ModePanel(void* panel_handler, void* widget_handler, void* rando_menu)
        : panel_handler_ptr(panel_handler),
          widget_handler_ptr(widget_handler),
          rando_menu_ptr(rando_menu) {
        at<BYTE*>(menu, 0) = reinterpret_cast<BYTE*>(panels);
        panels[3] = panel;
        at<void*>(panel, 0x2F8) = panel_handler_ptr;
        at<int32_t>(panel, 0x1E8) = 4;
        at<BYTE**>(panel, 0x1E0) = children;
        children[1] = child;
        at<void*>(child, 0x2C0) = rando_menu_ptr;
        at<void*>(child, 0x2C8) = widget_handler_ptr;
        at<BYTE*>(child, 0x1F0) = label;
        at<int32_t>(label, 0x1D8) = zap::kLabelLength;
        at<BYTE**>(label, 0x228) = glyph_list;
        for (int i = 0; i < zap::kLabelLength; ++i) {
            glyph_list[i] = glyphs[i];
            at<BYTE*>(glyphs[i], 0x1A8) = table.data();
            at<int32_t>(glyphs[i], 0x1BC) = kFrames;
        }
        reset_label();
        current = menu;
    }

    // The label as the game builds it, as on a later visit to the file screen.
    void reset_label() {
        const char text[] = "RANDOMIZER ";
        for (int i = 0; i < zap::kLabelLength; ++i) {
            at<int32_t>(glyphs[i], 0x1C4) = text[i];
            at<BYTE*>(glyphs[i], 0x1B0) = table.data() + text[i] * 0x38;
        }
    }

    zap::GameAddresses addresses() {
        zap::GameAddresses a;
        a.current_menu = reinterpret_cast<BYTE*>(&current);
        a.panel_handler_fn = static_cast<BYTE*>(panel_handler_ptr);
        a.widget_handler_fn = static_cast<BYTE*>(widget_handler_ptr);
        a.rando_menu_fn = static_cast<BYTE*>(rando_menu_ptr);
        return a;
    }

    std::string text() {
        std::string s;
        for (BYTE* g : glyph_list) {
            int32_t c = at<int32_t>(g, 0x1C4);
            if (at<BYTE*>(g, 0x1B0) != table.data() + c * 0x38) return "frame mismatch";
            s += static_cast<char>(c);
        }
        return s;
    }

    void* callback() { return at<void*>(child, 0x2C0); }
};

// The pause menu's options, as built with a save loaded (menu.h, AssistLock): a menu with two
// panels, the second holding eight entries, ASSIST at 6 with its 12 glyphs (ASSIST and six
// blanks) styled 0x15, as read live.
struct OptionsMenu {
    void* const widget_handler_ptr;
    void* const assist_menu_ptr;
    BYTE menu[0x80] = {};
    BYTE* panels[2] = {};
    BYTE first[0x300] = {};
    BYTE second[0x300] = {};
    BYTE* children[8] = {};
    BYTE others[7][0x2D0] = {};
    BYTE child[0x2D0] = {};
    BYTE label[0x230] = {};
    BYTE* glyph_list[zap::kAssistLength] = {};
    BYTE glyphs[zap::kAssistLength][0x1E0] = {};
    std::vector<BYTE> table = std::vector<BYTE>(kFrames * 0x38);
    BYTE* current = nullptr;

    OptionsMenu(void* widget_handler, void* assist_menu)
        : widget_handler_ptr(widget_handler), assist_menu_ptr(assist_menu) {
        at<BYTE*>(menu, 0) = reinterpret_cast<BYTE*>(panels);
        at<int32_t>(menu, 8) = 2;
        panels[0] = first;
        panels[1] = second;
        at<int32_t>(second, 0x1E8) = 8;
        at<int32_t>(second, 0x1EC) = 8;   // all eight selectable (the game's has ITEMS after)
        at<int32_t>(second, 0x2DC) = -1;  // no cursor yet
        at<BYTE**>(second, 0x1E0) = children;
        for (int i = 0; i < 7; ++i) {
            children[i < 6 ? i : 7] = others[i];
            at<void*>(others[i], 0x2C8) = widget_handler_ptr;
        }
        children[6] = child;
        for (int i = 0; i < 8; ++i) at<int32_t>(children[i], 0x1E0) = i;
        at<void*>(child, 0x2C0) = assist_menu_ptr;
        at<void*>(child, 0x2C8) = widget_handler_ptr;
        at<BYTE*>(child, 0x1F0) = label;
        at<int32_t>(label, 0x1D8) = zap::kAssistLength;
        at<BYTE**>(label, 0x228) = glyph_list;
        const char text[zap::kAssistLength + 1] = "ASSIST\0\0\0\0\0";  // six blanks
        for (int i = 0; i < zap::kAssistLength; ++i) {
            BYTE* g = glyphs[i];
            glyph_list[i] = g;
            at<BYTE*>(g, 0x1A8) = table.data();
            at<int32_t>(g, 0x1BC) = kFrames;
            at<int32_t>(g, 0x1C4) = text[i];
            at<BYTE*>(g, 0x1B0) = table.data() + text[i] * 0x38;
            at<int32_t>(g, 0x1D0) = 0x15;
            for (ptrdiff_t t : {0x68, 0x6C, 0x70, 0x74, 0x78, 0x7C, 0x80, 0x84}) at<float>(g, t) = 1.0f;
            at<void*>(g, 0x198) = palette_objects[1];  // the menu's own palette: number 26, say
            at<int16_t>(g, 0x1A0) = 26;
        }
        palettes[26] = palette_objects[1];
        current = menu;
    }

    BYTE palette_objects[2][16] = {};
    // The palette table by number, as the game's: 0 the locked look's, 26 the menu's own; the
    // rest empty, so a lookup reads the table and nothing past it.
    BYTE* palettes[65] = {palette_objects[0]};

    zap::GameAddresses addresses() {
        zap::GameAddresses a;
        a.current_menu = reinterpret_cast<BYTE*>(&current);
        a.widget_handler_fn = static_cast<BYTE*>(widget_handler_ptr);
        a.assist_menu_fn = static_cast<BYTE*>(assist_menu_ptr);
        a.palette_table = reinterpret_cast<BYTE*>(palettes);
        return a;
    }

    // Every glyph's palette object and number, if they agree; {nullptr, -1} if not.
    std::pair<void*, int16_t> palette() {
        void* p = at<void*>(glyphs[0], 0x198);
        int16_t n = at<int16_t>(glyphs[0], 0x1A0);
        for (BYTE* g : glyph_list) {
            if (at<void*>(g, 0x198) != p || at<int16_t>(g, 0x1A0) != n) {
                return {nullptr, static_cast<int16_t>(-1)};
            }
        }
        return {p, n};
    }

    void* callback() { return at<void*>(child, 0x2C0); }
    // The label's first six characters, if each glyph's frame matches its character.
    std::string text() {
        std::string s;
        for (int i = 0; i < 6; ++i) {
            if (code(i) < 0) return "frame mismatch";
            s += static_cast<char>(code(i));
        }
        return s;
    }

    // Glyph i's character, if its frame matches it; -1 if not.
    int32_t code(int i) {
        int32_t c = at<int32_t>(glyph_list[i], 0x1C4);
        return at<BYTE*>(glyph_list[i], 0x1B0) == table.data() + c * 0x38 ? c : -1;
    }

    // Every glyph's style, if they agree; -1 if not.
    int32_t style() {
        int32_t s = at<int32_t>(glyphs[0], 0x1D0);
        for (BYTE* g : glyph_list) {
            if (at<int32_t>(g, 0x1D0) != s) return -1;
        }
        return s;
    }
};

// The pause screen (pause.h), laid out as read live on B1: two panels, the first holding
// EQUIPMENT, the second the KEYS row (a 4-glyph label and nine sprites), drawn from a frame list
// whose frame 829 is the game's small Data Disc.
inline BYTE g_pause_frames[1024 * 0x38];
inline BYTE* const g_disc_frame = g_pause_frames + 829 * 0x38;  // the game's small Data Disc
// The key icons' quad, as read live.
inline const double kKeyCorners[8] = {-8, -8, 8, -8, 8, 8, -8, 8};

// Frame 829's entry as read live on B1: its texture rectangle (464, 816)-(480, 832) over 2048,
// then the rest.
inline void fill_pause_frames() {
    const float rect[4] = {464 / 2048.0f, 816 / 2048.0f, 480 / 2048.0f, 832 / 2048.0f};
    std::memcpy(g_disc_frame, rect, sizeof(rect));
    at<uint16_t>(g_disc_frame, 0x20) = 0x033D;
    at<uint16_t>(g_disc_frame, 0x22) = 0x033D;
    at<uint16_t>(g_disc_frame, 0x2E) = 0xFFFF;
    g_disc_frame[0x30] = 1;
}

struct PauseScreen {
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

    // `equipment`: the EQUIPMENT entry's callback; `other`: the first entry's; `widget_handler`:
    // the KEYS row's handler.
    PauseScreen(void* equipment, void* other, void* widget_handler) {
        fill_pause_frames();
        at<BYTE*>(menu, 0) = reinterpret_cast<BYTE*>(panels);
        at<int32_t>(menu, 8) = 2;
        panels[0] = entries_panel;
        panels[1] = row;
        at<BYTE**>(entries_panel, 0x1E0) = entries;
        at<int32_t>(entries_panel, 0x1E8) = 2;
        entries[0] = entry[0];
        entries[1] = entry[1];
        at<void*>(entry[0], 0x2C0) = other;
        at<void*>(entry[1], 0x2C0) = equipment;
        at<int32_t>(row, 0x1E8) = 1;
        at<BYTE**>(row, 0x1E0) = row_children;
        at<int32_t>(row, 0x1F8) = 9;
        at<BYTE**>(row, 0x1F0) = sprites;
        row_children[0] = keys;
        at<void*>(keys, 0x2C8) = widget_handler;
        at<BYTE*>(keys, 0x1F0) = label;
        at<int32_t>(label, 0x1D8) = 4;
        at<BYTE**>(label, 0x228) = glyphs;
        const char* text = "KEYS";
        for (int i = 0; i < 4; ++i) {
            glyphs[i] = glyph[i];
            at<int32_t>(glyph[i], 0x1BC) = 256;
            at<BYTE*>(glyph[i], 0x1A8) = g_pause_frames;
            at<int32_t>(glyph[i], 0x1C4) = text[i];
            at<BYTE*>(glyph[i], 0x198) = palette0;
            at<int32_t>(glyph[i], 0x1D0) = 0x15;
            at<float>(glyph[i], 0x68) = 1.0f;
            glyph[i][0x120] = 1;
        }
        for (int k = 0; k < 9; ++k) {
            sprites[k] = sprite[k];
            at<int32_t>(sprite[k], 0x1BC) = k < 4 ? 390 : 912;
            at<BYTE*>(sprite[k], 0x1A8) = g_pause_frames;
            at<int32_t>(sprite[k], 0x1C4) = k < 4 ? 167 : 380 + k;
            at<BYTE*>(sprite[k], 0x30) = font;  // a template, as the game's keep theirs
            for (int c = 0; c < 8; ++c) at<double>(sprite[k], 0x98 + 8 * c) = kKeyCorners[c];
            at<float>(sprite[k], 0x68) = 0.26f;
            at<double>(sprite[k], 0x148) = -8;
            at<double>(sprite[k], 0x150) = -8;
            at<double>(sprite[k], 0x158) = 8;
            at<double>(sprite[k], 0x160) = 8;
            sprite[k][0x120] = k == 8 ? 0 : 1;
        }
        at<int32_t>(font, 0x1BC) = 256;
        at<BYTE*>(font, 0x1A8) = g_pause_frames;
        templates[1] = font;
        templates_holder = reinterpret_cast<BYTE*>(templates);
        current = menu;
    }

    int frame(BYTE* s) { return at<int32_t>(s, 0x1C4); }
};

}  // namespace stand_in
