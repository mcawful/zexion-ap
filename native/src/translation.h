// The helper's text in the game's translations (the flag `tr_flag` set): popups and notices in
// borrowed SmallText entries (PopupAreas), names written into the tables in place (EntryWriter),
// menu labels built again with the game's builder (rebuild_label), and the pixel glyphs shown
// where a text cannot be built (show_pixel_glyphs). The tables are heap memory a pick of another
// translation frees, so each write goes only into the tables as read now, and is put back only
// while it still reads as written. Every part stands down without the translation's keys
// (game_calls.cpp, `translation_key`) or with the flag clear. Game thread only; every read is
// fault-safe and every write checked first. native/README.md, "Translations"; docs/re/memory.md,
// "Translations" and the sections after it.
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

// The translation object (`translation`): docs/re/memory.md, "Translation tables" and "The
// helper's calls in a translation".
namespace tr {
constexpr ptrdiff_t kName = 0x00;
constexpr size_t kNameSize = 32;
constexpr ptrdiff_t kSmallText = 0x30, kLargeText = 0x38, kItemNames = 0x48;
constexpr ptrdiff_t kLargeOffsets = 0x60;  // LargeText's (0x8AE21, 0x8AE35)
constexpr ptrdiff_t kSmallOffsets = 0x58, kItemNameOffsets = 0x78;
constexpr ptrdiff_t kOn = 0x91;
// 32-bit: 1 for a pixel font (Config.txt's Font Style), else an HD font.
constexpr ptrdiff_t kFontStyle = 0xA8;
constexpr int32_t kPixelFont = 1;
// 32-bit: 1 for the pixel icon sheet's icons (Config.txt's Icon Style; 0x885BF).
constexpr ptrdiff_t kIconStyle = 0xA4;
constexpr int32_t kPixelIcons = 1;
// The font the builder is given; the pointer held there is the template its glyphs are made from.
constexpr ptrdiff_t kFont = 0xD0;
// Through the font's metrics (FontMetrics).
constexpr uint64_t kSize = kFont + 0x40068;
}  // namespace tr

// `hd_messages` (one per message slot) and `message_slots`: docs/re/memory.md, "Translated
// messages".
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
// "Translated menus and the prompt").
namespace hdlabel {
constexpr ptrdiff_t kPixelShown = 0x1EC;  // 32-bit: pixel glyphs shown
constexpr ptrdiff_t kBuilt = 0x230;       // byte: the translated text was built
constexpr ptrdiff_t kGlyphs = 0x238;
constexpr ptrdiff_t kCapacity = 0x240;               // 32-bit: three per pixel glyph (0x92511)
constexpr ptrdiff_t kShown = 0x244, kCount = 0x248;  // 32-bit each
constexpr ptrdiff_t kX = 0x258, kY = 0x260;  // doubles: the place the last build was given
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

// The translation's keys are all there, the tables and the flag at their traced places.
bool translation_keys(const GameAddresses& a);
// Game thread: the translation now, if its keys are there and its pointers read; with `on`
// clear, the tables may still be loaded (the default English picked) but nothing is written.
std::optional<Loaded> loaded_translation(const GameAddresses& a);
// Game thread: the flag is set (with the keys there).
bool translation_on(const GameAddresses& a);
// Game thread: translation_on, in an HD font with the HD icons (English(HD)).
bool hd_font_on(const GameAddresses& a);
// For `hello`: "-" the default English (the flag clear), the loaded translation's name while it
// is on (spaces as underscores), or "?" (no translation keys, or nothing readable).
std::string language_name(const GameAddresses& a);
// Game thread: English(HD), the one translation the mod's text shows in mixed case (the owner's
// rule, native/README.md, "Mixed case").
bool mixed_case(const GameAddresses& a);

// LargeText entry k, as loaded now: false without large_count, with the flag clear, or unread.
bool read_large_entry(const GameAddresses& addresses, int k, std::wstring& out);
// SmallText entry k, as loaded now: false with the flag clear, or if it does not read.
bool read_small_entry(const GameAddresses& addresses, int k, std::wstring& out);

// The characters entry k may hold in place, its 0 included: up to the next higher offset, less the
// line break before it; 0 if none, or no entry follows (docs/re/memory.md, "Translation tables").
int32_t entry_room(const TextTable& t, int k);
// The text at `offset`, up to its 0 within `room` characters: false if unreadable or no 0.
bool read_text(const TextTable& t, int32_t offset, int32_t room, std::wstring& out);
// Entry k's text within its room (`last_room` for the last entry): false if none or unread.
bool read_entry(const TextTable& t, int k, std::wstring& out, int32_t last_room = 0);
// Writes `text` and a 0 at `offset`, if they fit `room` and the place can be written.
bool write_text(const TextTable& t, int32_t offset, int32_t room, std::wstring_view text);

// The helper's text for the translation's font: printable ASCII kept, anything else left out, and
// braces, which start the game's markup in a translated text (0x88897), as parentheses.
std::wstring hd_text(std::string_view font_text);
// A record's icon byte, or one of the helper's glyphs (apitem.h IconSheets), as the private-use
// character the translations' fonts draw; "" for any other (docs/re/memory.md, "Translated
// messages").
std::wstring hd_icon(uint8_t icon);
// Game thread: a client's or the helper's own text (no icon) in the translation's font, as
// hd_text: as it is in English(HD), in capitals in any other (mixed_case).
std::wstring hd_case(const GameAddresses& a, std::string_view text);

// How rebuild_label places and lays out a label's text (docs/re/memory.md, "Where a rebuilt
// label goes").
struct Rebuild {
    // Where the game's attach would build it now, its first pixel glyph less 1.0 each way (the
    // game moves a label, not its build's place); else at its last build's place.
    bool attached = false;
    // Wrapped at the label's columns; else at 320 pixels from its first pixel glyph.
    bool wrap = false;
    // Centered on x 160 plus `center_by`, as 0x85B00 centers the prompt's question (-4).
    bool center = false;
    float center_by = 0;
    // Its middle kept where the text it replaces has it, as 0x86660 does the Feats screen's title.
    bool keep_middle = false;
    // Built even if the game never built its translated text (a text object the helper made).
    bool first = false;
};
// How wide the game's builder lays a line out in the translation's font as loaded now
// (docs/re/memory.md, "The translations' icons"). For layout_popup.
class FontMetrics {
public:
    // Game thread: reads the font's scale, space and glyph table; false if they do not read.
    bool load(const GameAddresses& addresses);
    // The width of `line` (no line breaks) in pixels, or -1 if an advance does not read.
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

// A translated popup's text (a notice's when `head` is empty): `head` (the icon and the name) over
// `suffix`, lines no wider than `cap` pixels, a popup's centered on the widest with leading
// spaces. Without metrics, wrapped at `fallback` characters and not centered.
std::wstring layout_popup(const std::wstring& head, const std::wstring& suffix,
                          const FontMetrics* metrics, double cap, size_t fallback);
// `text` split at its line breaks ("" is one empty line).
std::vector<std::wstring> split_lines(const std::wstring& text);
// `lines` joined by line breaks; empty lines before the first that is not are left out.
std::wstring join_lines(const std::vector<std::wstring>& lines);
// `text`'s lines centered as layout_popup centers a popup's; as it is without metrics.
std::wstring centered_lines(const std::wstring& text, const FontMetrics* metrics);

// Game thread: builds `label`'s translated text again as `text` with the game's builder, as `how`
// says, hidden if the text it replaces was. False, nothing done, without the keys or the flag, for
// a label never built translated (unless `how.first`), or for more glyphs than the label holds.
bool rebuild_label(const GameAddresses& a, BYTE* label, std::wstring_view text,
                   const Rebuild& how = {});
// The builder's glyph count and place: tells the helper's build of a label from a later one.
struct BuiltMark {
    int32_t count = -1;
    double x = 0, y = 0;
    bool operator==(const BuiltMark&) const = default;
};
// The label's mark now (count -1 if it does not read).
BuiltMark built_mark(const BYTE* label);
// A label the helper built, still the helper's while it reads the mark taken then.
struct BuiltLabel {
    BYTE* label = nullptr;
    BuiltMark mark;
    bool holds(const BYTE* l) const { return l == label && built_mark(l) == mark; }
    void take(BYTE* l) {
        label = l;
        mark = built_mark(l);
    }
};
// The left and right of a label's translated text as 0x85B00 takes them: false if none or unread.
bool translated_extent(const GameAddresses& a, const BYTE* label, float& left, float& right);
// A label's translated glyphs: their count `n` and list, if they read and 0 <= n <= `most`.
bool translated_list(const BYTE* label, int32_t most, int32_t& n, BYTE**& list);
// Glyph `i` of a glyph list, or null.
BYTE* glyph_at(BYTE** list, int32_t i);

// Game thread: a label's pixel glyphs shown and its translated ones hidden, as the game does with
// the flag clear (0x168AE9); and the other way back. Whether the label read as expected.
bool show_pixel_glyphs(BYTE* label);
bool show_translated_glyphs(BYTE* label);
// Whether the label's translated glyphs are hidden and its pixel ones shown.
bool pixel_glyphs_shown(const BYTE* label);

// Table entries the helper holds written in place while wanted, and puts back.
using EntryKey = std::pair<Table, int>;
class EntryWriter {
public:
    // Game thread, each frame: with the flag set, each wanted entry holds its text (cut to the
    // entry's room); one no longer wanted as written is put back, only into the table it was
    // written in and only while it reads as written. One in a table freed since is forgotten.
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
    static void put_back(const std::optional<Loaded>& now, const EntryKey& key, const Held& h);
    std::map<EntryKey, Held> held_;
};

// The SmallText run 66 to 72 that popups and notices borrow (docs/re/memory.md, "Borrowed entries
// for popups"), split in two by pointing entry 70 at its middle: each half holds one popup, the
// whole run one longer notice while both are free.
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
    // Game thread, each frame: an area whose message has gone is emptied, only in the table it was
    // written in and only while it reads as written; one in a table freed since is forgotten.
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
// Game thread: the serials of the messages now showing ItemNames entry `record` (an HD object
// still bound to its slot's message, with that entry and kind 3), which the game reads again every
// frame (docs/re/memory.md, "Translated messages").
std::vector<int32_t> item_name_serials(const GameAddresses& a, int record);

}  // namespace zap
