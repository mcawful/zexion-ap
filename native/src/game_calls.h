// Game: everything the helper does in the game, on the game's thread. configure keeps the
// addresses the client resolved by byte pattern (client/patterns.py, sent as RVAs); each part's
// watch runs from the frame call; the parts live in their own headers. Function arguments as
// traced in docs/re/memory.md, "Game functions the helper calls".
#pragma once

#include <windows.h>

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "addresses.h"
#include "game_mem.h"
#include "menu.h"
#include "apitem.h"
#include "looks.h"
#include "equip.h"
#include "feats.h"
#include "mission.h"
#include "pause.h"
#include "files.h"
#include "intel.h"
#include "offline.h"
#include "requests.h"
#include "scan.h"
#include "mapmarks.h"
#include "title.h"
#include "text.h"

namespace zap {

// Game::unload_refusal's reasons.
inline constexpr char kMenuOpenRefusal[] = "a menu is open";
inline constexpr char kEndingRefusal[] =
    "the ending RETURN HOME WITH THE DISKS? started is on its way";

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

class Game {
public:
    // Game thread. Checks every kConfigKeys entry is present and placed right in `image` (code
    // in code, data in writable data, the CRT import slots outside code, room where a key needs
    // it), then keeps the addresses. "" or an error. `active`: false for the offline cache, which
    // may lack the client_only keys (game_calls.cpp) or hold them where they do not fit: those
    // are left null (client_ready).
    std::string configure(HMODULE image, const std::map<std::string, uint64_t>& rvas,
                          bool active = true);
    bool configured() const { return configured_; }
    // Configured with every key, as a client's config is (or an offline cache that has them
    // all). The client's parts (grants, new games, DeathLink, Energy Shields, RETURN HOME) are
    // taken only then: apply, kill, spawn_shield, arm_newgame, request_mission and rearm refuse
    // otherwise, and the rest of those parts run only on what these set.
    bool client_ready() const { return configured_ && left_out_.empty(); }
    // The client-only keys the offline cache lacked ("" if none), for `hello`.
    const std::string& left_out() const { return left_out_; }
    // Game thread: a save loaded with a menu open (the pause, map or equipment screen, the quit
    // menu). Unload refuses then (dllmain.cpp, undo_for_unload).
    bool menu_open() const;
    // Whether a client is there: set by `config`, cleared when it goes away (client_gone).
    bool active() const { return active_; }
    // Before configure: the helper's wrappers the game is pointed at (Hooks).
    void set_hooks(const Hooks& hooks) { at_.hooks = hooks; }
    // After configure (which loads opengl32's): the OpenGL functions to paint with (tests).
    void set_gl(const GlFunctions& gl) { gl_ = gl; }

    // Game thread: puts back everything the helper changed (unload, a client going away); a
    // change held by a menu no longer open is gone with it. `callback`: ARCHIPELAGO's, to
    // recognize it.
    void put_back_all(WidgetCallback callback);
    // Game thread: the client went away. Puts back what it set (put_back_all) and goes
    // inactive until a client configures again; its popups not shown yet stay queued, as their
    // items are already given (the client counts popups handed over, not shown).
    void client_gone(WidgetCallback callback);
    // Game thread: what clients set, before put_back_all drops it, and set again after it (an
    // unload refused).
    ClientState snapshot() const;
    void rearm(ClientState state);

    // Game thread. Only in play (a save loaded, no menu, energy above 0); every compare-and-set
    // or none; then what the pickup routine does after (missiles, bombs, energy, HUD, Magnus
    // Armor's color) and the popups queued. "applied" or "refused <why>".
    std::string apply(const Batch& batch);

    // Game thread, every frame: shows the next queued popup, with its sound, once the game's
    // last popup has ended. A popup the game could not take stays queued.
    void pump_popups();

    size_t queued_popups() const { return popups_.size(); }

    // Game thread. Arms the AP new game: the next randomizer new game the game is about to
    // build (its new-game routine queued, the randomizer marker set) gets `plan`, once. False,
    // nothing armed, unless client_ready.
    bool arm_newgame(NewGamePlan plan);
    void disarm_newgame();
    // "armed", "idle", or the last outcome: "applied slot N" or "refused ...".
    std::string newgame_status() const;

    // Game thread, every frame: applies an armed plan when a randomizer new game is queued and
    // its timer has more than one frame left. One the player started from RANDOMIZER must hold
    // the plan's `identify` bytes (the seed text); with menu lines it is then placed again as
    // the seed (place_seed), else checked against `expect`. A section that does not match, or
    // whose placement as the seed is refused, is left as the game made it (the player's own
    // placement put back), and the plan is dropped.
    void watch_newgame();

    // Game thread, each frame call (dllmain.cpp, until an unload begins), if configured: every
    // part's watch, in a fixed order. `callback`: ARCHIPELAGO's (watch_menu).
    void watch_all(WidgetCallback callback);

    // Game thread, every frame: while a plan with menu lines is armed, offers ARCHIPELAGO in
    // place of RANDOMIZER on the game-mode panel, its callback `callback`; otherwise puts
    // RANDOMIZER back (also in put_back_all).
    void watch_menu(WidgetCallback callback);
    // Whether the game may still hold one of the helper's functions, which unload must take
    // back first: ARCHIPELAGO's callback or the game-mode panel's handler, EQUIPMENT's callback
    // or the Energy Tank panel's handler, the mission prompt's opener or YES, a file entry's
    // handler or the map's. The locked ASSIST holds none.
    bool menu_installed() const {
        return choice_.installed() || choice_.holds_hook() || equip_.installed() ||
               equip_screen_.holds_hook() || mission_.holds_hook() || files_.holds_hook() ||
               map_names_.holds_hook();
    }
    // The game's own handler for the game-mode panel, for the wrapper to call; null before
    // configure.
    void* panel_handler() const { return at_.panel_handler_fn; }
    // Game thread, from the wrapper after the game's handler: fits ARCHIPELAGO's hover border.
    void fit_archipelago_border() {
        if (configured_ && choice_.installed()) choice_.fit_border(at_);
    }

    // The options menu's ASSIST entry (menu.h, AssistLock): set_assists_locked arms it (the
    // `assists` request); the watch runs every frame, locking it while armed and unlocking it
    // otherwise; restore_assists unlocks it now (before an unload).
    void set_assists_locked(bool locked) { assists_locked_ = locked; }
    bool assists_locked() const { return assists_locked_; }
    void watch_assists();
    void restore_assists();

    // Game thread, from ARCHIPELAGO's callback: places the seed (place_seed) and queues the
    // new game
    // (watch_newgame reports "applied slot N" next frame). Anything off: "refused <why>", the
    // plan dropped, nothing queued.
    void start_archipelago(WidgetCallback callback);

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
    // EQUIPMENT wrapped while discs are on (equip.h); the wrapper calls the game's, then
    // equipment_built.
    void* equipment_fn() const { return at_.equipment_fn; }
    void equipment_built();
    // The Energy Tank panel's handler while it shows the disk (equip.h): the helper's wrapper
    // calls the game's (equip_panel_fn), then equipment_event.
    void* equip_panel_fn() const { return at_.equip_panel_fn; }
    void equipment_event(void* widget, intptr_t event);
    // RETURN HOME WITH THE DISKS? (mission.h): the `mission_prompt` request asks for it; the
    // watch runs every frame, and drops a request not yet opened once the player is in play
    // outside the seed's start cell (the landing pod's); the helper's opener and YES call
    // mission_open and mission_yes; put_back_all puts back what it holds (before an unload).
    // False, nothing asked, unless client_ready.
    bool request_mission();
    // Game thread: an AP save's ending on its way, which an unload would leave unguarded (its
    // patch put back before it runs) or drop: YES pressed and the ending not started yet, or the
    // ending routine set as the transition with the ending patched.
    bool ending_pending() const;
    // Game thread, in the unload's frame call before anything is put back: why it refuses then,
    // or null. A menu open (an open screen keeps what it was built from, the pause row's disc or
    // an item record's name, so nothing is put back under it), or ending_pending.
    const char* unload_refusal() const {
        if (menu_open()) return kMenuOpenRefusal;
        if (ending_pending()) return kEndingRefusal;
        return nullptr;
    }
    // The file select's entries (files.h): the disk count, which the `disk_slots` request sets
    // (a count by slot), and the AP logo on AP saves (ap_file_slots); the watch runs every
    // frame; the helper's wrapper of the save entries' handler calls the game's
    // (file_entry_original) and then file_event.
    void set_disk_slots(std::map<int, int> counts) {
        client_file_counts_ = std::move(counts);
        apply_file_counts();
    }
    void* file_entry_original(void* widget) const { return files_.original(widget); }
    const FileEntries& files() const { return files_; }
    void file_event(void* widget, intptr_t event);
    void restore_files();
    // The slots holding AP saves: a randomizer save in use whose own copy of the section is a
    // known seed's (known_section, as ap_save_loaded tests the loaded one).
    std::set<int> ap_file_slots() const;

    // Game thread: the Area Map named INTEL CHIP, its map reveal off, while on (`intel`,
    // intel.h), and put back.
    void set_intel(bool on) { intel_.set(on); }

    // Game thread: Feats and achievements kept out of AP saves (feats.h; when the guard goes on
    // and off: watch_feats). ap_save_loaded: a randomizer save is loaded whose item table and
    // start are a seed's the helper knows (a seed file read from the folder or put by a client
    // since, or the seed the labels were set for); add_known_seeds: seeds to know.
    // restore_feats puts both changes back (before an unload); release_steam, from the pipe
    // thread once no frame call can watch, puts back Steam's slot alone.
    bool ap_save_loaded() const;
    void watch_feats();
    void add_known_seeds(const std::vector<OfflineSeed>& seeds);
    size_t known_seeds() const { return known_seeds_.size(); }  // tests
    void restore_feats();
    void release_steam() { feats_.release_steam(); }
    std::string feats_state() const { return feats_.state(); }
    const FeatsGuard& feats() const { return feats_; }  // tests
    // An AP save's ending was jumped over (the guard went off as the save left, with the patch in
    // place, flag 250 set and 251 clear): the Feats of Natu screen that may follow says no Feats
    // were earned (feats_screen_note). Until a save is loaded, or that screen has come and gone.
    bool feats_ended() const { return feats_ended_; }

    // Game thread: AP saves without the client (offline.h). set_offline takes the seeds' files;
    // the watch (every 16th frame call) dresses a save of one of them while no client is there
    // or the client said other_save, and sets the file select's counts with or without a
    // client. `offline_state`, for `hello`: "-", or the count (then ":" and the dressed seed's
    // file, if any).
    void set_offline(std::vector<OfflineSeed> seeds);
    void watch_offline();
    void leave_offline();
    std::string offline_state() const;
    // Game thread (`other_save`): the loaded randomizer save is not the client's seed's. For an
    // AP save (its seed known from a file), queues the other-seed notice once a load (again
    // after a reconnect) and gives the save its offline look: "told" or "already told". A
    // randomizer save of no known seed (a plain randomizer game): "unknown", left alone.
    std::string other_save();
    // Game thread (`client_back`, from a client whose session has seen its seed's save in
    // play): if that save was played offline, ARCHIPELAGO CLIENT CONNECTED. pops up, once.
    // Whether it did.
    bool client_back();
    bool intel_on() const { return intel_.on(); }
    // Game thread: the title screen's mod lines (title.h): the version they name, the watch
    // (configured, client or not), and hidden before an unload. `title_state` for `hello`:
    // "1" up, "0" not, else why they could not be made.
    void set_title_version(const std::string& version) { title_.set_version(version); }
    // Game thread: the map cursor's name for the stand-ins' squares (mapmarks.h): the open map's
    // handler wrapped while this save's labels are known (each frame), the name rewritten after
    // the game's handler (`map_after`, from the wrapper), and the handler put back.
    void map_after();
    void* map_original(void* child) const;
    void hide_title();
    std::string title_state() const;
    void restore_intel();
    void mission_open();
    void* mission_yes() { return mission_.yes(); }
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
    // Game thread (`die`): energy 0 and the game's death routine, once settled in one cell for
    // a second, not halted and no message showing: "died" or "refused <why>".
    std::string kill(uint64_t now_ms);

    // An item record restyled (the stand-ins, the Area Map), an object dressed, or the pickup
    // popup's line width changed: unload must wait for a frame to put them back.
    bool ap_items_restyled() const {
        return ap_items_.restyled() || (popup_width_ > 0 && popup_width_ != kGamePopupWidth) ||
               looks_.dressed() || intel_.restyled();
    }
    int popup_width() const { return popup_width_; }  // tests
    // What an unload must wait for a frame to put back, the first of those above (and the
    // scanner's text, the pause screen's branch), as unload's refusal names it; null for none.
    const char* unload_blocker() const {
        if (menu_installed()) return "a menu still holds the helper's callback";
        if (ap_items_restyled()) return "an item record is still restyled";
        if (scan_written()) return "the scanner's message is still the helper's";
        if (keys_branch_patched()) return "the pause screen's branch is still the helper's";
        if (feats_.ending_patched()) return "the ending's patch is still the helper's";
        if (seen_marker_.patched()) return "the map's on-screen marker is still the helper's";
        return nullptr;
    }

private:
    // The watches only watch_all calls (it checks the configuration): the equipment screen and
    // the pause screen's row, the mission prompt, the file select, the Intel Chip, the title
    // lines, the map's names, and every 16th frame call item 48's map squares drawn as
    // important in the labels' seed's save (mapmarks.h).
    void watch_equipment();
    void watch_mission();
    void watch_files();
    void watch_intel();
    void watch_title();
    void watch_map_names();
    void watch_map_marks();
    // A font glyph painted while wanted, cleared once not (if it was painted).
    void show_glyph(FontGlyph& glyph, bool wanted);
    // The look a save gets from a client or the offline look put back: the stand-ins, INTEL
    // CHIP, the ASSIST lock and the disks (`discs` off: a prompt asked for dropped).
    void drop_save_look();
    // A randomizer save loaded (its marker set): the loaded section is its own.
    bool rando_save_loaded() const;
    // A new game queued: the game's new-game routine set as the next transition.
    bool newgame_queued() const { return at<void*>(at_.transition_fn, 0) == at_.newgame_fn; }
    bool in_play() const;
    // In play outside the loaded section's start cell (the seed's, the landing pod's); false
    // when the start record or the player's cell is no cell of the map.
    bool away_from_start() const;
    BYTE* player() const;
    BYTE* record(uint8_t item) const;
    bool popup_busy() const;
    // Frames left of the game's last message while it still shows, else 0.
    int32_t popup_frames_left() const;
    // Whether a queued popup may show in the save loaded now (Popup::origin).
    bool showable(const Popup& popup) const;
    void refresh_hud();

    bool configured_ = false;
    bool active_ = false;
    std::string left_out_;  // the client-only keys configure left null (client_ready)
    GameAddresses at_;  // configure's; the hooks from set_hooks
    BYTE* base_ = nullptr;  // the game's image
    // Offline (offline.h).
    void leave_offline_seed();
    void drop_offline_warning();
    bool offline_warned_ = false;  // this load's warning queued
    // The seed whose save this load was dressed offline (-1: none), kept through a client's
    // short connections (they undress it): `client_back` tells the player the client is back.
    int offline_played_ = -1;
    bool other_save_ = false;     // a client said the loaded save is not its seed's
    bool other_warned_ = false;   // this load's other-seed notice queued
    bool section_is(const OfflineSeed& seed, const BYTE* section) const;
    // Slot n's copy of its randomizer section, if it holds a randomizer save (else null); and
    // whether a section is a known seed's (the labels' or known_seeds_).
    const BYTE* slot_section(int n) const;
    bool known_section(const BYTE* section) const;
    // The index of the known seed whose save is loaded, or -1.
    int loaded_seed() const;
    // The pause row's text for a known disk seed's loaded save, from its file ("" if none).
    std::string known_disk_row() const;
    std::vector<OfflineSeed> offline_seeds_;
    int offline_index_ = -1;  // the seed whose save is loaded
    int offline_count_ = -1, offline_told_ = -1;
    int offline_frames_ = 0;
    // The file select's counts: the client's (disk_slots) and the offline ones (offline.h).
    std::map<int, int> client_file_counts_, offline_file_counts_;
    void apply_file_counts();
    std::deque<Popup> popups_;
    int frames_since_popup_ = 1 << 20;

    bool newgame_armed_ = false;
    NewGamePlan newgame_;
    std::string newgame_status_ = "idle";

    ArchipelagoChoice choice_;
    AssistLock assists_;
    bool assists_locked_ = false;
    FontGlyph lock_glyph_{kLockGlyph, kLockRows};
    FontGlyph disk_glyph_{kDiskGlyph, kDiskRows};  // the disk's popup icon (equip.h)
    FontGlyph pause_disc_{kDiscX, kDiscY, kDiscRows};  // the pause row's disc (pause.h)
    FontGlyph disk_header_[2] = {{kDiskHeaderGlyphs[0], kDiskHeaderRows[0]},
                                 {kDiskHeaderGlyphs[1], kDiskHeaderRows[1]}};  // files.h
    FontGlyph ap_logo_{kApLogoX, kApLogoY, kApLogoRows};  // the file select's AP logo (files.h)
    int file_slot_frames_ = 0;  // frames until the next look for the AP saves' slots
    bool starting_ = false;  // ARCHIPELAGO pressed; the new game it queued not yet applied

    // The start table: for ARCHIPELAGO (starting_), filled from the plan if the game has not
    // built it, or checked against it; over a RANDOMIZER game, the game's own. The plan's
    // second target must be in the table used. Returns "" or why not.
    std::string ensure_start_table();
    // The seed's placement: the start table ensured, the menu set to the seed's settings and the
    // plan written, the game's placement run, the player's menu put back (however it ends);
    // then, if the section holds `expect`, the plan written over it. Returns "" or why not.
    std::string place_seed();
    ScanText scan_;
    EquipmentHook equip_;
    EquipmentScreen equip_screen_;
    PauseDiskRow pause_row_;
    FileEntries files_;
    IntelChip intel_;
    TitleLines title_;
    std::string title_error_;
    int map_frames_ = 0;  // frames until the next map look
    int map_seed_passes_ = 0;  // map looks in a row that read the labels' seed loaded (to 2)
    // The loaded section's item table and start when the labels were set (the seed they
    // are for): the map is changed only in a save holding that seed.
    std::optional<OfflineSeed> labels_seed_;
    std::map<int, Label> labels_;  // as set, for snapshot
    // That seed's save loaded, in play or in a menu (such as the map).
    bool labels_seed_loaded() const;
    MapCursorNames map_names_;
    SeenMarker seen_marker_;  // the on-screen square's write, in the labels' seed's save
    bool discs_told_ = true;
    MissionPrompt mission_;
    int mission_save_ = -1;  // *save_loaded when last watched (0 or 0xFF; -1: not yet)
    std::string equip_status_ = "not shown yet";
    std::string scan_line_;
    int disc_count_ = 0, discs_required_ = 0;
    int scan_frames_ = 0;  // frames until the next scanner check
    ApItems ap_items_;
    // Runs a received item's pickup event (the Energy Shield's): "" or why not.
    std::string run_item_event(uint8_t item);
    int play_cell_ = -1;
    uint64_t play_since_ms_ = 0;
    int popup_width_ = -1;  // what the game holds, once read; -1 before
    std::map<int, int> label_widths_;
    std::map<int, Label> item_labels_;  // labels still to be made from an item's record
    ItemLooks looks_;  // other Zexion players' items shown as themselves
    // The item pool copied into pool_copy_, once a frame; null if it could not be read.
    const BYTE* read_item_pool();
    std::vector<BYTE> pool_copy_;
    void resolve_item_labels();
    void set_popup_width(int width);
    GlFunctions gl_;
    TextureCheck texture_check_;  // for everything painted into the sprite texture
    FeatsGuard feats_;
    int feats_passes_ = 0;  // frame calls in a row that found an AP save loaded (to 2)
    int feats_other_ = 0;   // and that found another save loaded (to 2)
    std::set<int> feats_slots_;  // the AP saves' slots, for the stub's load test
    int feats_slot_frames_ = 0;  // frames until the next look for them
    bool feats_ended_ = false;
    bool note_seen_ = false;  // the Feats of Natu screen was up since the ending
    int note_frames_ = 0;  // frames until the next look for it
    // Every seed the helper has known since it loaded (table and start only), the oldest first.
    std::vector<OfflineSeed> known_seeds_;
};

}  // namespace zap
