// Host tests for translation.h on a stand-in translation (stand_in.h: English(HD)'s tables as
// the parser leaves them): the text helpers, the tables' entries, the received items' popups in
// the translation's font (the two areas taking turns, waiting, emptying), the names written and
// put back, a language change between a write and its put-back, the menu labels built again and
// back, the pixel-glyph swap, and the default English left as it was.

#include <windows.h>

#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "fake_gl.h"
#include "game_calls.h"
#include "menu.h"
#include "offline.h"
#include "requests.h"
#include "stand_in.h"
#include "stand_in_menu.h"
#include "translation.h"

using namespace stand_in;

namespace {

void no_callback(void*, int) {}

// The stand-in game configured by a client, English(HD) loaded and on, a save in play, and the
// message routine numbering its messages as the game does.
void start(zap::Game& game, bool on = true) {
    reset_game();
    reset_translation();
    reset_texts();
    load_translation("English(HD)", on);
    g_slots_live = true;
    std::memset(g_slots, 0, sizeof(g_slots));
    check(configure(game).empty(), "configured");
}

void queue(zap::Game& game, const char* request) {
    check(apply(game, request) == "applied", "the batch applied", request);
}

// Frames of the helper's: its watches, then its popups, as the frame call runs them.
void frames(zap::Game& game, int n) {
    for (int i = 0; i < n; ++i) {
        game.watch_all(&no_callback);
        game.pump_popups();
    }
}

BYTE* slot(int i) { return g_slots + 0x1628 * i; }
BYTE* hd_object(int i) { return g_hd_messages + 0x238 * i; }
// A message's end, as the game's update reaches 0 frames left: its serial cleared.
void end_message(int i) { put<int32_t>(slot(i), 0x1D8, 0); }

void test_the_text_helpers() {
    check(zap::hd_text("A {B} C\xEE") == L"A (B) C", "braces as parentheses, others left out");
    check(zap::hd_icon(0xC0) == L"\xE024" && zap::hd_icon(0xEA) == L"\xE04E" &&
              zap::hd_icon(0x6E) == L"\xE04F" && zap::hd_icon(0x77) == L"\xE050",
          "the icons as the pickup maps them");
    check(zap::hd_icon(0) == L"" && zap::hd_icon(0xEB) == L"" && zap::hd_icon(0x41) == L"",
          "none, and no icon");
    check(zap::hd_icon(0xFB) == L"\xE03A" && zap::hd_icon(0xEF) == L"\xE03B" &&
              zap::hd_icon(0xEE) == L"\xE03C",
          "the disk, the padlock and the AP logo as the helper paints them in the icon sheets");
    // As layout_label lays a label out: "\xEE" "MASTER SWORD" over FOR ALICE, 13 wide.
    const zap::Label label = zap::layout_label("\xEE" "MASTER SWORD", "FOR ALICE");
    check(zap::hd_label(label.name, label.width) == L"\xE03C" L"MASTER SWORD\nFOR ALICE",
          "a label's lines, the logo first");
    check(zap::hd_label(label.name, label.width, true) == L"\xE03C" L"MASTER SWORD",
          "its first line");
    check(zap::hd_label(label.name, label.width, true, false) == L"MASTER SWORD",
          "the map cursor's: no logo");
    const zap::Label own = zap::layout_label("\xC5" "MISSILE BOX", "FOR BOB");
    check(zap::hd_label(own.name, own.width) == L"\xE029MISSILE BOX\nFOR BOB",
          "another Zexion player's item with its icon");
}

void test_the_tables() {
    zap::Game game;
    start(game);
    zap::GameAddresses a;
    with_translation(a);
    check(zap::translation_keys(a) && zap::translation_on(a), "the keys, the flag");
    auto now = zap::loaded_translation(a);
    check(now && now->on && now->small.count == 448 && now->items.count == 51,
          "the translation read");
    check(game.language() == "English(HD)", "hello's language");
    const int32_t own = g_items->offsets[47], next = g_items->offsets[48];
    check(zap::entry_room(now->items, 47) == next - own - 1, "an entry's room: to the next's");
    check(zap::entry_room(now->items, 50) == 0, "the last entry: none known");
    std::wstring text;
    check(zap::read_text(now->items, own, 64, text) && text == L"Scrap Energy", "read");
    check(zap::read_entry(now->items, 50, text, 64) && text == L"Energy Shield" &&
              !zap::read_entry(now->items, 50, text),
          "the last entry read within the room given for it, and not without one");
    check(!zap::write_text(now->items, own, 5, L"TOO LONG"), "too long for its room: refused");
    load_translation("Brazilian Portuguese");
    check(game.language() == "Brazilian_Portuguese", "a name with a space, as one word");
    g_translation[0x91] = 0;
    check(game.language() == "-", "the default English");
    // A layout that differs: the translation parts stand down.
    a.small_text = g_translation + 0x38;
    check(!zap::translation_keys(a) && !zap::loaded_translation(a), "a layout not as traced");
}

void test_popups_take_turns_and_empty() {
    zap::Game game;
    start(game);
    const int32_t run = g_small->offsets[66], seventy = g_small->offsets[70];
    queue(game, "apply\npopup 36 FROM ALICE");
    frames(game, 1);
    check(g_messages == 1 && g_hd_texts.size() == 1, "a popup, given its translated text");
    const std::wstring first = L"\xE029Missile Box\nfrom ALICE";
    check(!g_hd_texts.empty() && g_hd_texts[0].entry == 66 && g_hd_texts[0].kind == 0 &&
              g_hd_texts[0].text == first && g_hd_texts[0].entry_then == first,
          "the icon and the translation's name, the suffix under it, in entry 66 before the call");
    check(g_small->offsets[66] == run && g_small->offsets[70] > run &&
              g_small->offsets[70] != seventy && game.popup_areas().split(),
          "the run split: entry 70 moved to its middle");
    check(hd_object(0)[0x22C] == 1, "the message's HD object marked, as the game marks it");

    // The next while the first still closes: the other area.
    g_message_slot = 1;
    queue(game, "apply\npopup 0 FROM BOB");
    frames(game, 25);
    check(g_hd_texts.size() == 2 && g_hd_texts[1].entry == 70 &&
              g_hd_texts[1].text == L"Energy Tank\nfrom BOB" &&
              entry_text(g_small, 70) == L"Energy Tank\nfrom BOB" &&
              entry_text(g_small, 66) == first,
          "the second in entry 70, the first's kept");
    check(game.unload_blocker() != nullptr, "an unload waits while one is read");

    // Both taken: the third waits, no message made.
    g_message_slot = 2;
    queue(game, "apply\npopup 36 FROM CAROL");
    const int made = g_messages;
    frames(game, 25);
    check(g_messages == made && game.queued_popups() == 1, "the third waits for an area");

    // The first ends: its area emptied, the third takes it.
    end_message(0);
    frames(game, 1);
    check(g_hd_texts.size() == 3 && g_hd_texts[2].entry == 66, "the third in the area freed");
    // The second's object given a translated message of the game's (GAME SAVED in its slot).
    put<void*>(hd_object(1), 0x220, slot(1));  // the slot itself, as the game binds it
    put<int32_t>(hd_object(1), 0x228, 7);  // and its message's serial
    put<int32_t>(hd_object(1), 0x20C, 98);
    frames(game, 1);
    check(entry_text(g_small, 70).empty(), "the second's area emptied once its object was taken");
    end_message(2);
    frames(game, 1);
    check(entry_text(g_small, 66).empty() && game.popup_areas().in_use() == false,
          "every area empty once its message has gone");
    check(game.unload_blocker() != nullptr, "the split: an unload no frame came for waits");

    // Put back: entry 70 at its own place again.
    game.put_back_all(&no_callback);
    check(g_small->offsets[70] == seventy && !game.popup_areas().split(), "the split undone");
    check(game.unload_blocker() == nullptr, "nothing for an unload to wait for");
}

void test_a_notice_too_long_shows_its_pixel_text() {
    zap::Game game;
    start(game);
    // The game's GAME SAVED left slot 0 marked (docs/re/memory.md, "Translated messages").
    // As GAME SAVED leaves slot 0's object: bound to the slot itself, an older serial.
    put<void*>(hd_object(0), 0x220, slot(0));
    put<int32_t>(hd_object(0), 0x228, 7);
    hd_object(0)[0x22C] = 1;
    std::string notice = "apply\nnotice 44";
    for (int i = 0; i < 38; ++i) notice += " WORDS";
    queue(game, notice.c_str());
    frames(game, 1);
    check(g_messages == 1 && g_hd_texts.empty(), "past the run: no translated text");
    check(hd_object(0)[0x22C] == 0, "the slot's mark cleared, so its pixel text shows");
    check(entry_text(g_small, 66) == L"\r", "nothing written");
    game.put_back_all(&no_callback);
}

// With the flag set but the tables not readable (here: no ItemNames loaded), and after an
// unload's put-back: the pixel text, the slot's mark cleared, nothing borrowed.
void test_the_pixel_text_when_the_tables_cannot_be_used() {
    zap::Game game;
    start(game);
    put<wchar_t*>(g_translation, 0x48, nullptr);
    // As GAME SAVED leaves slot 0's object: bound to the slot itself, an older serial.
    put<void*>(hd_object(0), 0x220, slot(0));
    put<int32_t>(hd_object(0), 0x228, 7);
    hd_object(0)[0x22C] = 1;
    queue(game, "apply\npopup 36 FROM ALICE");
    frames(game, 1);
    check(g_messages == 1 && g_hd_texts.empty() && hd_object(0)[0x22C] == 0,
          "tables not readable: the pixel text, the mark cleared");
    put<wchar_t*>(g_translation, 0x48, g_items->text.data());

    game.put_back_all(&no_callback);
    game.hold_translated_popups();
    hd_object(0)[0x22C] = 1;
    queue(game, "apply\npopup 36 FROM BOB");
    frames(game, 25);
    check(g_messages == 2 && g_hd_texts.empty() && hd_object(0)[0x22C] == 0 &&
              !game.popup_areas().split() && game.unload_blocker() == nullptr,
          "while unloading: the pixel text, no entry borrowed");
    game.rearm(game.snapshot());  // the unload refused
    queue(game, "apply\npopup 36 FROM CAROL");
    frames(game, 25);
    check(g_hd_texts.size() == 1, "refused: the translation's font again");
    game.put_back_all(&no_callback);
}

// A translation whose borrowed run holds text (here entry 68): no split, nothing written, and
// the popup in its pixel text (PopupAreas::prepare takes the run only with every entry empty).
void test_the_run_not_borrowed_when_an_entry_holds_text() {
    zap::Game game;
    start(game);
    std::map<int, std::wstring> texts = hd_small_texts();
    texts[68] = L"Taken";
    g_small = make_table(448, texts);
    put<wchar_t*>(g_translation, 0x30, g_small->text.data());
    put<int32_t*>(g_translation, 0x58, g_small->offsets.data());
    const std::vector<wchar_t> text = g_small->text;
    const std::vector<int32_t> offsets = g_small->offsets;
    queue(game, "apply\npopup 36 FROM ALICE");
    frames(game, 1);
    check(g_messages == 1 && g_hd_texts.empty(), "the popup in its pixel text");
    check(!game.popup_areas().split() && g_small->offsets == offsets && g_small->text == text,
          "no split, the table as it was");
    game.put_back_all(&no_callback);
}

void test_a_whole_notice() {
    zap::Game game;
    start(game);
    // Longer than an area, within the run: both free, the whole run.
    std::string notice = "apply\nnotice 44 HINT:";
    for (int i = 0; i < 16; ++i) notice += " MORE";
    queue(game, notice.c_str());
    frames(game, 1);
    check(g_hd_texts.size() == 1 && g_hd_texts[0].entry == 66 &&
              g_hd_texts[0].text.size() > 80,
          "the whole run, by entry 66");
    check(game.popup_areas().in_use(), "both taken");
    // A popup while the notice shows: it waits, the notice's text left whole.
    const std::wstring shown = entry_text(g_small, 66);
    g_message_slot = 1;
    queue(game, "apply\npopup 36 FROM ALICE");
    frames(game, 25);
    check(g_messages == 1 && game.queued_popups() == 1 && entry_text(g_small, 66) == shown,
          "a popup waits while the whole run is taken");
    end_message(0);
    frames(game, 1);
    check(g_hd_texts.size() == 2 && g_hd_texts[1].entry == 66 && g_messages == 2,
          "the popup once the notice has gone");
    end_message(1);
    frames(game, 1);
    check(!game.popup_areas().in_use() && entry_text(g_small, 66).empty(), "both free again");
    game.put_back_all(&no_callback);
}

// ItemNames' last entry, whose room the offsets cannot give: the popup still has its name.
void test_the_last_item_name() {
    zap::Game game;
    start(game);
    queue(game, "apply\npopup 50 FROM ALICE");
    frames(game, 1);
    check(g_hd_texts.size() == 1 && g_hd_texts[0].text.ends_with(L"Energy Shield\nfrom ALICE"),
          "the last entry's name, as the translation has it");
    game.put_back_all(&no_callback);
}

// The same translation loaded again into the same buffers (same pointers and count, as the
// allocator may give them), its texts and offsets now another load's: nothing the helper wrote
// is taken for its own, so neither the area is emptied over the new text nor entry 70 put back
// over the new table's own offset.
void test_a_reload_in_place() {
    zap::Game game;
    start(game);
    load_translation("English(HD)", true, L" (old)");
    LoadedTable* table = g_small;
    queue(game, "apply\npopup 36 FROM ALICE");
    frames(game, 1);
    check(g_hd_texts.size() == 1 && game.popup_areas().split(), "a popup in an area");
    LoadedTable* again = make_table(448, hd_small_texts());  // shorter texts: other offsets
    std::copy(again->text.begin(), again->text.end(), table->text.begin());
    std::copy(again->offsets.begin(), again->offsets.end(), table->offsets.begin());
    const std::vector<wchar_t> fresh = table->text;
    const int32_t seventy = table->offsets[70];
    end_message(0);
    frames(game, 1);
    check(table->text == fresh, "the area not emptied: it no longer reads as written");
    game.put_back_all(&no_callback);
    check(table->offsets[70] == seventy && !game.popup_areas().split(),
          "entry 70 left at the new table's own offset");
}

void test_a_language_change_mid_popup() {
    zap::Game game;
    start(game);
    queue(game, "apply\npopup 36 FROM ALICE");
    frames(game, 1);
    LoadedTable* old_small = g_small;
    const int32_t moved = old_small->offsets[70];
    const std::wstring shown = entry_text(old_small, 66);
    // The player picks French: every buffer and table freed, new ones loaded.
    load_translation("French", true, L" (FR)");
    frames(game, 1);
    check(entry_text(old_small, 66) == shown && old_small->offsets[70] == moved,
          "nothing written into the old tables");
    check(!game.popup_areas().split() && !game.popup_areas().in_use(), "the old split forgotten");
    // The next popup: the split made again in the new table.
    end_message(0);
    queue(game, "apply\npopup 36 FROM BOB");
    frames(game, 25);
    check(g_hd_texts.size() == 2 && g_hd_texts[1].text == L"\xE029Missile Box (FR)\nFROM BOB" &&
              entry_text(g_small, g_hd_texts[1].entry) == g_hd_texts[1].text &&
              g_small->offsets[70] > g_small->offsets[66],
          "split again in the new table, the new translation's name");
    game.put_back_all(&no_callback);
}

void test_the_names_written_and_put_back() {
    zap::GameAddresses a;
    reset_translation();
    load_translation();
    with_translation(a);
    zap::EntryWriter names;
    const std::map<zap::EntryKey, std::wstring> want = {
        {{zap::Table::kItems, 47}, L"MASTER SWORD\nFOR ALICE"},
        {{zap::Table::kSmall, 12}, L"COLLECT 12 OS INSTALL DISKS!"}};
    names.watch(zap::loaded_translation(a), want);
    check(entry_text(g_items, 47) == L"MASTER SWORD\nFOR ALICE" &&
              entry_text(g_small, 12) == L"COLLECT 12 OS INSTALL DISKS!" && names.held() == 2,
          "written in place");
    check(names.original({zap::Table::kItems, 47}) == std::wstring(L"Scrap Energy"),
          "the translation's own kept");
    // A name too long for its room: cut to it.
    const int32_t room = g_items->offsets[49] - g_items->offsets[48] - 1;
    names.watch(zap::loaded_translation(a),
                {{{zap::Table::kItems, 48}, std::wstring(200, L'X')}});
    check(entry_text(g_items, 48) == std::wstring(room - 1, L'X'), "cut to its room");
    check(entry_text(g_items, 47) == L"Scrap Energy" &&
              entry_text(g_small, 12) == L"Energy Keys detected!",
          "no longer wanted: put back");

    // A language change between the write and the put-back.
    names.watch(zap::loaded_translation(a), want);
    LoadedTable* old_items = g_items;
    load_translation("French", true, L" (FR)");
    names.watch(zap::loaded_translation(a), want);
    check(entry_text(old_items, 47) == L"MASTER SWORD\nFOR ALICE", "the old table left alone");
    check(entry_text(g_items, 47) == L"MASTER SWORD\nFOR ALICE" &&
              names.original({zap::Table::kItems, 47}) == std::wstring(L"Scrap Energy (FR)"),
          "written into the new one");
    names.restore(zap::loaded_translation(a));
    check(entry_text(g_items, 47) == L"Scrap Energy (FR)" &&
              entry_text(old_items, 47) == L"MASTER SWORD\nFOR ALICE" && names.held() == 0,
          "put back into the new table only");

    // One the game changed since (reloaded in place) is not put back.
    names.watch(zap::loaded_translation(a), want);
    const int32_t at47 = g_items->offsets[47];
    g_items->text[at47] = L'Z';
    names.restore(zap::loaded_translation(a));
    check(entry_text(g_items, 47)[0] == L'Z', "an entry no longer as written: left alone");

    // The default English picked (the flag clear): put back, nothing written.
    load_translation("English(HD)", true);
    names.watch(zap::loaded_translation(a), want);
    g_translation[0x91] = 0;
    names.watch(zap::loaded_translation(a), want);
    check(entry_text(g_items, 47) == L"Scrap Energy" && names.held() == 0,
          "the flag clear: back, and none held");
}

void test_the_scanner_line() {
    zap::Game game;
    start(game);
    game.set_discs(3, 12, "COLLECT 12 OS INSTALL DISKS!", true);
    frames(game, 1);
    check(entry_text(g_small, 12) == L"COLLECT 12 OS INSTALL DISKS!", "the scanner's line");
    check(game.unload_blocker() != nullptr, "an unload waits for it to be put back");
    game.set_discs(0, 0, "");
    frames(game, 2);
    check(entry_text(g_small, 12) == L"Energy Keys detected!", "and back with discs off");
    game.set_discs(3, 12, "COLLECT 12 OS INSTALL DISKS!", true);
    frames(game, 1);
    game.put_back_all(&no_callback);
    check(entry_text(g_small, 12) == L"Energy Keys detected!" && game.unload_blocker() == nullptr,
          "put back with the rest");
}

void test_the_default_english_untouched() {
    zap::Game game;
    start(game, false);  // a translation loaded, then the default English picked
    const std::vector<wchar_t> small = g_small->text, items = g_items->text;
    const std::vector<int32_t> offsets = g_small->offsets;
    // As GAME SAVED leaves slot 0's object: bound to the slot itself, an older serial.
    put<void*>(hd_object(0), 0x220, slot(0));
    put<int32_t>(hd_object(0), 0x228, 7);
    hd_object(0)[0x22C] = 1;  // what the game left: with the flag clear it hides nothing
    queue(game, "apply\npopup 36 FROM ALICE\nnotice 44 OS INSTALL DISK 3");
    game.set_discs(3, 12, "COLLECT 12 OS INSTALL DISKS!", true);
    frames(game, 40);
    check(g_messages == 2 && g_hd_texts.empty() && g_builds.empty(), "the pixel popups, as before");
    check(hd_object(0)[0x22C] == 1, "the mark left as the game left it");
    check(g_small->text == small && g_items->text == items && g_small->offsets == offsets,
          "the tables as loaded");
    check(game.language() == "-", "hello: the default English");
    game.put_back_all(&no_callback);

    // No translation ever loaded (the pointers null).
    zap::Game fresh;
    reset_game();
    reset_translation();
    check(configure(fresh).empty(), "configured");
    queue(fresh, "apply\npopup 36 FROM ALICE");
    frames(fresh, 1);
    check(g_messages == 1 && g_hd_texts.empty() && fresh.language() == "-",
          "a popup in the pixel font");
    fresh.put_back_all(&no_callback);
}


int g_calls = 0;
void panel_handler() { g_calls += 100; }
void widget_handler() { g_calls += 200; }
void rando_menu(void*, int) { g_calls += 300; }
void assist_menu(void*, int) { g_calls += 400; }
void archipelago(void*, int) { ++g_calls; }

void test_archipelago_built_again_and_back() {
    reset_translation();
    load_translation();
    ModePanel panel(reinterpret_cast<void*>(&panel_handler),
                    reinterpret_cast<void*>(&widget_handler), reinterpret_cast<void*>(&rando_menu));
    zap::GameAddresses a = panel.addresses();
    with_translation(a);
    HdGlyphs<3 * zap::kLabelLength> hd(panel.label);
    // The game attaches the panel's text before it places the panel: built at its first pixel
    // glyph's place then, less 1 (0x8AC30), the window's corner; the panel then placed, its
    // pixel glyphs at the label's place and its translated glyphs moved with them (0xA79E0).
    built(panel.label, -1.0, -1.0);
    for (BYTE* g : panel.glyph_list) put<double>(g, 0x40, 160.0), put<double>(g, 0x48, 101.0);
    zap::ArchipelagoChoice choice;
    check(choice.install(a, &archipelago) && panel.text() == "ARCHIPELAGO", "installed");
    check(g_builds.size() == 1 && g_builds[0].text == L"Archipelago" &&
              g_builds[0].label == panel.label && g_builds[0].x == 159.0f &&
              g_builds[0].y == 100.0f && g_builds[0].font == g_translation + 0xD0 &&
              g_builds[0].flag == 0,
          "its translated text built again where the game's attach would build it now");
    check(hd.x(0) == 159.0 && hd.y(0) == 99.0 && get<int32_t>(panel.label, 0x248) == 11,
          "on the panel, not in the window's corner");
    // The panel placed elsewhere since (0xA79E0: its pixel glyphs placed, its translated glyphs
    // moved by as much, the place the build was given left as it was).
    for (BYTE* g : panel.glyph_list) put<double>(g, 0x40, 170.0), put<double>(g, 0x48, 120.0);
    for (int i = 0; i < 11; ++i) {
        put<double>(hd.glyphs[i], 0x40, hd.x(i) + 11.0);
        put<double>(hd.glyphs[i], 0x48, hd.y(i) + 20.0);
    }
    check(choice.restore(a, &archipelago), "restored");
    check(g_builds.size() == 2 && g_builds[1].text == L"Randomizer" && g_builds[1].x == 169.0f &&
              g_builds[1].y == 119.0f && hd.x(0) == 169.0,
          "and back to entry 354's, where the panel is now");

    // A hidden panel's label (the file select before an empty slot is picked): built hidden,
    // its count kept for the game to show when the panel shows.
    g_builds.clear();
    for (int i = 0; i < 11; ++i) hd.glyphs[i][0x120] = 0;
    panel.reset_label();
    check(choice.install(a, &archipelago) && g_builds.size() == 1 &&
              get<int32_t>(panel.label, 0x244) == 11 && hd.glyphs[0][0x120] == 0 &&
              hd.glyphs[10][0x120] == 0,
          "a hidden label built again hidden, its count kept");
    check(choice.restore(a, &archipelago) && hd.glyphs[0][0x120] == 0, "and put back hidden");

    // A label whose pixel glyphs cannot be read is not built again.
    g_builds.clear();
    BYTE** list = get<BYTE**>(panel.label, 0x228);
    put<BYTE**>(panel.label, 0x228, nullptr);
    check(!zap::rebuild_label(a, panel.label, L"Archipelago", {.attached = true}) &&
              g_builds.empty(),
          "no first pixel glyph: nothing built");
    put<BYTE**>(panel.label, 0x228, list);
    // Nor one with more glyphs than its translated text holds (the builder would lay the rest
    // over its last glyph).
    check(!zap::rebuild_label(a, panel.label, std::wstring(34, L'A'), {.attached = true}) &&
              zap::rebuild_label(a, panel.label, std::wstring(33, L'A') + L"   ",
                                 {.attached = true}) &&
              g_builds.size() == 1,
          "a text past its capacity refused; spaces take no glyph");

    // The default English: nothing built.
    g_builds.clear();
    g_translation[0x91] = 0;
    panel.reset_label();
    check(choice.install(a, &archipelago) && choice.restore(a, &archipelago), "again");
    check(g_builds.empty(), "with the flag clear, no translated text built");
}

// In a translation the locked ASSIST is its translated text with the padlock after it, in the
// locked style, built where the game's attach builds it; the padlock left out where it would pass
// the options box; built again when the game builds the label again; the entry's text back when
// unlocked. With the flag clear, the pixel look, the padlock eighth.
void test_assist_in_a_translation(bool on = true) {
    reset_translation();
    load_translation("English(HD)", on);
    OptionsMenu options(reinterpret_cast<void*>(&widget_handler),
                        reinterpret_cast<void*>(&assist_menu));
    zap::GameAddresses a = options.addresses();
    with_translation(a);
    HdGlyphs<3 * zap::kAssistLength> hd(options.label);
    for (int i = 0; i < zap::kAssistLength; ++i) {
        put<double>(options.glyph_list[i], 0x40, 137.0 + 8 * i);
        put<double>(options.glyph_list[i], 0x48, 172.0);
        options.glyph_list[i][0x120] = 0;  // hidden, as the attach left them
    }
    built(options.label, 136.0, 171.0);
    // The options box's border: its top right piece from x 193 (shot 13's box in English(HD)).
    BYTE piece[0x80] = {};
    put<BYTE*>(options.second, 0x228, piece);
    put<double>(piece, 0x40, 193.0);
    zap::AssistLock lock;
    auto frame = [&](int i) { return get<int32_t>(options.glyph_list[i], 0x1C4); };
    auto count = [&] { return get<int32_t>(options.label, 0x248); };
    // The options entries' style, dimmed: style 5's color is the cursor's on some screens.
    auto all_locked_style = [&] {
        for (int i = 0; i < count(); ++i) {
            if (get<int32_t>(hd.glyphs[i], 0x1D0) != 0x15 ||
                get<float>(hd.glyphs[i], 0x84) != 0.45f) {
                return false;
            }
        }
        return count() > 0;
    };
    check(lock.install(a), "locked");
    check(frame(7) == zap::kLockGlyph, "the pixel glyphs styled as ever, the padlock eighth");
    if (!on) {
        check(g_builds.empty() && lock.restore(a) && g_builds.empty(),
              "the flag clear: nothing built");
        return;
    }
    // In English(HD) the padlock's cell holds its own gap: no space before it.
    check(!g_builds.empty() && g_builds.back().text == L"Assist\xE03B" &&
              g_builds.back().x == 136.0f && g_builds.back().y == 171.0f && count() == 7 &&
              all_locked_style() && !zap::pixel_glyphs_shown(options.label),
          "its translated text with the padlock, in the locked style, its pixel glyphs hidden");
    // Kept while locked; the game building the label again is noticed and built over.
    const size_t builds = g_builds.size();
    put<int32_t>(hd.glyphs[0], 0x1D0, 5);  // the menu restyles it
    put<float>(hd.glyphs[1], 0x84, 1.0f);
    check(!lock.install(a) && g_builds.size() == builds && all_locked_style(),
          "kept, its style and alpha pinned");
    built(options.label, 136.0, 171.0);
    put<int32_t>(options.label, 0x248, 6);  // the game's Assist again
    check(!lock.install(a) && g_builds.size() == builds + 1 &&
              g_builds.back().text == L"Assist\xE03B",
          "built again over the game's");
    check(lock.restore(a) && g_builds.back().text == L"Assist" && count() == 6 &&
              get<float>(hd.glyphs[0], 0x84) == 1.0f,
          "unlocked: the entry's text again, at full alpha");

    // A box too narrow for the padlock: the text alone.
    g_builds.clear();
    put<double>(piece, 0x40, 176.0);
    zap::AssistLock narrow;
    check(narrow.install(a) && g_builds.size() == 2 && g_builds.back().text == L"Assist" &&
              all_locked_style(),
          "the padlock left out where it would pass the box");
    narrow.restore(a);

    // A translation drawn as pixels (Font Style and Icon Style 1, as French): a space before the
    // pixel padlock, as before.
    g_builds.clear();
    put<double>(piece, 0x40, 193.0);
    put<int32_t>(g_translation, 0xA8, 1);
    put<int32_t>(g_translation, 0xA4, 1);
    zap::AssistLock pixel;
    check(pixel.install(a) && !g_builds.empty() && g_builds.back().text == L"Assist \xE03B",
          "a pixel font: the space kept");
    pixel.restore(a);
}

// The options menu built again after Video in English(HD), as the owner saw on the 1.0.0 RC: the
// new menu's VERSION 1.20 in the locked ASSIST's freed place is left as the game built it (its
// pixel glyphs and its translated text), and the new ASSIST's text is built with the padlock.
void test_assist_in_a_translation_after_the_menu_is_built_again() {
    reset_translation();
    load_translation("English(HD)", true);
    OptionsMenu options(reinterpret_cast<void*>(&widget_handler),
                        reinterpret_cast<void*>(&assist_menu));
    OptionsMenu fresh(reinterpret_cast<void*>(&widget_handler),
                      reinterpret_cast<void*>(&assist_menu));
    zap::GameAddresses a = options.addresses();
    with_translation(a);
    HdGlyphs<3 * zap::kAssistLength> hd(options.label), hd_fresh(fresh.label);
    for (OptionsMenu* o : {&options, &fresh}) {
        for (BYTE* g : o->glyph_list) g[0x120] = 0;  // hidden, as the attach left them
        built(o->label, 136.0, 171.0);
    }
    BYTE piece[0x80] = {};  // the options box's top right piece
    put<BYTE*>(options.second, 0x228, piece);
    put<double>(piece, 0x40, 193.0);
    zap::AssistLock lock;
    check(lock.install(a), "locked");

    // Built again where it was: VERSION 1.20 (no callback, in its own panel, its text built by
    // the game) in the old ASSIST's place, the new ASSIST sixth of the selectable eight.
    BYTE* version_list[1] = {};
    options.rebuild_in_place(fresh, version_list);
    built(options.label, 8.0, 8.0);
    put<int32_t>(options.label, 0x248, 11);
    for (BYTE* g : hd.list) {
        put<int32_t>(g, 0x1D0, 0x15);
        put<float>(g, 0x84, 1.0f);
    }
    g_builds.clear();

    check(lock.install(a), "the new menu's ASSIST locked");
    bool version_kept = get<int32_t>(options.glyph_list[7], 0x1C4) == ' ' &&
                        get<int32_t>(options.glyph_list[0], 0x1D0) == 0x15 &&
                        get<int32_t>(options.label, 0x248) == 11;
    for (BYTE* g : hd.list) version_kept = version_kept && get<float>(g, 0x84) == 1.0f;
    for (const BuildCall& b : g_builds) version_kept = version_kept && b.label != options.label;
    check(version_kept, "VERSION 1.20: its glyphs, its text and its look left as the game built them");
    check(!g_builds.empty() && g_builds.back().label == fresh.label &&
              g_builds.back().text == L"Assist\xE03B" &&
              get<void*>(fresh.child, 0x2C0) == nullptr &&
              get<float>(hd_fresh.glyphs[0], 0x84) == 0.45f,
          "the new ASSIST: its text with the padlock, dimmed, its callback cleared");
    check(lock.restore(a) && g_builds.back().label == fresh.label &&
              g_builds.back().text == L"Assist" &&
              get<void*>(fresh.child, 0x2C0) == reinterpret_cast<void*>(&assist_menu) &&
              get<int32_t>(options.label, 0x248) == 11,
          "unlocked: the new ASSIST's text and callback back, VERSION untouched");
}

// The popups' layout in a translation's font: lines measured as the builder lays them out (a
// stand-in pixel font, every character 6 wide, a space 3), wrapped at the cap, a popup's lines
// centered with spaces, a notice's left as they wrap; without metrics, wrapped by characters.
void test_the_popup_layout() {
    reset_translation();
    load_translation();
    zap::GameAddresses a;
    with_translation(a);
    BYTE* font = g_translation + 0xD0;
    BYTE glyph[0x20] = {};
    put<int32_t>(glyph, 0x10, 6 * 64);  // advance 6 at scale 1
    put<BYTE*>(font, 0x08, glyph);
    put<float>(font, 0x40040, 3.0f);
    put<double>(font, 0x40060, 1.0);
    put<int32_t>(g_translation, 0xA8, 1);  // a pixel font
    zap::FontMetrics m;
    check(m.load(a) && m.width(L"ab c") == 21.0 && m.space() == 3.0, "measured as laid out");
    put<int32_t>(font, 0x30 + 'q' * 4, -1);  // no glyph: skipped
    check(m.width(L"aqb") == 12.0, "a character with no glyph takes no room");
    check(zap::layout_popup(L"Energy Tank", L"from Bob", &m, 192, 30) ==
              L"Energy Tank\n   from Bob",
          "a popup's shorter line centered with spaces");
    check(zap::layout_popup(L"Tank", L"from Bob", &m, 192, 30) == L"    Tank\nfrom Bob",
          "whichever line is shorter (half of 21 over 3, rounded)");
    auto pad = [](size_t n, const wchar_t* line) { return std::wstring(n, L' ') + line; };
    check(zap::layout_popup(L"Energy Tank", L"from a player with a long name", &m, 60, 30) ==
              pad(4, L"Energy\n") + pad(6, L"Tank\n") + pad(5, L"from a\n") +
                  pad(4, L"player\n") + L"with a long\n" + pad(6, L"name"),
          "every line no wider than the cap, words whole, centered on the widest");
    check(zap::layout_popup(L"", L"Hint: the item is here and there", &m, 60, 30) ==
              L"Hint: the\nitem is\nhere and\nthere",
          "a notice wrapped, left as it wraps");
    check(zap::layout_popup(L"", L"line one\nline two", &m, 192, 30) == L"line one\nline two",
          "a notice's own line breaks kept");
    check(zap::centered_lines(L"Energy Tank\nfrom Bob", &m) == L"Energy Tank\n   from Bob" &&
              zap::centered_lines(L"AP ITEM\nFOR ALICE", &m) == L"  AP ITEM\nFOR ALICE",
          "a stand-in's name centered as a popup's lines");
    check(zap::centered_lines(L"AP ITEM\nFOR ALICE", nullptr) == L"AP ITEM\nFOR ALICE",
          "no metrics: as it is");
    check(zap::layout_popup(L"Energy Tank", L"from Bob", nullptr, 0, 8) ==
              L"Energy\nTank\nfrom Bob",
          "no metrics: wrapped by characters, not centered");
    put<double>(font, 0x40060, std::numeric_limits<double>::quiet_NaN());
    check(!m.load(a), "a font whose values are not a loaded font's: not measured");
    put<double>(font, 0x40060, 1.0);
    check(zap::hd_suffix("FROM Bob") == L"from Bob" &&
              zap::hd_suffix("FROM ALICE") == L"from ALICE" &&
              zap::hd_suffix("FOR Bob") == L"FOR Bob",
          "the client's FROM in the sentence's case, the name as sent");
    reset_translation();
}

// The icon sheets through the game: painted while a translation shows, cleared when the default
// English is picked and before an unload (put_back_all), and left as the game's where a cell is
// its.
void test_the_icons_through_the_game() {
    zap::Game game;
    start(game);
    gl_fake::reset_gl();
    game.set_gl(gl_fake::fake_gl());
    BYTE hd[0x1D8] = {}, pixel[0x1D8] = {};
    for (BYTE* t : {hd, pixel}) put<int32_t>(t, 0x1BC, 256);
    put<int32_t>(hd, 0x1D0, static_cast<int32_t>(gl_fake::kHdSheet));
    put<int32_t>(pixel, 0x1D0, static_cast<int32_t>(gl_fake::kPixelSheet));
    g_template_list[101] = hd;
    g_template_list[104] = pixel;
    frames(game, 1);
    check(gl_fake::g_uploads.size() == 6 && !gl_fake::all_blank(gl_fake::g_uploads),
          "painted into both sheets while the translation shows");
    gl_fake::g_uploads.clear();
    g_translation[0x91] = 0;  // the default English picked
    frames(game, 1);
    check(gl_fake::g_uploads.size() == 6 && gl_fake::all_blank(gl_fake::g_uploads),
          "cleared when the default English is picked");
    gl_fake::g_uploads.clear();
    frames(game, 1);
    check(gl_fake::g_uploads.empty(), "and not again");
    g_translation[0x91] = 1;
    frames(game, 1);
    check(gl_fake::g_uploads.size() == 6, "painted again with the translation back");
    gl_fake::g_uploads.clear();
    game.put_back_all(&no_callback);
    check(gl_fake::g_uploads.size() == 6 && gl_fake::all_blank(gl_fake::g_uploads),
          "cleared before an unload");

    // A cell the game's art uses: left as it is.
    zap::Game other;
    start(other);
    gl_fake::reset_gl();
    gl_fake::g_icon_cell_used = true;
    other.set_gl(gl_fake::fake_gl());
    frames(other, 1);
    check(gl_fake::g_uploads.empty(), "a sheet whose cell the game uses: left as the game's");
    other.put_back_all(&no_callback);
    check(gl_fake::g_uploads.empty(), "and nothing to clear");
    g_template_list[101] = g_template_list[104] = nullptr;
    gl_fake::reset_gl();
    reset_translation();
}

// A popup measured as the font draws it, through the game: a pixel font (every character 6 wide,
// a space 3) and its icon sheet (each icon 8 wide), the icon led name over the suffix, each
// line centered on the widest; a long notice wrapped at 192 pixels.
void test_a_popup_laid_out_by_the_font() {
    zap::Game game;
    start(game);
    BYTE* font = g_translation + 0xD0;
    BYTE glyph[0x20] = {};
    put<int32_t>(glyph, 0x10, 6 * 64);
    put<BYTE*>(font, 0x08, glyph);
    put<float>(font, 0x40040, 3.0f);
    put<double>(font, 0x40060, 1.0);
    put<int32_t>(g_translation, 0xA8, 1);  // a pixel font: definition 104's icons
    std::vector<BYTE> frames_list(256 * 0x38);
    for (int f = 0; f < 256; ++f) put<int16_t>(frames_list.data() + f * 0x38, 0x28, 8);
    BYTE sheet[0x1D8] = {};
    put<int32_t>(sheet, 0x1BC, 256);
    put<BYTE*>(sheet, 0x1A8, frames_list.data());
    g_template_list[104] = sheet;
    queue(game, "apply\npopup 36 FROM Bob");
    frames(game, 1);
    // "\xE029Missile Box" is 8 + 10 x 6 + 3 = 71 wide, "from Bob" 45: 13 more each side, 4 spaces.
    check(g_hd_texts.size() == 1 &&
              g_hd_texts[0].text == L"\xE029" L"Missile Box\n    from Bob",
          "the icon led name over the suffix, centered with spaces, measured with the icon's "
          "width");
    // A notice longer than a line: wrapped, no line wider than 192.
    end_message(0);
    frames(game, 200);
    g_hd_texts.clear();
    g_message_slot = 1;
    queue(game,
          "apply\nnotice 49 HINT: PROGRESSIVE FORCE BLASTER IS AT DOUBLE JUMP FLOATING SHEEP IN "
          "BOB'S WORLD");
    frames(game, 1);
    zap::FontMetrics m;
    zap::GameAddresses a;
    with_translation(a);
    a.sprite_templates = reinterpret_cast<BYTE*>(&g_sprite_templates);
    bool narrow = !g_hd_texts.empty() && m.load(a);
    size_t lines = 0;
    for (size_t at = 0; narrow && at <= g_hd_texts[0].text.size(); ++lines) {
        size_t end = g_hd_texts[0].text.find(L'\n', at);
        if (end == std::wstring::npos) end = g_hd_texts[0].text.size();
        narrow = m.width(g_hd_texts[0].text.substr(at, end - at)) <= 192;
        at = end + 1;
    }
    check(narrow && lines >= 3, "a long notice wrapped, no line wider than a pixel popup");
    g_template_list[104] = nullptr;
    game.put_back_all(&no_callback);
    reset_translation();
}

// A stand-in's name written into ItemNames through the game: its lines centered with leading
// spaces as a received item's popup's, unless the spaces would not fit the entry's room (33
// characters for entry 47 here), when it is written as it is, so no name is cut.
void test_a_stand_in_name_centered_through_the_game() {
    zap::Game game;
    start(game);
    BYTE* font = g_translation + 0xD0;
    BYTE glyph[0x20] = {};
    put<int32_t>(glyph, 0x10, 6 * 64);  // every character 6 wide, a space 3
    put<BYTE*>(font, 0x08, glyph);
    put<float>(font, 0x40040, 3.0f);
    put<double>(font, 0x40060, 1.0);
    put<int32_t>(g_translation, 0xA8, 1);  // a pixel font: definition 104's icons, 8 wide
    std::vector<BYTE> frames_list(256 * 0x38);
    for (int f = 0; f < 256; ++f) put<int16_t>(frames_list.data() + f * 0x38, 0x28, 8);
    BYTE sheet[0x1D8] = {};
    put<int32_t>(sheet, 0x1BC, 256);
    put<BYTE*>(sheet, 0x1A8, frames_list.data());
    g_template_list[104] = sheet;
    own_stand_ins(g_records);
    field<uint8_t>(0x107A) = 2;  // the player in cell (5, 2)
    field<uint8_t>(0x107B) = 5;
    auto label = [&](const std::string& text) {
        std::map<int, zap::Label> labels;
        zap::parse_labels("ap_labels\nlabel 5 2 " + hex(text), labels);
        game.set_labels(labels);
        frames(game, 1);
        return entry_text(g_items, 47);
    };
    // The logo 8 and AP ITEM 39 wide, FOR BOB 39: half of 8 over a space of 3, one space.
    check(label("AP ITEM\nFOR BOB") == L"\xE03C" L"AP ITEM\n FOR BOB",
          "centered with a leading space");
    // A long first line over a short second: 20 spaces would pass the room; written as it is.
    check(label("ABCDEFGHIJKLMNOPQRSTUVW\nFOR B") ==
              L"\xE03C" L"ABCDEFGHIJKLMNOPQRSTUVW\nFOR B",
          "not centered where the spaces would not fit, nothing cut");
    g_template_list[104] = nullptr;
    game.put_back_all(&no_callback);
    reset_translation();
}

// A stand-in's name stays as it was picked up while its pickup message shows (the HD message
// reads ItemNames 47 again every frame): walking into the next cell or pausing leaves it, and
// it follows the player again once the message ends. A stand-in of that record within reach
// takes it, for the next pickup.
void test_a_stand_in_name_held_while_its_popup_shows() {
    zap::Game game;
    start(game);
    own_stand_ins(g_records);
    std::map<int, zap::Label> labels;
    zap::parse_labels("ap_labels\nlabel 5 2 " + hex(std::string("MASTER SWORD\nFOR BOB")) +
                          "\nlabel 8 2 " + hex(std::string("BOOMERANG\nFOR CAROL")),
                      labels);
    game.set_labels(labels);
    auto in_cell = [](int column) {
        field<uint8_t>(0x107A) = 2;
        field<uint8_t>(0x107B) = static_cast<uint8_t>(column);
    };
    in_cell(5);
    frames(game, 1);
    const std::wstring picked = entry_text(g_items, 47);
    check(picked.find(L"MASTER SWORD") != std::wstring::npos, "named for the player's cell");
    // The pickup's message (0x1F985): slot 0 bound to entry `entry`, kind 3, with its serial.
    auto pick_up = [](int32_t serial, int32_t entry, int k = 0) {
        put<int32_t>(slot(k), 0x1D8, serial);
        put<int32_t>(slot(k), 0x1E0, k);
        put<BYTE*>(hd_object(k), 0x220, slot(k));
        put<int32_t>(hd_object(k), 0x228, serial);
        put<int32_t>(hd_object(k), 0x20C, entry);
        put<int32_t>(hd_object(k), 0x210, 3);
    };
    pick_up(41, 47);
    in_cell(6);  // no label there: AP Item
    frames(game, 2);
    check(entry_text(g_items, 47) == picked, "the next cell: the name picked up held");
    g_menu = 1;  // paused
    frames(game, 1);
    check(entry_text(g_items, 47) == picked, "paused: not put back while it shows");
    g_menu = 0;
    pick_up(42, 48);  // a message of the other record's: not this one's
    frames(game, 1);
    check(entry_text(g_items, 47).find(L"MASTER SWORD") == std::wstring::npos,
          "a message of another entry holds nothing");
    // SmallText 47 (kind 0), and an object bound to an earlier message of the slot: neither holds.
    in_cell(5);
    frames(game, 1);
    pick_up(44, 47);
    put<int32_t>(hd_object(0), 0x210, 0);
    in_cell(6);
    frames(game, 1);
    check(entry_text(g_items, 47).find(L"MASTER SWORD") == std::wstring::npos,
          "another kind's entry 47 holds nothing");
    in_cell(5);
    frames(game, 1);
    pick_up(45, 47);
    put<int32_t>(slot(0), 0x1D8, 46);  // the slot's next message, not bound
    in_cell(6);
    frames(game, 1);
    check(entry_text(g_items, 47).find(L"MASTER SWORD") == std::wstring::npos,
          "an object bound to an earlier message holds nothing");
    end_message(0);
    in_cell(5);
    frames(game, 1);
    pick_up(43, 47);
    in_cell(6);
    frames(game, 1);
    check(entry_text(g_items, 47) == picked, "held again for the next pickup");
    // Another Scrap Energy stand-in, for (8, 2), within reach, a Scrap Chunk one for (5, 2)
    // nearer: 47 named for its own, for the next pickup.
    field<double>(0x1100) = 1000;
    field<double>(0x1108) = 600;
    auto object = [](int k, int16_t record, int column, double x) {
        BYTE* o = g_item_pool + k * 0x3B0;
        std::memset(o, 0, 0x3B0);
        put<int32_t>(o, 0x1D8, 77 + k);
        put<int16_t>(o, 0x1E8, record);
        o[0x3AC] = 2;
        o[0x3AD] = static_cast<BYTE>(column);
        put<double>(o, 0x40, x);
        put<double>(o, 0x48, 600.0);
    };
    object(0, 47, 8, 1040.0);
    object(1, 48, 5, 1005.0);
    frames(game, 1);
    check(entry_text(g_items, 47).find(L"BOOMERANG") != std::wstring::npos,
          "one of that record within reach: named for it");
    std::memset(g_item_pool, 0, 2 * 0x3B0);
    end_message(0);
    frames(game, 1);
    check(entry_text(g_items, 47).find(L"MASTER SWORD") == std::wstring::npos &&
              entry_text(g_items, 47).find(L"BOOMERANG") == std::wstring::npos,
          "the message ended: named for the player's cell again");
    g_menu = 1;
    frames(game, 1);
    check(entry_text(g_items, 47) == L"Scrap Energy", "paused with the message ended: put back");
    g_menu = 0;

    // Another pickup of that record no scan had seen (a newer message in slot 1, the first still
    // closing in slot 0): named as the frame names it, then held.
    in_cell(5);
    frames(game, 1);
    pick_up(50, 47);
    in_cell(8);
    frames(game, 1);
    check(entry_text(g_items, 47) == picked, "held for the first pickup");
    pick_up(51, 47, 1);
    frames(game, 1);
    in_cell(6);
    frames(game, 2);
    check(entry_text(g_items, 47).find(L"BOOMERANG") != std::wstring::npos,
          "a newer message: named for it, then held");
    end_message(1);
    end_message(0);
    frames(game, 1);

    // The map open (its menu current, its first child holding the map's handler, wrapped): the
    // game's own name for the map cursor's squares, and the held one again once it closes.
    int map_handler = 0, hook = 0;
    zap::Hooks hooks;
    hooks.map = &hook;
    game.set_hooks(hooks);
    g_map_handler = &map_handler;
    BYTE child[0x200] = {};
    put<void*>(child, 0x168, &map_handler);
    put<void*>(child, 0x178, &map_handler);
    BYTE* children[1] = {child};
    BYTE panel[0x300] = {};
    put<BYTE**>(panel, 0x1E0, children);
    BYTE* panels[1] = {panel};
    BYTE* menu = reinterpret_cast<BYTE*>(panels);
    in_cell(5);
    frames(game, 1);
    pick_up(52, 47);
    in_cell(6);
    frames(game, 1);
    g_current_menu = reinterpret_cast<BYTE*>(&menu);
    g_menu = 2;
    frames(game, 1);
    check(get<void*>(child, 0x168) == &hook && entry_text(g_items, 47) == L"Scrap Energy",
          "the map open: the game's own name");
    g_current_menu = nullptr;
    g_menu = 0;
    frames(game, 1);
    check(entry_text(g_items, 47) == picked, "the map closed: the name picked up again");
    // Open again with no wrap (no map hook): the game's own name all the same.
    game.set_hooks({});
    put<void*>(child, 0x168, &map_handler);
    put<void*>(child, 0x178, &map_handler);
    g_current_menu = reinterpret_cast<BYTE*>(&menu);
    g_menu = 2;
    frames(game, 1);
    check(get<void*>(child, 0x168) == &map_handler && entry_text(g_items, 47) == L"Scrap Energy",
          "the map open, not wrapped: the game's own name");
    g_current_menu = nullptr;
    g_menu = 0;
    frames(game, 1);
    check(entry_text(g_items, 47) == picked, "and the name picked up again once it closes");
    end_message(0);
    frames(game, 1);
    g_map_handler = nullptr;

    // Back at the title with the message still bound: no save, so nothing held.
    in_cell(5);
    frames(game, 1);
    pick_up(47, 47);
    g_menu = 1;
    g_save_loaded = 0xFF;
    frames(game, 1);
    check(entry_text(g_items, 47) == L"Scrap Energy", "no save loaded: put back");
    g_save_loaded = 3;
    g_menu = 0;

    // Another translation picked while it shows: the name picked up, written into its table.
    end_message(0);
    frames(game, 1);
    pick_up(48, 47);
    in_cell(6);
    load_translation("French", true, L" (FR)");
    frames(game, 1);
    check(entry_text(g_items, 47) == picked, "a language change: the name picked up kept");
    // The client gone with the message still bound: put back, and not written again.
    game.client_gone(&no_callback);
    frames(game, 2);
    check(entry_text(g_items, 47) == L"Scrap Energy (FR)", "the client gone: put back for good");
    game.put_back_all(&no_callback);
    reset_translation();
}

// The suffix's case, the owner's choice: in its own case in English(HD) only ("from Bob"); in
// capitals in any other translation, as in the default English's pixel text.
void test_the_suffix_case() {
    zap::Game hd;
    start(hd);
    queue(hd, "apply\npopup 36 FROM Bob");
    frames(hd, 1);
    check(g_hd_texts.size() == 1 && g_hd_texts[0].text.ends_with(L"\nfrom Bob"),
          "English(HD): the suffix in the case the client sent it");
    hd.put_back_all(&no_callback);

    zap::Game french;
    start(french);
    load_translation("French");
    queue(french, "apply\npopup 36 FROM Bob");
    frames(french, 1);
    check(g_hd_texts.size() == 1 && g_hd_texts[0].text.ends_with(L"\nFROM BOB"),
          "French: the suffix in capitals");
    french.put_back_all(&no_callback);

    zap::Game plain;
    start(plain, false);
    queue(plain, "apply\npopup 36 FROM Bob");
    frames(plain, 1);
    check(g_hd_texts.empty() && g_text.find("FROM BOB") != std::string::npos,
          "the default English: the pixel text in capitals, as before");
    plain.put_back_all(&no_callback);
    reset_translation();
}

// The mod's own text in mixed case in English(HD) only (the owner's choice, 2026-11-02): a
// client's notice, the helper's own, a label and the scanner's line in the case they were
// written in; in capitals in French and in the default English's pixel text.
void test_the_mixed_case() {
    struct Language {
        const char* name;
        bool on;
    };
    for (const Language language : {Language{"English(HD)", true}, Language{"French", true},
                                    Language{"English(HD)", false}}) {
        const bool hd = language.on && std::string(language.name) == "English(HD)";
        const std::string where = language.on ? language.name : "the default English";
        zap::Game game;
        start(game, language.on);
        if (language.on) load_translation(language.name);
        // A client's notice (an Area Map's hint).
        queue(game, "apply\nnotice 49 Hint: Slide Mod is at Guardia 22,7");
        frames(game, 1);
        const std::wstring hint = g_hd_texts.empty() ? L"" : g_hd_texts.back().text;
        if (language.on) {
            check(hint.find(hd ? L"Hint: Slide Mod is at" : L"HINT: SLIDE MOD IS AT") !=
                      std::wstring::npos,
                  "a client's notice", where);
        } else {
            check(g_hd_texts.empty() && g_text.find("HINT: SLIDE MOD IS AT") != std::string::npos,
                  "a client's notice in pixels", where);
        }
        // A label sent in mixed case, its lines kept in both cases.
        std::map<int, zap::Label> labels;
        const std::string text = "AP Item\nfor Alice";
        check(zap::parse_labels("ap_labels\nlabel 5 2 " + hex(text), labels).empty() &&
                  labels.size() == 1 && labels.begin()->second.own_first == "AP Item" &&
                  labels.begin()->second.own_who == "for Alice" &&
                  labels.begin()->second.who == "FOR ALICE",
              "a label parsed in both cases");
        game.set_labels(labels);
        const int cell = 2 * 54 + 5;
        if (language.on) {
            check(game.hd_label_text(cell, false, true) ==
                      (hd ? L"\xE03C" L"AP Item\nfor Alice" : L"\xE03C" L"AP ITEM\nFOR ALICE"),
                  "a stand-in's name", where);
            check(game.hd_label_text(cell, true, false) == (hd ? L"AP Item" : L"AP ITEM"),
                  "the map cursor's", where);
        }
        // Another Zexion player's item with no name of the client's: in English(HD), the
        // translation's own name for it.
        std::map<int, zap::Label> zexion;
        check(zap::parse_labels("ap_labels\nlabel 6 2 " + hex(std::string("@36\nfor Bob")),
                                zexion)
                  .empty(),
              "an @N label parsed");
        game.set_labels(zexion);
        if (hd) {
            const std::wstring own_name = entry_text(g_items, 36);
            check(!own_name.empty() &&
                      game.hd_label_text(2 * 54 + 6, false, false) == own_name + L"\nfor Bob",
                  "another Zexion player's item by the translation's name");
            // Another player's Scrap Energy (@47) where the player stands: its name is the
            // game's own, not the label the helper writes into that same entry, frame after
            // frame.
            own_stand_ins(g_records);
            field<uint8_t>(0x107A) = 2;
            field<uint8_t>(0x107B) = 7;
            std::map<int, zap::Label> scrap;
            zap::parse_labels("ap_labels\nlabel 7 2 " + hex(std::string("@47\nfor Bob")), scrap);
            game.set_labels(scrap);
            frames(game, 3);
            check(entry_text(g_items, 47) == L"\xE03C" L"Scrap Energy\nfor Bob",
                  "@47: the game's own name, written once, not grown from the helper's",
                  std::string(entry_text(g_items, 47).size(), '.'));
        }
        // The scanner's line: in the pixel Text table always in capitals.
        game.set_discs(3, 12, "Collect 12 OS Install Disks!", true);
        frames(game, 1);
        if (language.on) {
            check(entry_text(g_small, 12) == (hd ? L"Collect 12 OS Install Disks!"
                                                 : L"COLLECT 12 OS INSTALL DISKS!"),
                  "the scanner's line", where);
        }
        check(std::string(reinterpret_cast<const char*>(g_text_table + 12 * 32)) ==
                  "COLLECT 12 OS INSTALL DISKS!",
              "the scanner's pixel line in capitals", where);
        game.put_back_all(&no_callback);
    }
    reset_translation();
}

// The helper's own notices in mixed case in English(HD): an AP save dressed with no client.
void test_the_helpers_notice_case() {
    for (const bool hd : {true, false}) {
        zap::Game game;
        start(game);
        if (!hd) load_translation("French");
        std::memset(g_section, 0, sizeof(g_section));
        g_section[0x26C] = 0x77;  // a table to match
        const zap::OfflineSeed seed = zap::offline_seed_of(g_section);
        game.leave_offline();
        configure(game, false);  // no client
        game.set_offline({seed});
        g_rando_marker = 1;
        g_save_loaded = 3;
        for (int i = 0; i < 16; ++i) game.watch_offline();
        check(game.queued_popups() == 1, "the not-connected notice queued");
        frames(game, 1);
        const std::wstring shown = g_hd_texts.empty() ? L"" : g_hd_texts.back().text;
        check(shown.find(hd ? L"Archipelago client not" : L"ARCHIPELAGO CLIENT NOT") !=
                  std::wstring::npos,
              hd ? "English(HD): the helper's notice in mixed case" : "French: in capitals");
        game.put_back_all(&no_callback);
    }
    reset_translation();
}

}  // namespace

int main() {
    test_the_text_helpers();
    test_the_mixed_case();
    test_the_helpers_notice_case();
    test_the_popup_layout();
    test_the_icons_through_the_game();
    test_a_popup_laid_out_by_the_font();
    test_the_suffix_case();
    test_a_stand_in_name_centered_through_the_game();
    test_a_stand_in_name_held_while_its_popup_shows();
    test_the_tables();
    test_popups_take_turns_and_empty();
    test_a_notice_too_long_shows_its_pixel_text();
    test_the_pixel_text_when_the_tables_cannot_be_used();
    test_the_run_not_borrowed_when_an_entry_holds_text();
    test_a_whole_notice();
    test_the_last_item_name();
    test_a_language_change_mid_popup();
    test_a_reload_in_place();
    test_the_names_written_and_put_back();
    test_the_scanner_line();
    test_the_default_english_untouched();
    test_archipelago_built_again_and_back();
    test_assist_in_a_translation();
    test_assist_in_a_translation(false);
    test_assist_in_a_translation_after_the_menu_is_built_again();
    return report("translation tests");
}
