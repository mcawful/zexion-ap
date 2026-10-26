// Host tests for the ARCHIPELAGO choice (menu.h): a stand-in game-mode panel laid out as traced
// on B1, relabeled, put back, and left alone whenever anything on the way does not match.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "menu.h"
#include "stand_in_menu.h"

namespace {

using stand_in::kFrames;

int g_calls = 0;
// Bodies of their own: the linker folds identical functions into one address.
void panel_handler() { g_calls += 100; }
void widget_handler() { g_calls += 200; }
void rando_menu(void*, int) { g_calls += 300; }
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

void test_the_border_under_the_mouse_fits_archipelago() {
    Stand stand;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = stand.addresses();
    // The panel's border as the game lays it out round a label at x = 160: eight 8 x 8 pieces,
    // the top and bottom edges 80.1 long, the right-hand pieces at 240.1.
    BYTE pieces[8][0x160] = {};
    auto at_d = [](BYTE* p, ptrdiff_t o) -> double& { return *reinterpret_cast<double*>(p + o); };
    const double xs[8] = {152, 160, 240.1, 240.1, 240.1, 160, 152, 152};
    auto lay_out = [&] {
        for (int i = 0; i < 8; ++i) {
            *reinterpret_cast<BYTE**>(stand.panel + 0x278 + 8 * i) = pieces[i];
            at_d(pieces[i], 0x40) = xs[i];
            at_d(pieces[i], 0x128) = xs[i];
            at_d(pieces[i], 0x138) = xs[i] + (i == 1 || i == 5 ? 80.1 : 8.0);
            *reinterpret_cast<float*>(pieces[i] + 0x114) = i == 1 || i == 5 ? 80.1f : 8.0f;
            at_d(pieces[i], 0x158) = i == 1 || i == 5 ? 80.1 : 8.0;
            at_d(pieces[i], 0xA8) = at_d(pieces[i], 0xB8) = i == 1 || i == 5 ? 80.1 : 8.0;
        }
    };
    lay_out();
    *reinterpret_cast<int32_t*>(stand.panel + 0x2B8) = 96;
    *reinterpret_cast<int32_t*>(stand.panel + 0x2DC) = -1;  // the mouse on nothing
    at_d(stand.label, 0x40) = 160;
    choice.install(a, &archipelago);
    check(!choice.fit_border(a) && at_d(pieces[2], 0x40) == 240.1, "no border moved with the mouse elsewhere");
    *reinterpret_cast<int32_t*>(stand.panel + 0x2DC) = 1;  // on ARCHIPELAGO
    check(choice.fit_border(a), "the border fitted under the mouse");
    check(at_d(pieces[2], 0x40) == 248.1 && at_d(pieces[3], 0x128) == 248.1 &&
              at_d(pieces[4], 0x138) == 256.1 && at_d(pieces[0], 0x40) == 152,
          "its right-hand pieces 8 to the right, the left-hand ones where they were");
    check(*reinterpret_cast<float*>(pieces[1] + 0x114) == 88.1f && at_d(pieces[5], 0x158) == 88.1 &&
              at_d(pieces[1], 0x138) == 248.1 && at_d(pieces[1], 0xA8) == 88.1 &&
              at_d(pieces[5], 0xB8) == 88.1 && at_d(pieces[0], 0xA8) == 8.0,
          "its top and bottom edges 8 longer");
    check(!choice.fit_border(a) && at_d(pieces[2], 0x40) == 248.1, "and only once");
    choice.restore(a, &archipelago);

    // Anything else about the panel, and nothing is moved.
    auto fitted = [&] { return at_d(pieces[2], 0x40) == 248.1 && at_d(pieces[1], 0x158) == 88.1; };
    lay_out();  // the next hover
    choice.install(a, &archipelago);
    *reinterpret_cast<int32_t*>(stand.panel + 0x2B8) = 104;  // a width from measured text
    check(!choice.fit_border(a) && at_d(pieces[2], 0x40) == 240.1, "another width: untouched");
    *reinterpret_cast<int32_t*>(stand.panel + 0x2B8) = 96;
    *reinterpret_cast<BYTE**>(stand.panel + 0x278 + 8 * 7) = nullptr;
    check(!choice.fit_border(a) && at_d(pieces[2], 0x40) == 240.1, "a piece missing: untouched");
    *reinterpret_cast<BYTE**>(stand.panel + 0x278 + 8 * 7) = pieces[7];
    BYTE other_child[0x300] = {};
    stand.children[1] = other_child;
    check(!choice.fit_border(a) && at_d(pieces[2], 0x40) == 240.1,
          "another widget in ARCHIPELAGO's place: untouched");
    stand.children[1] = stand.child;
    check(choice.fit_border(a) && fitted(), "and fitted once all is as the game lays it out");
    choice.restore(a, &archipelago);
}

int g_hook_calls = 0;
void panel_hook() { g_hook_calls += 3; }  // a body of its own: the linker folds identical ones
void other_handler() { g_hook_calls += 5; }

void test_the_panel_handler_is_the_helpers_while_installed() {
    Stand stand;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = stand.addresses();
    a.panel_hook = reinterpret_cast<void*>(&panel_hook);
    void*& handler = *reinterpret_cast<void**>(stand.panel + 0x2F8);
    check(choice.install(a, &archipelago) && handler == a.panel_hook,
          "installing makes the panel's handler the helper's");
    check(!choice.install(a, &archipelago), "and the panel is still taken for the game-mode one");
    check(choice.restore(a, &archipelago) && handler == a.panel_handler,
          "restoring puts the game's handler back");
    choice.install(a, &archipelago);
    check(choice.release(a, &archipelago) && handler == a.panel_handler,
          "and so does releasing after ARCHIPELAGO was chosen");
    handler = reinterpret_cast<void*>(&other_handler);  // a handler that is neither
    check(!choice.install(a, &archipelago) && handler == reinterpret_cast<void*>(&other_handler),
          "another handler: the panel is not taken, nothing written");
}

void test_the_panel_handler_goes_back_whatever_became_of_the_choice() {
    Stand stand;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = stand.addresses();
    a.panel_hook = reinterpret_cast<void*>(&panel_hook);
    void*& handler = *reinterpret_cast<void**>(stand.panel + 0x2F8);
    choice.install(a, &archipelago);
    // The choice no longer looks like the one relabeled: its callback is not the helper's.
    *reinterpret_cast<void**>(stand.child + 0x2C0) = reinterpret_cast<void*>(&rando_menu);
    check(!choice.restore(a, &archipelago), "the choice is not written");
    check(handler == a.panel_handler && !choice.holds_hook(),
          "but the game's handler is back on the panel");

    // Released likewise.
    *reinterpret_cast<void**>(stand.child + 0x2C0) = reinterpret_cast<void*>(&rando_menu);
    choice.install(a, &archipelago);
    *reinterpret_cast<void**>(stand.child + 0x2C0) = reinterpret_cast<void*>(&rando_menu);
    check(!choice.release(a, &archipelago) && handler == a.panel_handler && !choice.holds_hook(),
          "released: the game's handler back too");

    // A menu closed: freed with its panel, which is not written; the hook is forgotten.
    choice.install(a, &archipelago);
    BYTE file_screen[0x80] = {};
    stand.current = file_screen;
    check(!choice.restore(a, &archipelago) && handler == a.panel_hook && !choice.holds_hook(),
          "a closed menu's panel is left as it is, and forgotten");

    // Another panel in the open menu's place: the old one is not known to be alive, so the hook
    // stays remembered (and the helper loaded) until the menu closes.
    stand.current = stand.menu;
    handler = a.panel_handler;
    *reinterpret_cast<void**>(stand.child + 0x2C0) = reinterpret_cast<void*>(&rando_menu);
    choice.install(a, &archipelago);
    BYTE replacement[0x300] = {};
    stand.panels[3] = replacement;
    choice.restore(a, &archipelago);
    check(choice.holds_hook() && handler == a.panel_hook, "a panel swapped out: still held");
    stand.panels[3] = stand.panel;
    choice.restore(a, &archipelago);
    check(!choice.holds_hook() && handler == a.panel_handler, "put back once it is in place again");
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

void test_a_label_left_as_archipelago_is_not_taken_for_the_original() {
    Stand stand;
    zap::ArchipelagoChoice choice;
    zap::MenuAddresses a = stand.addresses();
    choice.install(a, &archipelago);
    choice.release(a, &archipelago);  // the menu outlived the new game, against expectation
    check(choice.install(a, &archipelago), "offered again on the same panel");
    check(choice.restore(a, &archipelago) && stand.text() == "RANDOMIZER ",
          "putting it back writes RANDOMIZER, not the ARCHIPELAGO it found");
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

void assist_menu(void*, int) {}
struct Options : stand_in::OptionsMenu {
    Options()
        : stand_in::OptionsMenu(reinterpret_cast<void*>(&widget_handler),
                                reinterpret_cast<void*>(&assist_menu)) {}
};

void test_assist_lock_and_unlock() {
    Options options;
    zap::AssistLock lock;
    zap::MenuAddresses a = options.addresses();
    check(lock.install(a), "locks ASSIST in the options menu's second panel");
    check(options.callback() == nullptr &&
              options.text() == "ASSIST" && options.code(6) == 0 &&
              options.code(7) == zap::kLockGlyph && options.style() == 5,
          "ASSIST, a blank and the padlock, in style 5, its callback cleared");
    check(options.palette().first == options.palette_objects[0] && options.palette().second == 0,
          "under palette 0, whatever the menu's own");
    check(Options::at<float>(options.glyphs[0], 0x68) < 0.5f &&
              Options::at<float>(options.glyphs[11], 0x80) < 0.5f &&
              Options::at<float>(options.glyphs[0], 0x74) == 1.0f,
          "tinted grey, its alpha left alone");
    check(!lock.install(a), "locks once");
    check(lock.restore(a), "unlocks");
    check(options.callback() == reinterpret_cast<void*>(&assist_menu) &&
              options.text() == "ASSIST" && options.code(7) == 0 && options.style() == 0x15,
          "the game's callback, label and style are back");
    check(options.palette().first == options.palette_objects[1] && options.palette().second == 26,
          "and the menu's own palette");
    check(Options::at<float>(options.glyphs[0], 0x68) == 1.0f, "and its own tint");
    check(!lock.installed() && !lock.restore(a), "unlocks once");
}

void test_assist_lock_moves_assist_past_the_selectable_entries() {
    Options options;
    zap::AssistLock lock;
    zap::MenuAddresses a = options.addresses();
    BYTE* back = options.children[7];
    Options::at<int32_t>(options.second, 0x2DC) = 7;  // the cursor on BACK
    lock.install(a);
    check(Options::at<int32_t>(options.second, 0x1EC) == 7 && options.children[6] == back &&
              options.children[7] == options.child &&
              Options::at<int32_t>(back, 0x1E0) == 6 &&
              Options::at<int32_t>(options.child, 0x1E0) == 7,
          "ASSIST moved past the selectable entries, BACK up one, each renumbered");
    check(Options::at<int32_t>(options.second, 0x2DC) == 6, "the cursor stays on BACK");
    lock.restore(a);
    check(Options::at<int32_t>(options.second, 0x1EC) == 8 && options.children[6] == options.child &&
              options.children[7] == back && Options::at<int32_t>(back, 0x1E0) == 7 &&
              Options::at<int32_t>(options.child, 0x1E0) == 6 &&
              Options::at<int32_t>(options.second, 0x2DC) == 7,
          "and all put back, the cursor still on BACK");
}

void test_assist_lock_leaves_an_unexpected_order_alone() {
    Options options;
    zap::AssistLock lock;
    zap::MenuAddresses a = options.addresses();
    Options::at<int32_t>(options.children[7], 0x1E0) = 3;  // not numbered by its place
    lock.install(a);
    check(lock.installed() && options.callback() == nullptr &&
              Options::at<int32_t>(options.second, 0x1EC) == 8 && options.children[6] == options.child,
          "locked, but not moved");
    lock.restore(a);
    check(options.children[6] == options.child && Options::at<int32_t>(options.second, 0x1EC) == 8,
          "and nothing moved back");
}

void test_assist_lock_follows_the_menu() {
    Options options;
    zap::AssistLock lock;
    zap::MenuAddresses a = options.addresses();
    lock.install(a);
    BYTE above[0x80] = {};
    Options::at<BYTE*>(above, 0x78) = options.menu;  // VIDEO, say, opened on top
    options.current = above;
    check(!lock.install(a) && lock.installed() && options.callback() == nullptr,
          "another menu on top: still locked while the options menu is open below");
    options.current = options.menu;
    for (BYTE* g : options.glyph_list) {  // the game redraws, in another palette
        Options::at<int32_t>(g, 0x1D0) = 0x15;
        Options::at<void*>(g, 0x198) = options.palette_objects[1];
        Options::at<int16_t>(g, 0x1A0) = 26;
    }
    check(!lock.install(a) && options.style() == 5 && options.code(7) == zap::kLockGlyph &&
              options.palette().second == 0,
          "back on top: the locked look is drawn again over the game's restyle");
    options.current = nullptr;  // the options menu closed and freed
    check(!lock.install(a) && !lock.installed(), "a closed menu is forgotten");
    check(options.callback() == nullptr,
          "unwritten: the freed widget is left as it was");
}

void test_assist_lock_comes_off_even_if_the_label_changed() {
    Options options;
    zap::AssistLock lock;
    zap::MenuAddresses a = options.addresses();
    lock.install(a);
    Options::at<int32_t>(options.glyphs[0], 0x1C4) = 'Z';  // relabeled while locked
    check(!lock.install(a) && lock.installed(),
          "still locked: found by its pointer, not its label");
    check(lock.restore(a), "unlocks");
    check(options.callback() == reinterpret_cast<void*>(&assist_menu),
          "the game's callback is back whatever the label says");
}

void test_assist_lock_leaves_anything_else_alone() {
    {
        Options options;
        zap::AssistLock lock;
        Options::at<int32_t>(options.glyphs[5], 0x1C4) = 'X';  // ASSISX: not the entry
        check(!lock.install(options.addresses()), "another label: left alone");
    }
    {
        Options options;
        zap::AssistLock lock;
        Options::at<int32_t>(options.child, 0x1C8) = 2;  // released
        check(!lock.install(options.addresses()), "a released widget");
    }
    {
        Options options;
        zap::AssistLock lock;
        zap::MenuAddresses a = options.addresses();
        a.assist_menu = nullptr;  // not configured
        check(!lock.install(a), "no ASSIST callback known: nothing found");
    }
    {
        Options options;
        zap::AssistLock lock;
        Options::at<int32_t>(options.menu, 8) = 40;  // not a panel count
        check(!lock.install(options.addresses()), "a panel count out of range");
    }
    {
        Options options;
        zap::AssistLock lock;
        Options::at<void*>(options.child, 0x2C8) = nullptr;  // another handler
        check(!lock.install(options.addresses()), "another widget handler");
        check(options.text() == "ASSIST", "and its label untouched");
    }
}

int main() {
    test_the_panel_handler_is_the_helpers_while_installed();
    test_the_panel_handler_goes_back_whatever_became_of_the_choice();
    test_the_border_under_the_mouse_fits_archipelago();
    test_assist_lock_and_unlock();
    test_assist_lock_follows_the_menu();
    test_assist_lock_leaves_anything_else_alone();
    test_assist_lock_comes_off_even_if_the_label_changed();
    test_assist_lock_moves_assist_past_the_selectable_entries();
    test_assist_lock_leaves_an_unexpected_order_alone();
    test_install_and_restore();
    test_restore_when_another_menu_is_up();
    test_release_keeps_the_label_and_returns_the_callback();
    test_a_label_left_as_archipelago_is_not_taken_for_the_original();
    test_a_new_panel_replaces_the_old();
    test_a_closed_menu_is_forgotten_unwritten();
    test_a_released_widget_is_left_alone();
    test_the_callback_goes_back_even_if_the_label_changed();
    test_mismatches_leave_the_menu_alone();
    if (g_failures == 0) std::printf("menu tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
