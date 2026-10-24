// A stand-in game-mode panel for the native tests, laid out as traced on B1 (menu.h): the most
// recent menu, its panel list, the panel, four children, RANDOMIZER's label and its 11 glyphs,
// each with a frame table.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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

    template <typename T>
    static T& at(BYTE* base, ptrdiff_t offset) {
        return *reinterpret_cast<T*>(base + offset);
    }

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
        const char text[] = "RANDOMIZER ";
        for (int i = 0; i < zap::kLabelLength; ++i) {
            BYTE* g = glyphs[i];
            glyph_list[i] = g;
            at<BYTE*>(g, 0x1A8) = table.data();
            at<int32_t>(g, 0x1BC) = kFrames;
            at<int32_t>(g, 0x1C4) = text[i];
            at<BYTE*>(g, 0x1B0) = table.data() + text[i] * 0x38;
        }
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

    zap::MenuAddresses addresses() {
        zap::MenuAddresses a;
        a.current_menu = reinterpret_cast<BYTE*>(&current);
        a.panel_handler = panel_handler_ptr;
        a.widget_handler = widget_handler_ptr;
        a.rando_menu = rando_menu_ptr;
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

    template <typename T>
    static T& at(BYTE* base, ptrdiff_t offset) {
        return *reinterpret_cast<T*>(base + offset);
    }

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
        current = menu;
    }

    BYTE palette_objects[2][16] = {};
    BYTE* palettes[2] = {palette_objects[0], palette_objects[1]};

    zap::MenuAddresses addresses() {
        zap::MenuAddresses a;
        a.current_menu = reinterpret_cast<BYTE*>(&current);
        a.widget_handler = widget_handler_ptr;
        a.assist_menu = assist_menu_ptr;
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
            int32_t c = at<int32_t>(glyph_list[i], 0x1C4);
            if (at<BYTE*>(glyph_list[i], 0x1B0) != table.data() + c * 0x38) return "frame mismatch";
            s += static_cast<char>(c);
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

}  // namespace stand_in
