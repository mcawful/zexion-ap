// Host tests for translation.h on a stand-in translation (stand_in.h: English(HD)'s tables as
// the parser leaves them): the text helpers, the tables' entries, the received items' popups in
// the translation's font (the two areas taking turns, waiting, emptying), the names written and
// put back, a language change between a write and its put-back, the menu labels built again and
// back, the pixel-glyph swap, and the default English left as it was.

#include <windows.h>

#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "check.h"
#include "game_calls.h"
#include "menu.h"
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
    zap::Batch batch;
    check(zap::parse_batch(request, batch).empty(), "the batch parses", request);
    check(game.apply(batch) == "applied", "the batch applied", request);
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
    check(g_hd_texts.size() == 2 && g_hd_texts[1].text == L"\xE029Missile Box (FR)\nfrom BOB" &&
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
    check(choice.restore(a, &archipelago), "restored");
    check(g_builds.size() == 2 && g_builds[1].text == L"Randomizer" && g_builds[1].x == 159.0f &&
              g_builds[1].y == 100.0f && hd.x(0) == 159.0,
          "and back to entry 354's, in the same place");

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
    auto all_locked_style = [&] {
        for (int i = 0; i < count(); ++i) {
            if (get<int32_t>(hd.glyphs[i], 0x1D0) != 5) return false;
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
    check(!g_builds.empty() && g_builds.back().text == L"Assist \xE03B" &&
              g_builds.back().x == 136.0f && g_builds.back().y == 171.0f && count() == 7 &&
              all_locked_style() && !zap::pixel_glyphs_shown(options.label),
          "its translated text with the padlock, in the locked style, its pixel glyphs hidden");
    // Kept while locked; the game building the label again is noticed and built over.
    const size_t builds = g_builds.size();
    put<int32_t>(hd.glyphs[0], 0x1D0, 0x15);  // the menu restyles it
    check(!lock.install(a) && g_builds.size() == builds && all_locked_style(),
          "kept, its style pinned");
    built(options.label, 136.0, 171.0);
    put<int32_t>(options.label, 0x248, 6);  // the game's Assist again
    check(!lock.install(a) && g_builds.size() == builds + 1 &&
              g_builds.back().text == L"Assist \xE03B",
          "built again over the game's");
    check(lock.restore(a) && g_builds.back().text == L"Assist" && count() == 6,
          "unlocked: the entry's text again");

    // A box too narrow for the padlock: the text alone.
    g_builds.clear();
    put<double>(piece, 0x40, 176.0);
    zap::AssistLock narrow;
    check(narrow.install(a) && g_builds.size() == 2 && g_builds.back().text == L"Assist" &&
              all_locked_style(),
          "the padlock left out where it would pass the box");
    narrow.restore(a);
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

}  // namespace

int main() {
    test_the_text_helpers();
    test_the_popup_layout();
    test_the_tables();
    test_popups_take_turns_and_empty();
    test_a_notice_too_long_shows_its_pixel_text();
    test_the_pixel_text_when_the_tables_cannot_be_used();
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
    return report("translation tests");
}
