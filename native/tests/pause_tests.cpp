// Host tests for the pause screen's disk row (pause.h): a stand-in pause screen laid out as read
// live on B1 (its KEYS panel: a 4-glyph label and nine sprites), the stand-in game's `jne` in
// code, and check.h's stand-in for the game's sprite-sheet function.

#include <windows.h>

#include <cstring>
#include <string>

#include "check.h"
#include "pause.h"
#include "stand_in.h"  // g_keys_branch
#include "stand_in_menu.h"  // PauseScreen

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
    // Turned once: a second look changes nothing.
    at<int32_t>(p.sprite[0], 0x1C4) = 'X';
    row.watch(a, false);
    check(p.frame(p.sprite[0]) == 'X', "turned once");

    row.set("");
    row.watch(a, false);
    check(branch() == 0x75 && !row.patched(), "off: the jne back");
}

// A disk seed's save loaded before the client sets its count: the seed file's count shows at
// once (no KEYS flash); the client's count then wins, from the next pause screen.
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

// The helper's 24 x 24 disc, once painted: sprite 4 pointed at the helper's one entry, a copy of
// frame 829's with the disc's place in the texture, drawn 24 x 24 about the icon's place.
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
    // Turned once: a second look changes nothing.
    at<int32_t>(p.sprite[0], 0x1C4) = 'X';
    row.watch(a, true);
    check(p.frame(p.sprite[0]) == 'X', "turned once");

    // The image not painted any more (a new texture it could not paint into): the entry shows
    // the game's disc.
    row.watch(a, false);
    check(std::memcmp(entry, g_disc_frame, 16) == 0, "unpainted: frame 829's rectangle");
    row.watch(a, true);
    check(std::memcmp(entry, rect, sizeof(rect)) == 0, "painted again: the disc's");
    // Off (and before an unload): the entry kept, showing frame 829's rectangle.
    row.set("");
    row.watch(a, false);
    check(branch() == 0x75 && !row.patched(), "off: the jne back");
    check(at<const BYTE*>(s, 0x1B0) == entry && std::memcmp(entry, g_disc_frame, 16) == 0,
          "the entry kept, never freed, showing the game's disc");

    // The next pause screen reuses the one entry.
    Pause q;
    row.set(zap::pause_row_text(3, 15));
    row.watch(q.addresses(), true);
    check(row.disc_entry() == entry && at<const BYTE*>(q.sprite[4], 0x1B0) == entry,
          "one entry for every pause screen");
    row.set("");
    row.watch(q.addresses(), false);
}

// The disc not painted, or a sprite without its template (whose list the game would free): the
// game's frame 829, 16 x 16, as before; a sprite with no frame 829: no disc at all.
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
    check(zap::pause_row_text(2, 0) == " 2 OF ? ", "before the scanner");
    check(zap::pause_row_text(12, 15) == "12 OF 15", "after");
    check(zap::pause_row_text(3, 9) == " 3 OF 9 ", "centered");
}

}  // namespace

int main() {
    test_the_row_shows_the_disks();
    test_prepared_before_the_count();
    test_the_helpers_disc();
    test_the_games_disc_otherwise();
    test_anything_else_is_left_alone();
    test_the_text();
    return report("pause tests");
}
