// The game's translations (docs/re/memory.md, "Translations" and the sections after it): with a
// translation on (English(HD), French, Japanese and the rest; the flag `tr_flag`), the game still
// builds its pixel-font text but draws the translation's own text, in the translation's font, in
// its place. What the helper writes into the pixel glyphs then does not show, and its popups were
// hidden by the mark the game's last translated message left in their slot. This is how the
// helper's text shows in a translation:
//
// - Received items' popups and notices (PopupAreas): the text is written into a run of SmallText
//   entries the game never shows (66 to 72, split in two by moving entry 70's offset), and the
//   message given it as the game gives GAME SAVED its text (hd_text_fn), since the game reads the
//   entry again every frame while the message shows. Emptied once the message has gone.
// - Names the game reads from the tables while they show (EntryWriter): the stand-ins' names
//   (ItemNames 47 and 48) for the pickup popup, INTEL CHIP (49), the scanner's line (SmallText
//   12), written in place, within the room the entry has, and put back.
// - Menu labels the helper rewrites (rebuild_label): their translated text built again with the
//   game's own builder (hd_build_fn) from a string made by the game's own std::wstring code
//   (wstring_new_fn), at the place the game's build left in the label.
// - Labels whose pixel glyphs the helper styles (ASSIST, the pause row's count, the Feats note):
//   the game's own swap back to the pixel glyphs (show_pixel_glyphs, as 0x168AE9 does).
//
// The tables are the translation's heap memory, freed and loaded again when the player picks
// another translation (a pick of the default English only clears the flag). So each write is made
// only into the tables as they are now, checked against the pointers and count read when its
// state was made, and an entry is put back only while it still reads as the helper wrote it.
//
// Every part stands down without the translation's keys (an offline cache from before them,
// game_calls.cpp: kConfigKeys, `translation_key`) or with the flag clear: the default English is
// as before. Game thread only; every read is fault-safe and every write checked first.
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "addresses.h"

namespace zap {

// The translation object (`translation`, docs/re/memory.md, "Translation tables"): the loaded
// folder's name, the tables' pointers, the flag (the byte `tr_flag` is), and the font the
// translated text is built in (0x8ACAC, 0xB2D05, 0x16B8F4 pass its address to hd_build_fn).
namespace tr {
constexpr ptrdiff_t kName = 0x00;
constexpr size_t kNameSize = 32;
constexpr ptrdiff_t kSmallText = 0x30, kItemNames = 0x48;
constexpr ptrdiff_t kSmallOffsets = 0x58, kItemNameOffsets = 0x78;
constexpr ptrdiff_t kOn = 0x91;
constexpr ptrdiff_t kFont = 0xD0;
constexpr uint64_t kSize = 0xD8;  // through the font's first field
}  // namespace tr

// The HD message objects (`hd_messages`, one per message slot) and the message slots
// (`message_slots`) (docs/re/memory.md, "Translated messages").
namespace hd {
constexpr size_t kSize = 0x238;
constexpr int kSlots = 4;
constexpr ptrdiff_t kEntry = 0x20C, kKind = 0x210;  // 32-bit each
constexpr ptrdiff_t kMessage = 0x220, kSerial = 0x228;
constexpr ptrdiff_t kHasText = 0x22C;  // byte: the mark that hides a message's pixel glyphs
}  // namespace hd
namespace message {
constexpr size_t kSize = 0x1628;
constexpr ptrdiff_t kSerial = 0x1D8, kSlot = 0x1E0;  // 32-bit each
}  // namespace message

// A label's translated glyphs, beside its pixel ones (label::, game_layout.h; docs/re/memory.md,
// "Translated menus and the prompt"): the pixel glyphs shown, whether the translated text was
// built, its glyphs, how many are shown and how many there are, and the place its last build was
// given (doubles, 0x88480 at 0x8869F).
namespace hdlabel {
constexpr ptrdiff_t kPixelShown = 0x1EC;  // 32-bit
constexpr ptrdiff_t kBuilt = 0x230;       // byte
constexpr ptrdiff_t kGlyphs = 0x238;
constexpr ptrdiff_t kShown = 0x244, kCount = 0x248;  // 32-bit each
constexpr ptrdiff_t kX = 0x258, kY = 0x260;
constexpr size_t kSize = 0x268;
}  // namespace hdlabel

// The tables the helper writes: SmallText (labels and messages, kind 0) and ItemNames (by item).
enum class Table { kSmall, kItems };

// One table as loaded now: its text (16-bit characters, each entry ending in a 0) and its int32
// offsets into it, in characters, one per entry (-1 for none).
struct TextTable {
    wchar_t* text = nullptr;
    int32_t* offsets = nullptr;
    int count = 0;
    bool operator==(const TextTable&) const = default;
};

// The translation as it is now.
struct Loaded {
    bool on = false;   // the flag: a translation's text shows
    std::string name;  // the loaded folder's name, "English(HD)" and the like
    TextTable small, items;
    const TextTable& table(Table t) const { return t == Table::kSmall ? small : items; }
};

// The translation's keys are all there and laid out as traced (the tables' pointers and the flag
// at their places in the object): the translation parts stand down otherwise.
bool translation_keys(const GameAddresses& a);
// Game thread: the translation now, if its keys are there and its pointers read; with `on`
// clear, the tables may still be loaded (the default English picked) but nothing is written.
std::optional<Loaded> loaded_translation(const GameAddresses& a);
// Game thread: the flag is set (with the keys there).
bool translation_on(const GameAddresses& a);
// For `hello`: "-" the default English (the flag clear), the loaded translation's name while it
// is on (spaces as underscores), or "?" (no translation keys, or nothing readable).
std::string language_name(const GameAddresses& a);

// Entry k's offset, or -1 (none, or unreadable).
int32_t entry_offset(const TextTable& t, int k);
// The characters entry k may hold in place, its ending 0 included: from its offset to the next
// higher offset of any entry, less the line break before that one (docs/re/memory.md,
// "Translation tables"; the parser never reads the lines between two texts again). 0 if it has
// none, or no entry follows it.
int32_t entry_room(const TextTable& t, int k);
// The text at `offset`, up to its 0 within `room` characters: false if unreadable or no 0 is
// there.
bool read_text(const TextTable& t, int32_t offset, int32_t room, std::wstring& out);
// Writes `text` and a 0 at `offset`, if they fit `room` and the place can be written.
bool write_text(const TextTable& t, int32_t offset, int32_t room, std::wstring_view text);

// The helper's text, as the translation's font draws it: the 8 x 8 font's characters kept (the
// helper's and the client's texts hold no others), anything else left out, and braces, which
// start the game's markup in a translated text (0x88897), as parentheses.
std::wstring hd_text(std::string_view font_text);
// A record's icon byte as the private-use character the translations' fonts draw it as, as the
// pickup does (0x1F89B to 0x1F8CE: 0xC0 to 0xEA as U+E024 to U+E04E, 0x6E as U+E04F, 0x77 as
// U+E050); "" for none or any other (the helper's own glyphs, the AP logo, the disk).
std::wstring hd_icon(uint8_t icon);
// A stand-in's label as laid out (`width` characters a line, its first character the item's
// glyph; text.h, Label) as translated text: each line trimmed, joined by line breaks, the glyph
// as hd_icon; the first line alone with `first_line`, as the map cursor names it.
std::wstring hd_label(const std::string& name, int width, bool first_line = false);

// Game thread, the flag set: builds `label`'s translated text again as `text`, with the game's
// builder at the place its last build was given (the game built it this frame, or since the menu
// opened). False, nothing done, without the keys, with the flag clear, or for a label whose
// translated text was never built.
bool rebuild_label(const GameAddresses& a, BYTE* label, std::wstring_view text);
// Game thread: a label's pixel glyphs shown and its translated ones hidden, as the game does when
// the flag is clear (0x168AE9); and the other way back, for a label the game built translated.
// Whether the label read as expected.
bool show_pixel_glyphs(BYTE* label);
bool show_translated_glyphs(BYTE* label);
// Whether the label's translated glyphs are hidden and its pixel ones shown.
bool pixel_glyphs_shown(const BYTE* label);

// Entries of the tables the helper holds written in place while wanted (the stand-ins' names,
// INTEL CHIP, the scanner's line), and puts back.
using EntryKey = std::pair<Table, int>;
class EntryWriter {
public:
    // Game thread, each frame: with the translation on, each wanted entry holds its text (cut to
    // the entry's room); one written before and no longer wanted (or wanted otherwise) is put
    // back, only into the tables it was written in and only while it reads as written. Entries
    // of tables loaded since are forgotten: the old ones were freed.
    void watch(const std::optional<Loaded>& now, const std::map<EntryKey, std::wstring>& want);
    // Game thread: every entry put back as above (a client going away, before an unload).
    void restore(const std::optional<Loaded>& now);
    // The entry's text as the translation loaded it, if the helper holds it written.
    std::optional<std::wstring> original(EntryKey key) const;
    size_t held() const { return held_.size(); }

private:
    struct Held {
        TextTable at;  // the table it was written in
        int32_t offset = -1, room = 0;
        std::wstring original, written;
    };
    // Puts it back if `now` is the table it was written in and it still reads as written.
    static void put_back(const std::optional<Loaded>& now, const EntryKey& key, const Held& h);
    std::map<EntryKey, Held> held_;
};

// The two areas received items' popups and notices borrow for their translated text: SmallText
// entries 66 to 72, empty in every translation and never read by the game (docs/re/memory.md,
// "Borrowed entries for popups"). The run is split in two by pointing entry 70 at its middle;
// each half holds one popup's text, and the whole run one longer notice, while both are free.
class PopupAreas {
public:
    static constexpr int kFirst = 66, kMoved = 70, kNext = 73;
    static constexpr int kWait = -1;  // none free yet: the popup waits a frame
    static constexpr int kNone = -2;  // the text cannot go there: the pixel text shows
    static constexpr int kWhole = 2;
    // Game thread, before a popup's message is made: where `chars` characters will go, an area
    // (0 or 1), the whole run, kWait or kNone. A wait over kMaxWait frames gives kNone.
    int reserve(const Loaded& now, size_t chars);
    // Game thread, right after the message routine made `message`: the text written into the
    // place `where` reserved, then the message given it (hd_text_fn). Whether it was.
    bool show(const GameAddresses& a, const Loaded& now, int where, BYTE* message,
              const std::wstring& text);
    // Game thread, each frame: an area whose message has gone (its serial cleared, or its HD
    // object bound to another) is emptied, only in the table it was written in and only while it
    // reads as written; one in a table loaded since is forgotten.
    void watch(const GameAddresses& a, const std::optional<Loaded>& now);
    // Game thread (a client going away, before an unload): areas free are emptied, and entry
    // 70's offset put back once none is in use.
    void restore(const GameAddresses& a, const std::optional<Loaded>& now);
    // A message still showing reads an area: an unload waits for it.
    bool in_use() const { return areas_[0].message != nullptr || areas_[1].message != nullptr; }
    bool split() const { return run_ >= 0; }  // tests
    int32_t area_offset(int i) const { return areas_[i].offset; }

    static constexpr int kMaxWait = 240;

private:
    // The split made in `now`'s SmallText, if it is not made there already: whether it is.
    bool prepare(const Loaded& now);
    void forget();
    struct Area {
        int16_t entry = -1;
        int32_t offset = -1, room = 0;
        BYTE* message = nullptr;  // the message reading it, null while free
        int32_t serial = 0;
        BYTE* object = nullptr;  // its HD object
        std::wstring written;
        bool whole = false;  // written over the whole run
    };
    bool area_free(const GameAddresses& a, const Area& area) const;
    void empty(const Loaded& now, Area& area);
    TextTable at_;          // the SmallText the split was made in
    int32_t run_ = -1;      // entry 66's offset, the run's start (-1: no split)
    int32_t run_room_ = 0;  // the run's room, its last 0 included
    int32_t moved_from_ = -1;  // entry 70's own offset
    Area areas_[2];
    int next_ = 0;  // the area the next popup prefers
    int waited_ = 0;
};

// Game thread: the HD object of `message`, one of the four message slots; null if it is not one.
BYTE* hd_object(const GameAddresses& a, BYTE* message);
// Game thread, the flag set: the mark the game's last translated message left in `message`'s
// slot cleared, so its pixel glyphs and box show (docs/re/memory.md, "Translated messages").
void clear_mark(const GameAddresses& a, BYTE* message);

}  // namespace zap
