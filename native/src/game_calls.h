// What the helper does in the game, on the game's thread: grants as one batch, the HUD refresh
// the game itself runs after loading a save, and the game's own pickup popup and sound.
//
// The client finds every address by byte pattern (client/patterns.py) and sends them as RVAs;
// nothing here is a fixed address. The functions' arguments are as traced in
// docs/re/memory.md, "Game functions the helper calls".
#pragma once

#include <windows.h>

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "menu.h"
#include "apitem.h"
#include "looks.h"
#include "skip.h"

namespace zap {

// The client's names for what it sends (client/patterns.py target names).
extern const char* const kConfigKeys[];
extern const size_t kConfigKeyCount;

struct Cas {
    uint8_t item;  // inventory byte 0..49
    uint8_t expected;
    uint8_t value;
};

struct Popup {
    uint8_t item;
    std::string suffix;  // already filtered to the font (see parse_batch)
};

struct Batch {
    std::vector<Cas> cas;
    bool refresh = false;
    std::vector<Popup> popups;
};

// Bytes at an offset into the loaded randomizer section (kSectionSize bytes at rando_blob).
struct SectionBytes {
    uint16_t offset;
    std::vector<uint8_t> bytes;
};

// What an AP new game needs: the section must already hold `expect` (the seed's mode), and
// then gets `write` (the AP item table, start, starting items). `menu` (offsets into the
// randomizer menu's state) sets the menu up as the seed's mode for the ARCHIPELAGO choice;
// without it the choice is not offered and only a RANDOMIZER game started by hand gets the
// plan.
// One room of the randomizer menu's start table: its map cell and flag (object 25 there).
struct StartRoom {
    int16_t x;
    int16_t y;
    uint8_t flag;
};

struct NewGamePlan {
    std::vector<SectionBytes> expect;
    std::vector<SectionBytes> write;
    std::vector<SectionBytes> menu;
    // The menu's start table as it builds it when it opens; for a mode with Double Teleport,
    // whose placement reads it. Filled in before the placement if the menu has not built it.
    std::vector<StartRoom> start_table;
};

constexpr size_t kStartTableMax = 64;  // the rooms the menu's builder allows

constexpr size_t kSectionSize = 0x908;
constexpr size_t kMenuSize = 0x114;  // the randomizer menu's state, as the game saves it

// "ap_newgame" followed by lines "expect OFFSET HEX", "write OFFSET HEX" (in the section) and
// "menu OFFSET HEX" (in the menu's state); OFFSET decimal or 0x hex, HEX an even number of hex
// digits; every range must lie inside its block. Also "start_table HEX": 5 bytes per room
// (x and y, 16-bit, and the flag), at most 64 rooms, cells on the 54 x 54 map, flags 0 or 1.
std::string parse_newgame(const std::string& request, NewGamePlan& out);

// A label for another player's item: the stand-in's name as laid out, and the popup's line
// width that name is laid out for (its widest line).
struct Label {
    std::string name;
    int width = 0;
    // For another Zexion player's item: the item's record, whose icon and name the first line
    // is made of once the records are loaded (until then the name says AP ITEM), and the
    // second line.
    int item = -1;
    std::string who;
};

// A label's popup layout: `first` (which starts with its glyph) and `who`, each centered in
// the longer, which is the width.
Label layout_label(const std::string& first, const std::string& who);

// Words of `text` in lines of at most `width` characters; a word longer than a line is cut.
std::vector<std::string> wrap_words(const std::string& text, size_t width);
// `lines` as one popup text: with more than one line, each centered in per_line characters and
// padded to the end of its line; one line as it is.
std::string center_lines(const std::vector<std::string>& lines, int per_line);
// A received item's popup text: `head` (the icon and the game's name), then `suffix` ("FROM
// ALICE") from the next line, its words wrapped at per_line characters, every line centered in
// the widest line, whose width goes to `*width` (the popup's line width: the game sizes a box
// to it).
std::string popup_text(const std::string& head, const std::string& suffix, int per_line,
                       int* width = nullptr);

// "ap_labels" followed by lines "label X Y HEX": the name, as hex bytes, for the stand-in
// item at map cell (x, y), both 0 to 53. Names keep only what the game's font draws
// (letters made upper-case, digits, space and . , ' ! ? - : ( ) / &) and at most 49 bytes.
// Returns "" or an error.
std::string parse_labels(const std::string& request, std::map<int, Label>& out);

// "config name=rva name=rva ...", RVAs in hex (0x...) or decimal. Returns "" or an error.
std::string parse_config(const std::string& request, std::map<std::string, uint64_t>& out);

// "apply" followed by lines "cas ITEM EXPECTED VALUE", "refresh", "popup ITEM SUFFIX".
// Returns "" or an error. The suffix keeps only what the game's font draws: letters (made
// upper-case), digits, space and . , ' ! ? -, at most 40 characters.
std::string parse_batch(const std::string& request, Batch& out);

class Game {
public:
    // Checks every key is present and lies inside `image` (functions in executable code, data
    // in writable data, with room for the whole player state around the inventory), then keeps
    // the addresses. Game thread. Returns "" or an error.
    std::string configure(HMODULE image, const std::map<std::string, uint64_t>& rvas);
    bool configured() const { return configured_; }

    // Game thread. In play (a save loaded, no menu, energy above 0) or refused; then every
    // compare-and-set or none; then (after writes, or for "refresh") missiles, bombs and energy
    // as the pickup routine sets them from the counts, and the HUD refresh; Magnus Armor's suit
    // color if it was just gained; and the popups queued.
    // Returns "applied", "refused ..." or "error ...".
    std::string apply(const Batch& batch);

    // Game thread, every frame: shows the next queued popup, with its sound, once the game's
    // last popup has ended. A popup the game could not take stays queued.
    void pump_popups();

    size_t queued_popups() const { return popups_.size(); }

    // Game thread. Arms the AP new game: the next randomizer new game the game is about to
    // build (its new-game routine queued, the randomizer marker set) gets `plan`, once.
    void arm_newgame(NewGamePlan plan);
    void disarm_newgame();
    // "armed", "idle", or the last outcome: "applied slot N" or "refused ...".
    std::string newgame_status() const;

    // Game thread, every frame: applies an armed plan when a randomizer new game is queued and
    // its timer has more than one frame left. A section that does not hold the plan's
    // `expect` bytes (another mode) is left as the game made it, and the plan is dropped.
    void watch_newgame();

    // Game thread, every frame: while a plan with menu lines is armed, offers ARCHIPELAGO in
    // place of RANDOMIZER on the game-mode panel, its callback `callback`; otherwise puts
    // RANDOMIZER back.
    void watch_menu(WidgetCallback callback);
    // Game thread: puts RANDOMIZER back now (before an unload).
    void restore_menu(WidgetCallback callback);
    // Whether a menu holds the helper's callback (ARCHIPELAGO). The locked ASSIST holds none.
    bool menu_installed() const { return choice_.installed(); }

    // The options menu's ASSIST entry (menu.h, AssistLock): set_assists_locked arms it (the
    // `assists` request); the watch runs every frame, locking it while armed and unlocking it
    // otherwise; restore_assists unlocks it now (before an unload).
    void set_assists_locked(bool locked) { assists_locked_ = locked; }
    bool assists_locked() const { return assists_locked_; }
    void watch_assists();
    void restore_assists();
    bool assist_lock_installed() const { return assists_.installed(); }

    // Game thread, from ARCHIPELAGO's callback: sets the randomizer menu's state to the plan's
    // mode, writes the plan (for the AP start), runs the game's placement, checks the section
    // is the seed's mode, writes the plan over the placement, puts the menu's state back, and
    // queues the new game in the chosen slot; watch_newgame reports "applied slot N" in the
    // next frame. A section of another mode, or a slot the game will not start a game in,
    // drops the plan with "refused ..." and nothing queued.
    void start_archipelago(WidgetCallback callback);

    // Game thread, every frame (checked every 30th): offers the escape skip in this game
    // (skip.h). restore_skip puts the event back (before an unload).
    void watch_skip();
    void restore_skip();
    bool skip_offered() const { return skip_.patched(); }

    // Game thread. Other players' items (apitem.h): set_labels arms it; the watch runs every
    // frame; restore_ap_items puts everything back (before an unload).
    void set_labels(std::map<int, Label> labels);
    void watch_ap_items();
    void restore_ap_items();
    // The stand-ins restyled, or the pickup popup's line width changed: unload must wait for
    // a frame to put them back.
    // Game thread, every frame: notes how long the player has been in play in one cell.
    void note_play(uint64_t now_ms);
    // Game thread (the `shield` request): spawns a received Energy Shield the way its pickup
    // does, once the player has been in play in one cell for a second: "spawned", or
    // "refused <why>".
    std::string spawn_shield(uint64_t now_ms);
    // Game thread (the `die` request, a received DeathLink): kills the player as the game's
    // damage code does, once the player has been in play in one cell for a second and no
    // message is showing: "died", or "refused <why>".
    std::string kill(uint64_t now_ms);

    bool ap_items_restyled() const {
        return ap_items_.restyled() || (popup_width_ > 0 && popup_width_ != kGamePopupWidth) ||
               looks_.dressed();
    }
    int popup_width() const { return popup_width_; }  // tests
    void set_gl(const GlFunctions& gl) { gl_ = gl; }  // tests

private:
    bool in_play() const;
    BYTE* player() const;
    BYTE* record(uint8_t item) const;
    bool popup_busy() const;
    void refresh_hud();

    bool configured_ = false;
    BYTE* base_ = nullptr;
    BYTE* inventory_ = nullptr;
    BYTE* save_loaded_ = nullptr;
    BYTE* menu_state_ = nullptr;
    BYTE* item_records_ = nullptr;  // holds a pointer to the records
    BYTE* last_message_ = nullptr;  // holds a pointer; the sequence number follows at +8
    BYTE* weapon_strip_gate_ = nullptr;
    void* sound_system_ = nullptr;
    void* message_fn_ = nullptr;
    void* sound_fn_ = nullptr;
    void* hud_number_fn_ = nullptr;
    void* hud_layout_fn_ = nullptr;
    void* energy_hud_fn_ = nullptr;
    void* weapon_strip_fn_ = nullptr;
    void* suit_color_fn_ = nullptr;
    std::deque<Popup> popups_;
    int frames_since_popup_ = 1 << 20;

    BYTE* rando_blob_ = nullptr;
    BYTE* rando_marker_ = nullptr;
    BYTE* transition_fn_ = nullptr;     // holds the routine the game runs when its timer ends
    BYTE* transition_timer_ = nullptr;  // 32-bit frames left
    BYTE* current_slot_ = nullptr;      // 32-bit, 0-based
    void* newgame_fn_ = nullptr;
    bool newgame_armed_ = false;
    NewGamePlan newgame_;
    std::string newgame_status_ = "idle";

    MenuAddresses menu_;
    ArchipelagoChoice choice_;
    AssistLock assists_;
    bool assists_locked_ = false;
    FontGlyph lock_glyph_{kLockGlyph, kLockRows};
    BYTE* randomizer_menu_ = nullptr;
    void* placement_fn_ = nullptr;
    void* rando_newgame_fn_ = nullptr;
    bool starting_ = false;  // ARCHIPELAGO pressed; the new game it queued not yet applied

    BYTE* event_table_ = nullptr;  // holds the pointer to the loaded area events
    BYTE* event_count_ = nullptr;
    BYTE* start_count_ = nullptr;  // 16-bit
    BYTE* start_cells_ = nullptr;  // (x, y) 16-bit each
    BYTE* start_flags_ = nullptr;
    // The start table: filled from the plan if the game has not built it, or checked against
    // it. Returns "" or why not.
    std::string ensure_start_table();
    EscapeSkip skip_;
    BYTE* sprite_texture_ = nullptr;  // holds the pointer to the sprite data
    ApItems ap_items_;
    // The pickup popup's line width: `mov edx, 24` in the game's pickup code, the path items
    // with no icon take (the stand-ins do), changed to a label's width while the player stands
    // in its cell (docs/re/memory.md, "Other players' items").
    static constexpr int kGamePopupWidth = 24;
    BYTE* popup_width_code_ = nullptr;
    BYTE* item_pool_ = nullptr;
    BYTE* palette_table_ = nullptr;
    BYTE* frame_clock_ = nullptr;
    BYTE* sprite_templates_ = nullptr;
    BYTE* event_handlers_ = nullptr;  // the area-event handlers, by event type
    // Runs a received item's pickup event (the Energy Shield's): "" or why not.
    std::string run_item_event(uint8_t item);
    int play_cell_ = -1;
    uint64_t play_since_ms_ = 0;
    void* death_fn_ = nullptr;
    BYTE* halt_flag_ = nullptr;
    int popup_width_ = -1;  // what the game holds, once read; -1 before
    std::map<int, int> label_widths_;
    std::map<int, Label> item_labels_;  // labels still to be made from an item's record
    ItemLooks looks_;  // other Zexion players' items shown as themselves
    ItemLooks::Places looks_places() const;
    void resolve_item_labels();
    void set_popup_width(int width);
    GlFunctions gl_;
    int skip_frames_ = 0;  // frames until the next escape-skip check
};

}  // namespace zap
