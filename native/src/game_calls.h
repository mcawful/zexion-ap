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
// then gets `write` (the AP item table, start, starting items).
struct NewGamePlan {
    std::vector<SectionBytes> expect;
    std::vector<SectionBytes> write;
};

constexpr size_t kSectionSize = 0x908;

// "ap_newgame" followed by lines "expect OFFSET HEX" and "write OFFSET HEX" (OFFSET decimal or
// 0x hex, HEX an even number of hex digits); every range must lie inside the section.
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
};

}  // namespace zap
