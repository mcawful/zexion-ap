// Host tests for the ARCHIPELAGO choice (menu.h): a stand-in game-mode panel laid out as traced
// on B1, relabeled, put back, and left alone whenever anything on the way does not match.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "menu.h"
#include "stand_in_menu.h"

namespace {

int g_failures = 0;
using stand_in::kFrames;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

void panel_handler() {}
void widget_handler() {}
void rando_menu(void*, int) {}
int g_calls = 0;
void archipelago(void*, int) { ++g_calls; }

struct Stand : stand_in::ModePanel {
    Stand()
        : stand_in::ModePanel(reinterpret_cast<void*>(&panel_handler),
                          reinterpret_cast<void*>(&widget_handler),
                          reinterpret_cast<void*>(&rando_menu)) {}
};

void test_install_and_restore() {
    Stand stand;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = stand.addresses();
    check(choice.install(a, &archipelago), "installs on the game-mode panel");
    check(stand.text() == "ARCHIPELAGO", "relabeled, each glyph's frame from its table");
    check(stand.callback() == reinterpret_cast<void*>(&archipelago), "points at the callback");
    check(!choice.install(a, &archipelago), "installs once");
    check(stand.text() == "ARCHIPELAGO", "still relabeled");
    check(choice.restore(a, &archipelago), "restores");
    check(stand.text() == "RANDOMIZER ", "the label is back");
    check(stand.callback() == reinterpret_cast<void*>(&rando_menu), "the callback is back");
    check(!choice.installed() && !choice.restore(a, &archipelago), "restores once");
}

void test_restore_when_another_menu_is_up() {
    Stand stand;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = stand.addresses();
    choice.install(a, &archipelago);
    BYTE above[0x80] = {};
    Stand::at<BYTE*>(above, 0x78) = stand.menu;  // a menu opened on top; ours still open below
    stand.current = above;
    check(!choice.install(a, &archipelago), "nothing to install on another menu");
    check(!choice.installed() && stand.text() == "RANDOMIZER " &&
              stand.callback() == &rando_menu,
          "the open menu below is put back as soon as it is not the top one");
    stand.current = stand.menu;
    check(choice.install(a, &archipelago) && stand.text() == "ARCHIPELAGO",
          "relabeled again when it is back on top");
    stand.current = above;
    check(choice.restore(a, &archipelago) && stand.text() == "RANDOMIZER ",
          "restore needs only the menu open, not on top");
}

void test_a_closed_menu_is_forgotten_unwritten() {
    Stand stand;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = stand.addresses();
    choice.install(a, &archipelago);
    // The game closed the menu: freed with its widgets, whose contents still look like ours.
    BYTE file_screen[0x80] = {};
    stand.current = file_screen;
    check(!choice.install(a, &archipelago), "nothing to install");
    check(!choice.installed(), "forgets it");
    check(stand.text() == "ARCHIPELAGO" && stand.callback() == &archipelago,
          "writes nothing into the closed menu");

    choice.install(a, &archipelago);  // nothing up: still nothing
    stand.current = stand.menu;
    Stand fresh;
    zap::MenuAddresses b = fresh.addresses();
    choice.install(b, &archipelago);
    fresh.current = nullptr;  // no menu at all (in play)
    check(!choice.restore(b, &archipelago) && fresh.text() == "ARCHIPELAGO",
          "no menus open: nothing written");
}

void test_a_released_widget_is_left_alone() {
    Stand stand;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = stand.addresses();
    choice.install(a, &archipelago);
    Stand::at<int32_t>(stand.child, 0x1C8) = 2;  // released, on the game's dead list
    check(!choice.restore(a, &archipelago), "a released widget is not written");
    check(stand.callback() == &archipelago, "left as it was");
    check(!choice.installed(), "forgotten");
}

void test_the_callback_goes_back_even_if_the_label_changed() {
    Stand stand;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = stand.addresses();
    choice.install(a, &archipelago);
    Stand::at<int32_t>(stand.label, 0x1D8) = 10;
    check(choice.restore(a, &archipelago), "restored");
    check(stand.callback() == &rando_menu, "the live widget no longer points at the helper");
}

void test_release_keeps_the_label_and_returns_the_callback() {
    Stand stand;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = stand.addresses();
    choice.install(a, &archipelago);
    check(choice.release(a, &archipelago), "released");
    check(stand.text() == "ARCHIPELAGO", "the label still reads ARCHIPELAGO");
    check(stand.callback() == &rando_menu, "RANDOMIZER's callback is back");
    check(!choice.installed() && !choice.restore(a, &archipelago), "forgotten: nothing more");
    check(stand.text() == "ARCHIPELAGO", "and nothing written after");

    Stand closed;
    zap::MenuAddresses b = closed.addresses();
    choice.install(b, &archipelago);
    BYTE file_screen[0x80] = {};
    closed.current = file_screen;  // the menu closed before the release
    check(!choice.release(b, &archipelago), "a closed menu is not written");
    check(closed.callback() == &archipelago, "left as it was");
}

void test_a_new_panel_replaces_the_old() {
    Stand first, second;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = first.addresses();
    choice.install(a, &archipelago);
    Stand::at<BYTE*>(second.menu, 0x78) = first.menu;  // a new menu on top of the old one
    first.current = second.menu;
    check(choice.install(a, &archipelago), "installs on the new panel");
    check(first.text() == "RANDOMIZER " && second.text() == "ARCHIPELAGO",
          "the old panel is put back, the new one relabeled");
    choice.restore(a, &archipelago);
    check(second.text() == "RANDOMIZER ", "the new panel is put back");
}

void test_mismatches_leave_the_menu_alone() {
    struct Case {
        const char* what;
        void (*spoil)(Stand&);
    };
    const Case cases[] = {
        {"no menu", [](Stand& s) { s.current = nullptr; }},
        {"a menu pointer that faults", [](Stand& s) { s.current = reinterpret_cast<BYTE*>(16); }},
        {"another panel handler",
         [](Stand& s) { Stand::at<void*>(s.panel, 0x2F8) = &s; }},
        {"another capacity", [](Stand& s) { Stand::at<int32_t>(s.panel, 0x1E8) = 3; }},
        {"a child that faults",
         [](Stand& s) { s.children[1] = reinterpret_cast<BYTE*>(8); }},
        {"another callback", [](Stand& s) { Stand::at<void*>(s.child, 0x2C0) = &s; }},
        {"another widget handler", [](Stand& s) { Stand::at<void*>(s.child, 0x2C8) = &s; }},
        {"another label length", [](Stand& s) { Stand::at<int32_t>(s.label, 0x1D8) = 10; }},
        {"a glyph that faults",
         [](Stand& s) { s.glyph_list[10] = reinterpret_cast<BYTE*>(24); }},
        {"a font without capitals", [](Stand& s) { Stand::at<int32_t>(s.glyphs[4], 0x1BC) = 64; }},
        {"a released widget", [](Stand& s) { Stand::at<int32_t>(s.child, 0x1C8) = 2; }},
        {"a released glyph", [](Stand& s) { Stand::at<int32_t>(s.glyphs[3], 0x1C8) = 2; }},
    };
    for (const Case& c : cases) {
        Stand stand;
        c.spoil(stand);
        std::vector<BYTE> before(stand.child, stand.child + sizeof(stand.child));
        zap::ArchipelagoChoice choice;
        bool installed = choice.install(stand.addresses(), &archipelago);
        bool untouched = std::memcmp(before.data(), stand.child, sizeof(stand.child)) == 0 &&
                         Stand::at<int32_t>(stand.glyphs[0], 0x1C4) == 'R';
        if (installed || !untouched) {
            std::printf("FAIL: %s\n", c.what);
            ++g_failures;
        }
    }
}

}  // namespace

int main() {
    test_install_and_restore();
    test_restore_when_another_menu_is_up();
    test_release_keeps_the_label_and_returns_the_callback();
    test_a_new_panel_replaces_the_old();
    test_a_closed_menu_is_forgotten_unwritten();
    test_a_released_widget_is_left_alone();
    test_the_callback_goes_back_even_if_the_label_changed();
    test_mismatches_leave_the_menu_alone();
    if (g_failures == 0) std::printf("menu tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
