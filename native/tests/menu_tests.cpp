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
    check(choice.restore(a, &archipelago), "restore needs no panel up");
    choice.install(a, &archipelago);
    BYTE other[8] = {};
    stand.current = other;  // the file screen, say; the panel still exists
    check(!choice.install(a, &archipelago), "nothing to install on another menu");
    check(!choice.installed() && stand.text() == "RANDOMIZER ",
          "the hidden panel is put back as soon as it is not up");
    stand.current = stand.menu;
    check(choice.install(a, &archipelago) && stand.text() == "ARCHIPELAGO",
          "relabeled again when it is back up");
}

void test_a_new_panel_replaces_the_old() {
    Stand first, second;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = first.addresses();
    choice.install(a, &archipelago);
    first.current = second.menu;  // a new game-mode panel is up; the old one still exists
    check(choice.install(a, &archipelago), "installs on the new panel");
    check(first.text() == "RANDOMIZER " && second.text() == "ARCHIPELAGO",
          "the old panel is put back, the new one relabeled");
    choice.restore(a, &archipelago);
    check(second.text() == "RANDOMIZER ", "the new panel is put back");
}

void test_freed_choice_is_forgotten() {
    Stand stand;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = stand.addresses();
    choice.install(a, &archipelago);
    // The game freed the widget and reused the memory: it no longer holds the callback.
    Stand::at<void*>(stand.child, 0x2C0) = nullptr;
    std::memset(stand.glyphs[0] + 0x1C4, 0x55, 4);
    check(!choice.restore(a, &archipelago), "writes nothing into reused memory");
    check(Stand::at<int32_t>(stand.glyphs[0], 0x1C4) == 0x55555555, "left as it was");
    check(!choice.installed(), "forgets it");
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
    test_a_new_panel_replaces_the_old();
    test_freed_choice_is_forgotten();
    test_mismatches_leave_the_menu_alone();
    if (g_failures == 0) std::printf("menu tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
