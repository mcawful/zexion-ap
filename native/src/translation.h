// The game's translations (docs/re/memory.md, "Translations" and the sections after it): with a
// translation on (English(HD), French, Japanese and the rest; the flag `tr_flag`), the game still
// builds its pixel-font text but draws the translation's own text, in the translation's font, in
// its place. What the helper writes into the pixel glyphs then does not show, and its popups were
// hidden by the mark the game's last translated message left in their slot. This is how the
// helper's text shows in a translation:
//
// - Received items' popups and notices (PopupAreas): the text is written into SmallText entries
//   the game never shows, and the message given it as the game gives GAME SAVED its text
//   (hd_text_fn). Emptied once the message has gone.
// - Names the game reads from the tables while they show (EntryWriter): the stand-ins' names
//   (ItemNames 47 and 48) for the pickup popup, INTEL CHIP (49), the scanner's line (SmallText
//   12), written in place, within the room the entry has, and put back.
// - Menu labels the helper rewrites (rebuild_label): their translated text built again with the
//   game's own builder (hd_build_fn), placed, wrapped and centered as the game does that label.
// - The Feats screen's title where its translated text cannot be built (no large_count, or too
//   little room): the game's own swap back to the pixel glyphs (show_pixel_glyphs, as 0x168AE9
//   does), which hold the note.
// - The helper's icons, painted into the translations' icon sheets (apitem.h IconSheets) and
//   named by private-use characters (hd_icon).
//
// The tables are the translation's heap memory, freed and loaded again when the player picks
// another translation (a pick of the default English only clears the flag). So each write is made
// only into the tables as they are now, checked against the pointers and count read when its
// state was made, and an entry is put back only while it still reads as the helper wrote it.
//
// Every part stands down without the translation's keys (an offline cache may lack them:
// game_calls.cpp, kConfigKeys, `translation_key`) or with the flag clear, which leaves the
// default English alone. Game thread only; every read is fault-safe and every write checked first.
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "addresses.h"

namespace zap {

// The translation object (`translation`, docs/re/memory.md, "Translation tables"): the loaded
// folder's name, the tables' pointers, the flag (the byte `tr_flag` is), and the font the
// translated text is built in ("The helper's calls in a translation").
namespace tr {
constexpr ptrdiff_t kName = 0x00;
constexpr size_t kNameSize = 32;
constexpr ptrdiff_t kSmallText = 0x30, kLargeText = 0x38, kItemNames = 0x48;
constexpr ptrdiff_t kLargeOffsets = 0x60;  // LargeText's (0x8AE21, 0x8AE35)
constexpr ptrdiff_t kSmallOffsets = 0x58, kItemNameOffsets = 0x78;
constexpr ptrdiff_t kOn = 0x91;
// 32-bit: 1 for a translation whose font is drawn as pixels (Config.txt's "Font Style" 1,
// inferred from its uses: docs/re/memory.md, "Where a rebuilt label goes"), else an HD font.
constexpr ptrdiff_t kFontStyle = 0xA8;
constexpr int32_t kPixelFont = 1;
// 32-bit: 1 for the icons from the pixel icon sheet (Config.txt's "Icon Style" 1), as the builder
// takes them (0x885BF): every translation but English(HD) has both styles at 1, it both at 0.
constexpr ptrdiff_t kIconStyle = 0xA4;
constexpr int32_t kPixelIcons = 1;
// The font the builder is given (its address; the template its glyphs are made from, the pointer
// held there).
constexpr ptrdiff_t kFont = 0xD0;
// Through the font's metrics (its glyph table, space and scale: FontMetrics).
constexpr uint64_t kSize = kFont + 0x40068;
}  // namespace tr

// The HD message objects (`hd_messages`, one per message slot) and the message slots
// (`message_slots`) (docs/re/memory.md, "Translated messages").
namespace hd {
constexpr size_t kSize = 0x238;
constexpr int kSlots = 4;
constexpr ptrdiff_t kEntry = 0x20C, kKind = 0x210;  // 32-bit each
constexpr int32_t kItemNameKind = 3;  // the entry an ItemNames record, as the pickup's
constexpr ptrdiff_t kMessage = 0x220, kSerial = 0x228;
constexpr ptrdiff_t kHasText = 0x22C;  // byte: the mark that hides a message's pixel glyphs
}  // namespace hd
namespace message {
constexpr size_t kSize = 0x1628;
constexpr ptrdiff_t kSerial = 0x1D8, kSlot = 0x1E0;  // 32-bit each
}  // namespace message

// A label's translated glyphs, beside its pixel ones (label::, game_layout.h; docs/re/memory.md,
// "Translated menus and the prompt"): the pixel glyphs shown, whether the translated text was
// built, its glyphs, the most it can hold (three per pixel glyph, 0x92511), how many are shown and
// how many there are, and the place its last build was given (doubles, 0x88480 at 0x8869F).
namespace hdlabel {
constexpr ptrdiff_t kPixelShown = 0x1EC;  // 32-bit
constexpr ptrdiff_t kBuilt = 0x230;       // byte
constexpr ptrdiff_t kGlyphs = 0x238;
constexpr ptrdiff_t kCapacity = 0x240;               // 32-bit
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
    bool on = false;  // the flag: a translation's text shows
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
// Game thread: the translation shows (translation_on) in an HD font with the HD icons, as
// English(HD) does; the other translations draw their text and icons as pixels.
bool hd_font_on(const GameAddresses& a);
// For `hello`: "-" the default English (the flag clear), the loaded translation's name while it
// is on (spaces as underscores), or "?" (no translation keys, or nothing readable).
std::string language_name(const GameAddresses& a);

// LargeText entry k, as loaded now (the translation object's table, bounded by large_count):
// false without that key, with the flag clear, or if it does not read.
bool read_large_entry(const GameAddresses& addresses, int k, std::wstring& out);
// SmallText entry k, as loaded now: false with the flag clear, or if it does not read.
bool read_small_entry(const GameAddresses& addresses, int k, std::wstring& out);

// The characters entry k may hold in place, its ending 0 included: from its offset to the next
// higher offset of any entry, less the line break before that one (docs/re/memory.md,
// "Translation tables"; the parser never reads the lines between two texts again). 0 if it has
// none, or no entry follows it.
int32_t entry_room(const TextTable& t, int k);
// The text at `offset`, up to its 0 within `room` characters: false if unreadable or no 0 is
// there.
bool read_text(const TextTable& t, int32_t offset, int32_t room, std::wstring& out);
// Entry k's text, read within its room (`last_room` for the last entry, whose room is not
// known): false if it has none or it does not read.
bool read_entry(const TextTable& t, int k, std::wstring& out, int32_t last_room = 0);
// Writes `text` and a 0 at `offset`, if they fit `room` and the place can be written.
bool write_text(const TextTable& t, int32_t offset, int32_t room, std::wstring_view text);

// The helper's text, as the translation's font draws it: the 8 x 8 font's characters kept (the
// helper's and the client's texts hold no others), anything else left out, and braces, which
// start the game's markup in a translated text (0x88897), as parentheses.
std::wstring hd_text(std::string_view font_text);
// A record's icon byte as the private-use character the translations' fonts draw it as, as the
// pickup does (docs/re/memory.md, "Translated messages"), and the helper's own glyphs (the disk,
// the padlock, the AP logo) as the characters it paints them into the icon sheets as (apitem.h
// IconSheets); "" for none or any other.
std::wstring hd_icon(uint8_t icon);
// A stand-in's label as laid out (`width` characters a line, its first character the item's
// glyph; text.h, Label) as translated text: each line trimmed, joined by line breaks, the glyph
// as hd_icon (left out without `icon`); the first line alone with `first_line`, as the map
// cursor names it (with no icon, as the game's map names and the pixel font's map_name).
std::wstring hd_label(const std::string& name, int width, bool first_line = false,
                      bool icon = true);

// How rebuild_label places and lays out a label's text (docs/re/memory.md, "Translated menus and
// the prompt", "Where a rebuilt label goes").
struct Rebuild {
    // Where the game's attach (text_attach_fn) would build a menu label's text now: the label's
    // first pixel glyph's place less 1.0 each way. The game moves a label after its build without
    // moving the place the build was given. Else at the place of the label's last build: for a
    // label the game has just built where it stays (the equipment screen's, per selection).
    bool attached = false;
    // Wrapped at the label's columns, as the equipment screen's description; else at 320 pixels
    // from its first pixel glyph.
    bool wrap = false;
    // Centered on x 160 plus `center_by`, as 0x85B00 centers the prompt's question (-4).
    bool center = false;
    float center_by = 0;
    // Its middle kept where the text it replaces has it, as 0x86660 centers the Feats screen's
    // title on a y.
    bool keep_middle = false;
    // Built even if the game never built the label's translated text (a text object the helper
    // made, as the title's lines, whose label has its room all the same).
    bool first = false;
};
// How wide the game's builder lays a line out in the translation's font as loaded now
// (docs/re/memory.md, "The translations' icons"): a space by the font's space advance, an icon
// by its frame's advance in the icon sheet, any other character by its glyph's advance, one with
// no glyph (-1 in the font's table) not at all. For the popups' layout (layout_popup).
class FontMetrics {
public:
    // Game thread: reads the font's scale, space and glyph table; false if they do not read.
    bool load(const GameAddresses& addresses);
    // The width of `line` (no line breaks) in pixels, or -1 if a character's advance does not
    // read.
    double width(std::wstring_view line) const;
    double space() const { return space_; }

private:
    double advance(wchar_t c) const;  // -1 if it does not read

    const BYTE* font_ = nullptr;
    const BYTE* glyphs_ = nullptr;  // the font's glyph entries
    const BYTE* icon_frames_ = nullptr;  // the icon sheet's frame list
    bool pixel_font_ = false;
    double scale_ = 0, factor_ = 0, space_ = 0;
};

// A translated popup's text, laid out (a notice's when `head` is empty): `head` (the icon and the
// name) over `suffix`, every line no wider than `cap` pixels (words whole where they fit), each
// line of a popup centered with leading spaces on the widest; a notice's lines left as they
// wrap. Without metrics (null), wrapped at `fallback` characters and not centered.
std::wstring layout_popup(const std::wstring& head, const std::wstring& suffix,
                          const FontMetrics* metrics, double cap, size_t fallback);
// `text` split at its line breaks ("" is one empty line).
std::vector<std::wstring> split_lines(const std::wstring& text);
// `lines` joined by line breaks; empty lines before the first that is not are left out.
std::wstring join_lines(const std::vector<std::wstring>& lines);
// `text`'s lines centered on its widest with leading spaces, as layout_popup centers a popup's
// (the stand-ins' names, which the pickup popup shows from ItemNames); as it is without metrics.
std::wstring centered_lines(const std::wstring& text, const FontMetrics* metrics);
// A suffix in the translation's font, as the client sent it: "FROM Bob" as "from Bob".
std::wstring hd_suffix(const std::string& as_sent);

// Game thread, the flag set: builds `label`'s translated text again as `text`, with the game's
// builder, as `how` says, hidden if the text it replaces was (a hidden widget's). False, nothing
// done, without the keys, with the flag clear, for a label whose translated text was never built
// (unless `how.first`), or for a text with more glyphs than the label holds.
bool rebuild_label(const GameAddresses& a, BYTE* label, std::wstring_view text,
                   const Rebuild& how = {});
// What tells a label's translated text built by the helper from one the game built again since:
// its glyph count and the place its build was given, both written by the builder.
struct BuiltMark {
    int32_t count = -1;
    double x = 0, y = 0;
    bool operator==(const BuiltMark&) const = default;
};
// The label's mark now (count -1 if it does not read).
BuiltMark built_mark(const BYTE* label);
// The label whose translated text the helper built, and its mark then: it is still the helper's
// while the label reads that mark.
struct BuiltLabel {
    BYTE* label = nullptr;
    BuiltMark mark;
    bool holds(const BYTE* l) const { return l == label && built_mark(l) == mark; }
    void take(BYTE* l) {
        label = l;
        mark = built_mark(l);
    }
};
// The left and right of a label's translated text, as the game's centering takes them (0x85B00):
// false if it has none or it does not read.
bool translated_extent(const GameAddresses& a, const BYTE* label, float& left, float& right);
// A label's translated glyphs: their count `n` and list, if they read and 0 <= n <= `most`.
bool translated_list(const BYTE* label, int32_t most, int32_t& n, BYTE**& list);
// Glyph `i` of a glyph list, or null.
BYTE* glyph_at(BYTE** list, int32_t i);

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
    // The text the helper wrote there, if it holds it written.
    std::optional<std::wstring> written(EntryKey key) const;
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
    void watch(const std::optional<Loaded>& now);
    // Game thread (a client going away, before an unload): areas free are emptied, and entry
    // 70's offset put back once none is in use.
    void restore(const std::optional<Loaded>& now);
    // A message still showing reads an area: an unload waits for it.
    bool in_use() const { return areas_[0].message != nullptr || areas_[1].message != nullptr; }
    bool split() const { return run_ >= 0; }

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
        std::wstring written;  // empty for area 1 while the whole run is area 0's
    };
    static bool area_free(const Area& area);
    void empty(const Loaded& now, Area& area);
    TextTable at_;          // the SmallText the split was made in
    int32_t run_ = -1;      // entry 66's offset, the run's start (-1: no split)
    int32_t run_room_ = 0;  // the run's room, its last 0 included
    int32_t moved_from_ = -1;  // entry 70's own offset
    Area areas_[2];
    int next_ = 0;  // the area the next popup prefers
    int waited_ = 0;
};

// Game thread, the flag set: the mark the game's last translated message left in `message`'s
// slot cleared, so its pixel glyphs and box show (docs/re/memory.md, "Translated messages").
void clear_mark(const GameAddresses& a, BYTE* message);
// Game thread: whether a message showing reads ItemNames entry `record` (the pickup's): an HD
// object still bound to its slot's message (its serial the slot's) with that entry, kind 3. The
// game reads the entry again every frame while it shows (docs/re/memory.md, "Translated messages").
bool item_name_shown(const GameAddresses& a, int record);

}  // namespace zap
