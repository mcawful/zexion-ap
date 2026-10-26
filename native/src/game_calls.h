// Game: what the helper does in the game, on the game's thread. It keeps the addresses the
// client resolved (configure) and runs each part from the frame call: grants as one batch, with
// the HUD refresh the game runs after loading a save and the game's own pickup popup and sound;
// the AP new game and ARCHIPELAGO (menu.h); the escape skip (skip.h); other players' items
// (apitem.h, looks.h, mapmarks.h); the ASSIST lock (menu.h); Intel Chips (intel.h); the OS
// Install Disks goal (scan.h, equip.h, pause.h, files.h, mission.h); the title screen's lines
// (title.h); and AP saves without the client (offline.h, game_offline.cpp). The pipe's requests
// are parsed in requests.h, and the popups' text laid out in text.h.
//
// The client finds every address by byte pattern (client/patterns.py) and sends them as RVAs;
// nothing here is a fixed address. The functions' arguments are as traced in
// docs/re/memory.md, "Game functions the helper calls".
#pragma once

#include <windows.h>

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "game_mem.h"
#include "menu.h"
#include "apitem.h"
#include "looks.h"
#include "equip.h"
#include "mission.h"
#include "pause.h"
#include "files.h"
#include "intel.h"
#include "offline.h"
#include "requests.h"
#include "scan.h"
#include "mapmarks.h"
#include "title.h"
#include "skip.h"
#include "text.h"

namespace zap {

// What clients set that Game::put_back_all drops, for a refused unload to set again
// (Game::snapshot and Game::rearm). The offline look's own settings are left out: once the
// unload is refused, watch_offline dresses the save again by itself.
struct ClientState {
    bool assists_locked = false;
    bool intel = false;
    int disc_count = 0, discs_required = 0;
    bool discs_told = true;
    std::string scan_line;
    std::map<int, int> file_counts;  // the client's disk_slots
    std::map<int, Label> labels;
    std::optional<OfflineSeed> labels_seed;
    bool newgame_armed = false;
    bool starting = false;
    NewGamePlan newgame;
    std::string newgame_status;
    bool mission_asked = false;   // RETURN HOME WITH THE DISKS? on its way
    bool mission_ending = false;  // its YES pressed, the ending not started yet
};

// The helper's own functions the game is pointed at while a part is on (dllmain.cpp), each in
// place of the game's: the game-mode panel's handler (menu.h), EQUIPMENT's callback and the
// Energy Tank panel's handler (equip.h), the mission prompt's opener and YES (mission.h), the
// save entries' handler (files.h) and the map's handler (mapmarks.h). Given before configure,
// which hands each part its own.
struct Hooks {
    void* panel = nullptr;
    void* equipment = nullptr;
    void* equipment_panel = nullptr;
    void* mission_open = nullptr;
    void* mission_yes = nullptr;
    void* file_entry = nullptr;
    void* map = nullptr;
};

class Game {
public:
    // Checks every required key is present (the title screen's three and map_handler may be
    // missing) and each given one lies inside `image` (functions in executable code, data
    // in writable data, with room for the whole player state around the inventory), then keeps
    // the addresses. Game thread. Returns "" or an error.
    // `active`: a client sent it (false: the offline cache, offline.h).
    std::string configure(HMODULE image, const std::map<std::string, uint64_t>& rvas,
                          bool active = true);
    bool configured() const { return configured_; }
    // Whether a client is there: set by `config`, cleared when the client goes away. Without
    // one, nothing the helper does on its own (the escape skip) runs, except while the offline
    // look dresses a save (offline_skip_).
    bool active() const { return active_; }
    void set_active(bool active) { active_ = active; }
    // Popups not shown yet dropped (the client that sent them went away).
    void clear_popups() { popups_.clear(); }
    // Before configure: the helper's stand-ins the game is pointed at (Hooks).
    void set_hooks(const Hooks& hooks) { hooks_ = hooks; }

    // Game thread: everything the helper changed in the game put back, as far as a frame can (a
    // change held by a menu no longer open is gone with it): the new-game plan, the escape skip,
    // the OS Install Disks goal's changes, the Area Map's name, the stand-in items and their
    // looks, the map's handler, ARCHIPELAGO (`callback` its callback), the ASSIST lock and the
    // offline look. Unload and a client going away both use it.
    void put_back_all(WidgetCallback callback);
    // Game thread: what clients set, before put_back_all drops it, and set again after it (an
    // unload refused).
    ClientState snapshot() const;
    void rearm(ClientState state);

    // Game thread. In play (a save loaded, no menu, energy above 0) or refused; then every
    // compare-and-set or none; then (after writes, or for "refresh") missiles, bombs and energy
    // as the pickup routine sets them from the counts, and the HUD refresh; Magnus Armor's suit
    // color if it was just gained; and the popups queued.
    // Returns "applied" or "refused ...".
    std::string apply(const Batch& batch);

    // Game thread, every frame: shows the next queued popup, with its sound, once the game's
    // last popup has ended. A popup the game could not take stays queued.
    void pump_popups();

    size_t queued_popups() const { return popups_.size(); }
    // The client's popups still queued (Popup::counted), for the `popups` request: one leaves
    // the queue when shown, dropped for want of a record, or dropped with the client.
    size_t pending_popups() const;

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
    // Whether the game may still hold one of the helper's functions, which unload must take
    // back first: ARCHIPELAGO's callback or the game-mode panel's handler, EQUIPMENT's callback
    // or the Energy Tank panel's handler, the mission prompt's opener or YES, a file entry's
    // handler or the map's. The locked ASSIST holds none.
    bool menu_installed() const {
        return choice_.installed() || choice_.holds_hook() || equip_.installed() ||
               equip_screen_.holds_hook() || mission_.holds_hook() || files_.holds_hook() ||
               map_names_.holds_hook();
    }
    // The game's own handler for the game-mode panel, for the stand-in to call; null before
    // configure.
    void* panel_handler() const { return menu_.panel_handler; }
    // Game thread, from the stand-in after the game's handler: fits ARCHIPELAGO's hover border.
    void fit_archipelago_border() {
        if (configured_ && choice_.installed()) choice_.fit_border(menu_);
    }

    // The options menu's ASSIST entry (menu.h, AssistLock): set_assists_locked arms it (the
    // `assists` request); the watch runs every frame, locking it while armed and unlocking it
    // otherwise; restore_assists unlocks it now (before an unload).
    void set_assists_locked(bool locked) { assists_locked_ = locked; }
    bool assists_locked() const { return assists_locked_; }
    void watch_assists();
    void restore_assists();

    // Game thread, from ARCHIPELAGO's callback: sets the randomizer menu's state to the plan's
    // mode, writes the plan (for the AP start), runs the game's placement, checks the section
    // is the seed's mode, writes the plan over the placement, puts the menu's state back, and
    // queues the new game in the chosen slot; watch_newgame reports "applied slot N" in the
    // next frame. A section of another mode, or a slot the game will not start a game in,
    // drops the plan with "refused ..." and nothing queued.
    void start_archipelago(WidgetCallback callback);

    // Game thread, every frame (checked every 31st): offers the escape skip in this game
    // (skip.h). restore_skip puts the event back (before an unload).
    void watch_skip();
    void restore_skip();
    bool skip_offered() const { return skip_.patched(); }

    // The OS Install Disks goal (scan.h; the `discs` request): set_discs gives the count to show
    // and the scanner's line ("" turns it off); the watch runs every frame (checked every 31st),
    // rewording the scanner's message while a line is set and putting it back otherwise;
    // restore_discs puts it back now (before an unload).
    void set_discs(int count, int required, std::string scan_line, bool told = true);
    void watch_discs();
    void restore_discs();
    bool scan_written() const { return scan_.written(); }
    int disc_count() const { return disc_count_; }
    bool discs_told() const { return discs_told_; }
    bool keys_branch_patched() const { return pause_row_.patched(); }
    int discs_required() const { return discs_required_; }
    const std::string& scan_line() const { return scan_line_; }
    // The count on the equipment screen (equip.h): every frame, the watch points EQUIPMENT at
    // the helper's stand-in (Hooks) while the count is shown (and back otherwise); the stand-in
    // calls the game's, then equipment_built.
    void* equipment_fn() const { return equip_at_.equipment_fn; }
    void watch_equipment();
    void equipment_built();
    // The Energy Tank panel's handler while it shows the disk (equip.h): the helper's stand-in
    // calls the game's (equip_panel_fn), then equipment_event.
    void* equip_panel_fn() const { return equip_at_.equip_panel_fn; }
    void equipment_event(void* widget, intptr_t event);
    // RETURN HOME WITH THE DISKS? (mission.h): the `mission_prompt` request asks for it; the
    // watch runs every frame; the helper's opener and YES call mission_open and mission_yes;
    // restore_mission puts back what it holds (before an unload).
    void request_mission() { mission_.request(); }
    // The file select's disk count (files.h): the `disk_slots` request sets what to show; the
    // watch runs every frame; the helper's stand-in for the save entries' handler calls the
    // game's (file_entry_original) and then file_event.
    void set_disk_slots(int count, std::set<int> slots) {
        client_file_counts_.clear();
        for (int slot : slots) client_file_counts_[slot] = count;
        apply_file_counts();
    }
    void set_disk_slot_counts(std::map<int, int> counts) {
        client_file_counts_ = std::move(counts);
        apply_file_counts();
    }
    void watch_files();
    void* file_entry_original(void* widget) const { return files_.original(widget); }
    const FileDisks& files() const { return files_; }
    void file_event(void* widget, intptr_t event);
    void restore_files();

    // Game thread: the Area Map named INTEL CHIP, its map reveal off, while on (`intel`,
    // intel.h), and put back.
    void set_intel(bool on) { intel_.set(on); }

    // Game thread: AP saves without the client (offline.h). The seeds the client left, read
    // from its files; each frame, while no client is there, a save of one of them is given its
    // look, and the file select their disk counts. `offline_state` for `hello`: "-" with none,
    // else how many, then ":" and the file of the one loaded.
    void set_offline(std::vector<OfflineSeed> seeds);
    void watch_offline();
    void leave_offline();
    std::string offline_state() const;
    // Game thread (`other_save`, from a client whose session sees a randomizer save in play
    // that is not its seed's): THE ARCHIPELAGO CLIENT IS FOR ANOTHER SEED. once a load and
    // again after a reconnect (whether it was queued now), and the save's offline look if the
    // helper has its seed's file, until the save goes.
    bool other_save();
    // Game thread (`client_back`, from a client whose session has seen its seed's save in
    // play): if that save was played offline, ARCHIPELAGO CLIENT CONNECTED. pops up, once.
    // Whether it did.
    bool client_back();
    bool intel_on() const { return intel_.on(); }
    void watch_intel();
    // Game thread: the title screen's mod lines (title.h): the version they name, the watch
    // (configured, client or not), and hidden before an unload. `title_state` for `hello`:
    // "1" up, "0" not, else why they could not be made.
    void set_title_version(const std::string& version) { title_.set_version(version); }
    void watch_title();
    // Game thread, every few frames: the progression stand-in's map squares drawn as important
    // in an Archipelago save (mapmarks.h): one with labels, from the client or the offline look.
    void watch_map_marks();
    // Game thread: the map cursor's name for the stand-ins' squares (mapmarks.h): the open map's
    // handler wrapped while this save's labels are known (each frame), the name rewritten after
    // the game's handler (`map_after`, from the stand-in), and the handler put back.
    void watch_map_names();
    void map_after();
    void* map_original(void* child) const;
    void restore_map_names();
    void hide_title();
    std::string title_state() const;
    void restore_intel();
    void watch_mission();
    void mission_open();
    void* mission_yes() { return mission_.yes(); }
    void restore_mission();
    std::string mission_status() const { return mission_.status(); }
    std::string equipment_status() const { return equip_status_; }

    // Game thread. Other players' items (apitem.h): set_labels arms it; the watch runs every
    // frame; restore_ap_items puts everything back (before an unload).
    void set_labels(std::map<int, Label> labels);
    // The stand-ins' names by map cell, as last laid out (tests).
    const std::map<int, std::string>& label_names() const { return ap_items_.labels(); }
    void watch_ap_items();
    // The live stand-in objects in both pools, with their cells and positions (name_cells), from
    // this frame's copy of the item pool (null: not read) and the item sprites' frame list.
    std::vector<StandInSeen> stand_ins_seen(const BYTE* pool, BYTE* base) const;
    void restore_ap_items();
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

    // An item record restyled (the stand-ins, the Area Map), an object dressed, or the pickup
    // popup's line width changed: unload must wait for a frame to put them back.
    bool ap_items_restyled() const {
        return ap_items_.restyled() || (popup_width_ > 0 && popup_width_ != kGamePopupWidth) ||
               looks_.dressed() || intel_.restyled();
    }
    int popup_width() const { return popup_width_; }  // tests

private:
    bool in_play() const;
    BYTE* player() const;
    BYTE* record(uint8_t item) const;
    bool popup_busy() const;
    void refresh_hud();

    bool configured_ = false;
    bool active_ = false;
    // Offline (offline.h).
    void leave_offline_seed();
    void drop_offline_warning();
    bool offline_warned_ = false;  // this load's warning queued
    // The seed whose save this load was dressed offline (-1: none), kept through a client's
    // short connections (they undress it): `client_back` tells the player the client is back.
    int offline_played_ = -1;
    bool other_save_ = false;     // a client said the loaded save is not its seed's
    bool other_warned_ = false;   // this load's other-seed notice queued
    bool other_mode() const { return active_ && other_save_; }
    bool section_is(const OfflineSeed& seed, const BYTE* section) const;
    std::vector<OfflineSeed> offline_seeds_;
    int offline_index_ = -1;  // the seed whose save is loaded
    int offline_count_ = -1, offline_told_ = -1;
    int offline_frames_ = 0;
    bool offline_skip_ = false;
    // The file select's counts: the client's (disk_slots) and the offline ones (offline.h).
    std::map<int, int> client_file_counts_, offline_file_counts_;
    void apply_file_counts();
    BYTE* map_grid_ = nullptr;
    BYTE* flags_ = nullptr;
    BYTE* slot_array_ = nullptr;
    BYTE* rando_slots_ = nullptr;
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
    Hooks hooks_;
    ArchipelagoChoice choice_;
    AssistLock assists_;
    bool assists_locked_ = false;
    FontGlyph lock_glyph_{kLockGlyph, kLockRows};
    FontGlyph disk_glyph_{kDiskGlyph, kDiskRows};  // the disk's popup icon (equip.h)
    FontGlyph disk_header_[2] = {{kDiskHeaderGlyphs[0], kDiskHeaderRows[0]},
                                 {kDiskHeaderGlyphs[1], kDiskHeaderRows[1]}};  // files.h
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
    BYTE* text_table_ = nullptr;  // holds the pointer to the loaded Text table
    ScanText scan_;
    EquipAddresses equip_at_;
    EquipmentHook equip_;
    EquipmentScreen equip_screen_;
    MissionAddresses mission_at_;
    PauseAddresses pause_at_;
    PauseDiskRow pause_row_;
    FileAddresses files_at_;
    FileDisks files_;
    IntelChip intel_;
    TitleAddresses title_at_;
    TitleLines title_;
    std::string title_error_;
    int map_frames_ = 0;  // frames until the next map look
    // The loaded section's item table and start when the labels were set (the seed they
    // are for): the map is changed only in a save in play holding that seed.
    std::optional<OfflineSeed> labels_seed_;
    std::map<int, Label> labels_;  // as set, for snapshot
    // That seed's save loaded (`in_play_only`: and in play, not in a menu such as the map).
    bool labels_seed_loaded(bool in_play_only) const;
    BYTE* map_handler_ = nullptr;  // holds the map screen's handler (null: not known)
    MapAddresses map_at_;
    MapCursorNames map_names_;
    bool discs_told_ = true;
    MissionPrompt mission_;
    int mission_save_ = -1;  // the save slot loaded when last watched (-1: none yet)
    std::string equip_status_ = "not shown yet";
    std::string scan_line_;
    int disc_count_ = 0, discs_required_ = 0;
    int scan_frames_ = 0;  // frames until the next scanner check
    BYTE* sprite_texture_ = nullptr;  // holds the pointer to the sprite data
    ApItems ap_items_;
    // The pickup popup's line width, kGamePopupWidth in the path items with no icon take (the
    // stand-ins do), changed to the width of the label a stand-in was named after: the one
    // within reach, or the player's cell's (docs/re/memory.md, "Other players' items").
    BYTE* popup_width_code_ = nullptr;
    BYTE* item_pool_ = nullptr;
    BYTE* object_pool_ = nullptr;  // the general objects (items in reversed-gravity rooms)
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
    // The item pool copied into pool_copy_, once a frame; null if it could not be read.
    const BYTE* read_item_pool();
    std::vector<BYTE> pool_copy_;
    void resolve_item_labels();
    void set_popup_width(int width);
    GlFunctions gl_;
    TextureCheck texture_check_;  // for everything painted into the sprite texture
    int skip_frames_ = 0;  // frames until the next escape-skip check
};

}  // namespace zap
