// Host tests for the pause screen's disk row (pause.h), on a stand-in pause screen laid out as
// read live on B1: its KEYS panel a 4-glyph label and nine sprites.

#include <windows.h>

#include <cstring>
#include <new>
#include <string>

#include "check.h"
#include "pause.h"
#include "stand_in.h"  // g_keys_branch
#include "stand_in_menu.h"  // PauseScreen
#include "translation.h"

namespace {

int g_calls = 0;
void widget_handler() { g_calls += 1; }
void equipment_fn(void*, int) { g_calls += 2; }
void other_fn(void*, int) { g_calls += 3; }

unsigned char* const g_branch = stand_in::g_keys_branch;
unsigned char branch() { return code_byte(g_branch, 0); }

BYTE* const g_frames = stand_in::g_pause_frames;
BYTE* const g_disc_frame = stand_in::g_disc_frame;

struct Pause : stand_in::PauseScreen {
    Pause()
        : PauseScreen(reinterpret_cast<void*>(&equipment_fn), reinterpret_cast<void*>(&other_fn),
                      reinterpret_cast<void*>(&widget_handler)) {}

    zap::GameAddresses addresses() {
        zap::GameAddresses a;
        a.current_menu = reinterpret_cast<BYTE*>(&current);
        a.equipment_fn = reinterpret_cast<BYTE*>(&equipment_fn);
        a.widget_handler_fn = reinterpret_cast<BYTE*>(&widget_handler);
        a.sprite_templates = reinterpret_cast<BYTE*>(&templates_holder);
        a.sprite_sheet_fn = reinterpret_cast<BYTE*>(&sprite_sheet);
        a.keys_branch = g_branch;
        return a;
    }
};

void test_the_row_shows_the_disks() {
    Pause p;
    zap::PauseDiskRow row;
    zap::GameAddresses a = p.addresses();
    row.set(zap::pause_row_text(2, 0));
    row.watch(a, false);
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
    at<int32_t>(p.sprite[0], 0x1C4) = 'X';
    row.watch(a, false);
    check(p.frame(p.sprite[0]) == 'X', "turned once");

    row.set("");
    row.watch(a, false);
    check(branch() == 0x75 && !row.patched(), "off: the jne back");
}

// Before the client sets its count, the seed file's shows at once (no KEYS flash); the client's
// then wins, from the next pause screen.
void test_prepared_before_the_count() {
    Pause p;
    zap::PauseDiskRow row;
    zap::GameAddresses a = p.addresses();
    row.prepare(zap::pause_row_text(3, 0));
    row.watch(a, false);
    check(branch() == 0xEB && row.patched(), "prepared: the panel built");
    check(p.frame(p.sprite[4]) == 829 && p.frame(p.sprite[1]) == '3',
          "the file's count shown at once");
    row.set(zap::pause_row_text(4, 0));
    row.watch(a, false);
    check(p.frame(p.sprite[1]) == '3', "an open screen's row turned once");
    Pause q;
    row.watch(q.addresses(), false);
    check(q.frame(q.sprite[1]) == '4', "the next pause screen: the client's count");
    row.set("");
    row.prepare("");
    row.watch(a, false);
    check(branch() == 0x75 && !row.patched(), "neither: the jne back");
}

// The helper's 24 x 24 disc, once painted: sprite 4 pointed at the helper's copy of frame 829's
// entry, with the disc's place in the texture.
void test_the_helpers_disc() {
    Pause p;
    zap::PauseDiskRow row;
    zap::GameAddresses a = p.addresses();
    row.set(zap::pause_row_text(2, 15));
    row.watch(a, true);
    BYTE* s = p.sprite[4];
    const BYTE* entry = row.disc_entry();
    check(entry != nullptr && at<const BYTE*>(s, 0x1A8) == entry &&
              at<const BYTE*>(s, 0x1B0) == entry && at<int32_t>(s, 0x1BC) == 1 &&
              p.frame(s) == 0,
          "the sprite drawn from the helper's entry, its only frame");
    const float rect[4] = {128 / 2048.0f, 1008 / 2048.0f, 152 / 2048.0f, 1032 / 2048.0f};
    check(entry != nullptr && std::memcmp(entry, rect, sizeof(rect)) == 0,
          "the disc's place in the texture");
    check(entry != nullptr && std::memcmp(entry + 16, g_disc_frame + 16, 0x38 - 16) == 0,
          "the rest of the entry frame 829's");
    const double corners[8] = {-12, -12, 12, -12, 12, 12, -12, 12};
    check(std::memcmp(s + 0x98, corners, sizeof(corners)) == 0 && at<double>(s, 0x148) == -12 &&
              at<double>(s, 0x160) == 12,
          "a 24 x 24 quad about its place");
    check(at<double>(s, 0x40) == 160 && at<double>(s, 0x48) == 184 &&
              at<double>(s, 0x128) == 148 && at<double>(s, 0x130) == 172 &&
              at<double>(s, 0x138) == 172 && at<double>(s, 0x140) == 196 && s[0x120] == 1 &&
              at<float>(s, 0x68) == 1.0f && at<BYTE*>(s, 0x30) == p.font,
          "in the middle, above the text, white, its template kept");
    check(p.sprite[5][0x120] == 0 && p.sprite[8][0x120] == 0, "the other icons hidden");
    at<int32_t>(p.sprite[0], 0x1C4) = 'X';
    row.watch(a, true);
    check(p.frame(p.sprite[0]) == 'X', "turned once");

    // A new texture it could not paint into.
    row.watch(a, false);
    check(std::memcmp(entry, g_disc_frame, 16) == 0, "unpainted: frame 829's rectangle");
    row.watch(a, true);
    check(std::memcmp(entry, rect, sizeof(rect)) == 0, "painted again: the disc's");
    row.set("");
    row.watch(a, false);
    check(branch() == 0x75 && !row.patched(), "off: the jne back");
    check(at<const BYTE*>(s, 0x1B0) == entry && std::memcmp(entry, g_disc_frame, 16) == 0,
          "the entry kept, never freed, showing the game's disc");

    Pause q;
    row.set(zap::pause_row_text(3, 15));
    row.watch(q.addresses(), true);
    check(row.disc_entry() == entry && at<const BYTE*>(q.sprite[4], 0x1B0) == entry,
          "one entry for every pause screen");
    row.set("");
    row.watch(q.addresses(), false);
}

// The disc not painted, or a sprite without its template (whose list the game would free): the
// game's frame 829, 16 x 16; a sprite with no frame 829: no disc at all.
void test_the_games_disc_otherwise() {
    zap::PauseDiskRow row;
    {
        Pause p;
        row.set(zap::pause_row_text(2, 15));
        row.watch(p.addresses(), false);
        BYTE* s = p.sprite[4];
        check(p.frame(s) == 829 && at<BYTE*>(s, 0x1B0) == g_disc_frame &&
                  at<BYTE*>(s, 0x1A8) == g_frames && at<int32_t>(s, 0x1BC) == 912 &&
                  at<double>(s, 0x148) == -8 && at<double>(s, 0x98) == -8 &&
                  row.disc_entry() == nullptr,
              "not painted: frame 829, its own size, no entry made");
        row.watch(p.addresses(), true);
        check(p.frame(s) == 829, "a row turned with 829 stays so");
    }
    {
        Pause p;
        at<BYTE*>(p.sprite[4], 0x30) = nullptr;
        row.watch(p.addresses(), true);
        check(p.frame(p.sprite[4]) == 829 && at<BYTE*>(p.sprite[4], 0x1A8) == g_frames &&
                  row.disc_entry() == nullptr,
              "no template: frame 829, the game's list kept");
    }
    {
        Pause p;
        at<int32_t>(p.sprite[4], 0x1BC) = 829;  // frames 0 to 828 only
        row.watch(p.addresses(), true);
        check(p.frame(p.sprite[4]) == 384 && at<BYTE*>(p.sprite[4], 0x1A8) == g_frames &&
                  row.disc_entry() == nullptr,
              "a sprite without frame 829: neither disc");
    }
    row.set("");
    Pause p;
    row.watch(p.addresses(), false);
}

void test_anything_else_is_left_alone() {
    zap::PauseDiskRow row;
    struct Case {
        const char* what;
        void (*spoil)(Pause&, zap::PauseDiskRow&);
    };
    const Case cases[] = {
        {"not the pause screen (no EQUIPMENT)",
         [](Pause& p, zap::PauseDiskRow&) {
             at<void*>(p.entry[1], 0x2C0) = reinterpret_cast<void*>(&other_fn);
         }},
        {"not the KEYS row",
         [](Pause& p, zap::PauseDiskRow&) { at<int32_t>(p.glyph[0], 0x1C4) = 'Z'; }},
        {"another sprite count",
         [](Pause& p, zap::PauseDiskRow&) { at<int32_t>(p.row, 0x1F8) = 8; }},
        {"a text of another length (not the row's eight cells)",
         [](Pause&, zap::PauseDiskRow& r) { r.set("2 OF 15"); }},
    };
    for (const Case& c : cases) {
        Pause p;
        row.set(zap::pause_row_text(2, 15));
        c.spoil(p, row);
        const int glyph = p.frame(p.glyph[0]);
        row.watch(p.addresses(), false);
        check(p.frame(p.sprite[4]) == 384 && p.frame(p.glyph[0]) == glyph, c.what);
    }
    row.set("");
    Pause p;
    row.watch(p.addresses(), false);
    set_code_byte(g_branch, 0, 0x74);  // not the game's jne: never written
    row.set(zap::pause_row_text(2, 15));
    row.watch(p.addresses(), false);
    check(branch() == 0x74 && !row.patched(), "another instruction left alone");
    set_code_byte(g_branch, 0, 0x75);
    row.restore(p.addresses());
}

void test_the_text() {
    check(zap::pause_row_text(2, 0) == " 2 of ? ", "before the scanner");
    check(zap::pause_row_text(12, 15) == "12 of 15", "after");
    check(zap::pause_row_text(3, 9) == " 3 of 9 ", "centered");
}

// The pause menu's way back from OPTION MENU wrapped as dllmain.cpp wraps it.
zap::PauseDiskRow* g_row = nullptr;
const zap::GameAddresses* g_at = nullptr;
void options_back(void* menu) {
    reinterpret_cast<void (*)(void*)>(g_at->options_back_fn)(menu);
    g_row->returned(*g_at, static_cast<BYTE*>(menu), false);
}

void* on_return(Pause& p) { return at<void*>(p.menu, 0x68); }

// OPTION MENU opened over the pause screen, as its opener does: the pause menu's way back set to
// 0x168910 (0x16207D), its own menu on top.
struct Options {
    BYTE menu[0x80] = {};
    Pause& under;
    explicit Options(Pause& p) : under(p) {
        at<void*>(p.menu, 0x68) = reinterpret_cast<void*>(&stand_in::options_back_fn);
        at<BYTE*>(menu, 0x78) = p.menu;
        p.current = menu;
    }
    // Closed, as the game's menu-close code does (0x92B10 to 0x935C9): the pause menu on top
    // again, then its way back called with it. The game draws the frame after this.
    void close() {
        under.current = under.menu;
        reinterpret_cast<void (*)(void*)>(on_return(under))(under.menu);
    }
};

zap::GameAddresses hooked_addresses(Pause& p, zap::PauseDiskRow& row) {
    zap::GameAddresses a = p.addresses();
    a.options_back_fn = reinterpret_cast<BYTE*>(&stand_in::options_back_fn);
    a.hooks.options_back = reinterpret_cast<void*>(&options_back);
    g_row = &row;
    return a;
}

}  // namespace

// Back from OPTION MENU in English(HD), the game builds Keys again before it draws; the wrapped
// way back builds the count in the same call, so no frame shows Keys.
void test_back_from_options_in_a_translation() {
    stand_in::reset_translation();
    stand_in::load_translation("English(HD)", true);
    Pause p;
    zap::PauseDiskRow row;
    zap::GameAddresses a = hooked_addresses(p, row);
    g_at = &a;
    stand_in::with_translation(a);
    stand_in::HdGlyphs<12> hd(p.label);
    put<double>(p.glyph[0], 0x40, 144.0);
    put<double>(p.glyph[0], 0x48, 208.0);
    stand_in::built(p.label, 143.0, 207.0);
    put<int32_t>(p.label, 0x248, 4);  // the game's Keys
    row.set(zap::pause_row_text(2, 15));
    row.watch(a, false);
    check(get<int32_t>(p.label, 0x248) == 5 && !row.holds_hook(), "the count, before OPTION MENU");

    for (int round = 0; round < 3; ++round) {
        Options options(p);
        row.watch(a, false);  // a frame with OPTION MENU on top
        check(row.holds_hook() && on_return(p) == a.hooks.options_back,
              "the pause menu's way back is the helper's while OPTION MENU is open");
        row.watch(a, false);
        check(row.holds_hook() && on_return(p) == a.hooks.options_back, "and stays so");
        const int backs = stand_in::g_options_backs;
        options.close();
        // What the game draws next, before the helper's frame call.
        check(stand_in::g_options_backs == backs + 1, "the game's way back ran");
        check(get<int32_t>(p.label, 0x248) == 5 && !stand_in::g_builds.empty() &&
                  stand_in::g_builds.back().text == L"2 of 15" && hd.glyphs[0][0x120] == 1 &&
                  hd.glyphs[4][0x120] == 1 && !zap::pixel_glyphs_shown(p.label),
              "the frame drawn after OPTION MENU closes shows the count, not Keys");
        check(on_return(p) == reinterpret_cast<void*>(&stand_in::pause_on_return_fn) &&
                  !row.holds_hook(),
              "the game's way back set the pause menu's own: nothing left to put back");
        const size_t builds = stand_in::g_builds.size();
        row.watch(a, false);
        check(stand_in::g_builds.size() == builds && get<int32_t>(p.label, 0x248) == 5,
              "the next frame keeps it");
    }
    row.set("");
    row.watch(a, false);
    stand_in::reset_translation();
}

// In the default English the game's way back shows the four pixel glyphs again, which still hold
// the helper's characters: the row as it was.
void test_back_from_options_in_the_default_english() {
    stand_in::reset_translation();
    Pause p;
    zap::PauseDiskRow row;
    zap::GameAddresses a = hooked_addresses(p, row);
    g_at = &a;
    row.set(zap::pause_row_text(2, 0));
    row.watch(a, false);
    Options options(p);
    for (int i = 0; i < 4; ++i) p.glyph[i][0x120] = 0;  // hidden under OPTION MENU
    row.watch(a, false);
    check(row.holds_hook(), "wrapped in the default English too");
    options.close();
    const char* text = " 2 OF ? ";
    bool shown = true;
    for (int i = 0; i < 4; ++i) {
        shown = shown && p.frame(p.glyph[i]) == text[2 + i] && p.glyph[i][0x120] == 1;
    }
    for (int k = 0; k < 4; ++k) shown = shown && p.sprite[k][0x120] == 1;
    check(shown && p.frame(p.sprite[1]) == '2' && p.frame(p.sprite[4]) == 829 &&
              stand_in::g_builds.empty(),
          "the count on all eight cells, nothing built");
    row.set("");
    row.watch(a, false);
}

// The way back is the game's again when the row turns off and before an unload; a menu that
// closed is forgotten, never written.
void test_the_way_back_put_back() {
    stand_in::reset_translation();
    const void* game_fn = reinterpret_cast<void*>(&stand_in::options_back_fn);
    {
        Pause p;
        zap::PauseDiskRow row;
        zap::GameAddresses a = hooked_addresses(p, row);
        row.set(zap::pause_row_text(2, 0));
        Options options(p);
        row.watch(a, false);
        row.set("");
        row.watch(a, false);
        check(!row.holds_hook() && on_return(p) == game_fn, "the row off: the game's way back");
        row.set(zap::pause_row_text(2, 0));
        row.watch(a, false);
        check(row.holds_hook(), "on again: wrapped again");
        row.restore(a);
        check(!row.holds_hook() && on_return(p) == game_fn && branch() == 0x75,
              "before an unload: the game's way back, the branch back");
        row.watch(a, false);
        p.current = nullptr;  // every menu closed, the pause menu freed with them
        row.restore(a);
        check(!row.holds_hook() && on_return(p) == a.hooks.options_back,
              "a menu no longer open: forgotten, not written");
        p.current = options.menu;
        at<void*>(p.menu, 0x68) = const_cast<void*>(game_fn);
        row.watch(a, false);
        check(row.holds_hook(), "wrapped once more");
        at<void*>(p.menu, 0x68) = nullptr;
        row.watch(a, false);
        check(!row.holds_hook() && on_return(p) == nullptr, "no longer the wrapper: forgotten");
        row.set("");
        row.watch(a, false);
    }
    struct Case {
        const char* what;
        void (*spoil)(Pause&, zap::GameAddresses&, zap::PauseDiskRow&);
    };
    const Case cases[] = {
        {"a menu of another shape",
         [](Pause& p, zap::GameAddresses&, zap::PauseDiskRow&) { at<int32_t>(p.menu, 8) = 3; }},
        {"another way back",
         [](Pause& p, zap::GameAddresses&, zap::PauseDiskRow&) {
             at<void*>(p.menu, 0x68) = reinterpret_cast<void*>(&other_fn);
         }},
        {"no address for the game's way back",
         [](Pause&, zap::GameAddresses& a, zap::PauseDiskRow&) { a.options_back_fn = nullptr; }},
        {"the row off", [](Pause&, zap::GameAddresses&, zap::PauseDiskRow& r) { r.set(""); }},
    };
    for (const Case& c : cases) {
        Pause p;
        zap::PauseDiskRow row;
        zap::GameAddresses a = hooked_addresses(p, row);
        row.set(zap::pause_row_text(2, 0));
        Options options(p);
        c.spoil(p, a, row);
        void* before = on_return(p);
        row.watch(a, false);
        check(!row.holds_hook() && on_return(p) == before, c.what);
        row.set("");
        row.restore(a);
    }
}

// A fresh pause screen's label at the last one's place, with its mark, is the game's: the count
// is built on it.
void test_a_fresh_label_at_the_last_ones_place() {
    stand_in::reset_translation();
    stand_in::load_translation("English(HD)", true);
    zap::PauseDiskRow row;
    alignas(Pause) static unsigned char room[sizeof(Pause)];
    Pause* p = new (room) Pause();
    zap::GameAddresses a = p->addresses();
    stand_in::with_translation(a);
    auto set_up = [](Pause& q) {
        put<double>(q.glyph[0], 0x40, 144.0);
        put<double>(q.glyph[0], 0x48, 208.0);
        stand_in::built(q.label, 143.0, 207.0);
        put<int32_t>(q.label, 0x248, 4);
    };
    {
        stand_in::HdGlyphs<12> hd(p->label);
        set_up(*p);
        row.set(zap::pause_row_text(2, 15));
        row.watch(a, false);
        // Two calls a build: one to measure it for centering, one at its place.
        check(stand_in::g_builds.size() == 2, "the first screen's count built");
        const int32_t count = get<int32_t>(p->label, 0x248);
        const double x = get<double>(p->label, 0x258), y = get<double>(p->label, 0x260);
        // The next pause screen at the same place, its label's mark by chance the helper's.
        p->~Pause();
        p = new (room) Pause();
        stand_in::HdGlyphs<12> hd2(p->label);
        set_up(*p);
        put<int32_t>(p->label, 0x248, count);
        put<double>(p->label, 0x258, x);
        put<double>(p->label, 0x260, y);
        row.watch(a, false);
        check(stand_in::g_builds.size() == 4 && stand_in::g_builds.back().text == L"2 of 15",
              "the next screen's count built too");
        row.set("");
        row.watch(a, false);
    }
    p->~Pause();
    stand_in::reset_translation();
}

// In a translation the count is the KEYS label's translated text, centered on the row (x 160),
// the key slots' characters hidden; with the flag clear, the label left as it is.
void test_the_row_in_a_translation() {
    for (bool on : {true, false}) {
        stand_in::reset_translation();
        stand_in::load_translation("English(HD)", on);
        Pause p;
        zap::PauseDiskRow row;
        zap::GameAddresses a = p.addresses();
        stand_in::with_translation(a);
        stand_in::HdGlyphs<12> hd(p.label);
        put<double>(p.glyph[0], 0x40, 144.0);
        put<double>(p.glyph[0], 0x48, 208.0);
        stand_in::built(p.label, 143.0, 207.0);
        put<int32_t>(p.label, 0x248, 4);  // the game's Keys
        row.set(zap::pause_row_text(2, 15));
        row.watch(a, false);
        auto slots_shown = [&] {
            for (int k = 0; k < 4; ++k) {
                if (p.sprite[k][0x120] == 0) return false;
            }
            return true;
        };
        if (on) {
            const int32_t n = get<int32_t>(p.label, 0x248);
            const double middle = (hd.x(0) + hd.x(n - 1) + stand_in::kHdWidth) / 2;
            check(!stand_in::g_builds.empty() && stand_in::g_builds.back().text == L"2 of 15" &&
                      n == 5 && middle == 160.0 && !zap::pixel_glyphs_shown(p.label) &&
                      p.glyph[0][0x120] == 0,
                  "the count in the translation's font, centered on the row");
            check(!slots_shown(), "the key slots' characters hidden");
            const size_t builds = stand_in::g_builds.size();
            row.watch(a, false);
            check(stand_in::g_builds.size() == builds, "kept while it is the helper's");
            stand_in::built(p.label, 143.0, 207.0);  // built again on OPTION MENU's return
            put<int32_t>(p.label, 0x248, 4);
            row.watch(a, false);
            check(stand_in::g_builds.size() == builds + 2 &&
                      stand_in::g_builds.back().text == L"2 of 15",
                  "and again after the game's");
        } else {
            check(stand_in::g_builds.empty() && slots_shown(),
                  "the flag clear: nothing built, the key slots shown");
        }
        row.set("");
        row.watch(a, false);
    }
    stand_in::reset_translation();
}

int main() {
    test_back_from_options_in_a_translation();
    test_back_from_options_in_the_default_english();
    test_the_way_back_put_back();
    test_a_fresh_label_at_the_last_ones_place();
    test_the_row_in_a_translation();
    test_the_row_shows_the_disks();
    test_prepared_before_the_count();
    test_the_helpers_disc();
    test_the_games_disc_otherwise();
    test_anything_else_is_left_alone();
    test_the_text();
    return report("pause tests");
}
