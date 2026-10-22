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
struct NewGamePlan {
    std::vector<SectionBytes> expect;
    std::vector<SectionBytes> write;
    std::vector<SectionBytes> menu;
};

constexpr size_t kSectionSize = 0x908;
constexpr size_t kMenuSize = 0x114;  // the randomizer menu's state, as the game saves it

// "ap_newgame" followed by lines "expect OFFSET HEX", "write OFFSET HEX" (in the section) and
// "menu OFFSET HEX" (in the menu's state); OFFSET decimal or 0x hex, HEX an even number of hex
// digits; every range must lie inside its block.
std::string parse_newgame(const std::string& request, NewGamePlan& out);

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
    bool menu_installed() const { return choice_.installed(); }

    // Game thread, from ARCHIPELAGO's callback: sets the randomizer menu's state to the plan's
    // mode, writes the plan (for the AP start), runs the game's placement, checks the section
    // is the seed's mode, puts the menu's state back, and queues the new game in the chosen
    // slot; watch_newgame then writes the plan over the placement in the next frame and
    // reports "applied slot N". A section of another mode, or a slot the game will not start
    // a game in, drops the plan with "refused ..." and nothing queued.
    void start_archipelago(WidgetCallback callback);

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
    BYTE* randomizer_menu_ = nullptr;
    void* placement_fn_ = nullptr;
    void* rando_newgame_fn_ = nullptr;
    bool starting_ = false;  // ARCHIPELAGO pressed; the new game it queued not yet applied
};

}  // namespace zap
