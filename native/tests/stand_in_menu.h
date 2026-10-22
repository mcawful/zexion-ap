// A stand-in game-mode panel for the native tests, laid out as traced on B1 (menu.h): the most
// recent menu, its panel list, the panel, four children, RANDOMIZER's label and its 11 glyphs,
// each with a frame table.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "menu.h"

namespace stand_in {

constexpr int kFrames = 128;

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

}  // namespace stand_in
