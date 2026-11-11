// Game: everything the helper does in the game, on the game's thread. configure keeps the
// addresses the client resolved by byte pattern (client/patterns.py, sent as RVAs); watch_all
// runs each part's watch from the frame call. The parts live in their own headers. Calls as
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
#include "translation.h"
#include "trap.h"

namespace zap {

// Game::unload_refusal's reasons.
inline constexpr char kMenuOpenRefusal[] = "a menu is open";
inline constexpr char kEndingRefusal[] =
    "the ending RETURN HOME WITH THE DISKS? started is on its way";

class Game {
public:
    // Game thread. Checks each required kConfigKeys entry is present (an optional one may be
    // missing, its member left null) and each given one in the right kind of section, with the
    // room it needs (game_calls.cpp, check_key), then keeps the addresses. "" or an error. `active`: false for the offline cache (no client there yet).
    std::string configure(HMODULE image, const std::map<std::string, uint64_t>& rvas,
                          bool active = true);
    bool configured() const { return configured_; }
    // Game thread: a save loaded with a menu open (pause, map, equipment screen, quit menu).
    bool menu_open() const;
    // Whether a client is there: set by `config`, cleared when it goes away (client_gone).
    bool active() const { return active_; }
    // Before configure: the helper's wrappers the game is pointed at (Hooks).
    void set_hooks(const Hooks& hooks) { at_.hooks = hooks; }
    // After configure (which loads opengl32's): the OpenGL functions to paint with (tests).
    void set_gl(const GlFunctions& gl) { gl_ = gl; }

    // Game thread: puts back everything the helper changed in the game's memory (unload, a
    // client going away); a change held by a closed menu is gone with it. What the client asked
    // for is kept, so the watches apply it again (a refused unload); the offline look is left,
    // and dressed again by its watch. `callback`: ARCHIPELAGO's, to recognize it.
    void put_back_game(WidgetCallback callback);
    // Game thread, after put_back_game: what the client asked for dropped (the new game, the
    // save's look, RETURN HOME's prompt, the file select's counts).
    void forget_client();
    // Game thread: the client went away. put_back_game, forget_client, its notices withdrawn, and
    // inactive until a client configures again. Its queued popups stay: their items are already
    // given (the client counts popups handed over, not shown).
    void client_gone(WidgetCallback callback);

    // Game thread. Only in play; every compare-and-set or none, then what the pickup routine
    // does after (maxima, HUD, Magnus Armor's color) and the popups queued. "applied" or
    // "refused <why>".
    std::string apply(const Batch& batch);

    // Game thread, every frame: shows the next queued popup, with its sound, once the game's
    // last popup has ended. A popup the game could not take stays queued.
    void pump_popups();

    size_t queued_popups() const { return popups_.size(); }

    // Game thread. Arms the AP new game: the next randomizer new game queued gets `plan`, once.
    // False, nothing armed, unless configured.
    bool arm_newgame(NewGamePlan plan);
    void disarm_newgame();
    // "armed", "idle", or the last outcome: "applied slot N" or "refused ...".
    std::string newgame_status() const;

    // Game thread, every frame: applies an armed plan to a queued randomizer new game with more
    // than one frame left. One started from RANDOMIZER must hold `identify` (the seed text), and
    // is placed again as the seed; if not, it is left as the player made it and the plan dropped.
    void watch_newgame();

    // Game thread, each frame call while configured (dllmain.cpp, until an unload begins):
    // every part's watch, in a fixed order. `callback`: ARCHIPELAGO's (watch_menu).
    void watch_all(WidgetCallback callback);

    // Game thread, every frame: ARCHIPELAGO in place of RANDOMIZER (menu.h) while a plan with
    // `choice` is armed, its callback `callback`; otherwise RANDOMIZER back.
    void watch_menu(WidgetCallback callback);
    // Whether the game may still hold one of the helper's functions (the parts' callbacks,
    // handlers and wrappers below), which unload must take back first. The ASSIST lock holds none.
    bool menu_installed() const {
        return choice_.installed() || choice_.holds_hook() || equip_.installed() ||
               equip_screen_.holds_hook() || mission_.holds_hook() || files_.holds_hook() ||
               map_names_.holds_hook() || pause_row_.holds_hook();
    }
    // The game's own game-mode panel handler, for the wrapper to call; null before configure.
    void* panel_handler() const { return at_.panel_handler_fn; }
    // Game thread, from the wrapper after the game's handler: fits ARCHIPELAGO's hover border.
    void fit_archipelago_border() {
        if (configured_ && choice_.installed()) choice_.fit_border(at_);
    }

    // The ASSIST lock (menu.h, AssistLock), armed by `assists`: the watch locks it every frame
    // while armed and unlocks it otherwise; restore_assists unlocks it now (before an unload).
    void set_assists_locked(bool locked) { assists_locked_ = locked; }
    bool assists_locked() const { return assists_locked_; }
    void watch_assists();
    void restore_assists();

    // Game thread, from ARCHIPELAGO's callback: place_seed, then the new game queued (reported by
    // watch_newgame next frame). Anything off: "refused <why>", the plan dropped, nothing queued.
    void start_archipelago(WidgetCallback callback);

    // The OS Install Disks goal (scan.h; `discs`): set_discs sets the count and the scanner's
    // line ("" turns it off); the watch rewords the scanner's message while a line is set;
    // restore_discs puts back the goal's changes now (before an unload).
    void set_discs(int count, int required, std::string scan_line, bool told = true);
    void watch_discs();
    void restore_discs();
    bool scan_written() const { return scan_.written(); }
    int disc_count() const { return disc_count_; }
    bool discs_told() const { return discs_told_; }
    bool keys_branch_patched() const { return pause_row_.patched(); }
    int discs_required() const { return discs_required_; }
    const std::string& scan_line() const { return scan_line_; }
    // EQUIPMENT's wrapper (equip.h) calls the game's, then equipment_built.
    void* equipment_fn() const { return at_.equipment_fn; }
    void equipment_built();
    // The Energy Tank panel's wrapper (equip.h) calls the game's, then equipment_event.
    void* equip_panel_fn() const { return at_.equip_panel_fn; }
    void equipment_event(void* widget, intptr_t event);
    // OPTION MENU's way back to the pause menu (pause.h): the wrapper calls the game's, then
    // options_returned turns the disk row before the frame is drawn.
    void* options_back_fn() const { return at_.options_back_fn; }
    void options_returned(void* menu);
    // RETURN HOME WITH THE DISKS? (mission.h; `mission_prompt`), with home's save point in tiles
    // (the Landing Pod's); dropped unopened once in play outside home's cell. False, nothing
    // asked, unless configured and home on the map.
    bool request_mission(int column, int row);
    // Game thread: an AP save's ending on its way, which an unload would leave unguarded or
    // drop: YES pressed and the ending not started yet (or the screen still black after it), or
    // the ending routine (or the helper's wrapper for it) queued with the ending patched.
    bool ending_pending() const;
    // Game thread, in the unload's frame call before anything is put back: why it refuses, or
    // null. An open menu keeps what it was built from (the pause row's disc, a record's name).
    const char* unload_refusal() const {
        if (menu_open()) return kMenuOpenRefusal;
        if (ending_pending()) return kEndingRefusal;
        return nullptr;
    }
    // The file select (files.h): `disk_slots` sets the counts by slot; the entries' wrapper calls
    // the game's (file_entry_original), then file_event.
    void set_disk_slots(std::map<int, int> counts) {
        client_file_counts_ = std::move(counts);
        apply_file_counts();
    }
    void* file_entry_original(void* widget) const { return files_.original(widget); }
    const FileEntries& files() const { return files_; }
    void file_event(void* widget, intptr_t event);
    void put_back_files();  // the entries and glyphs; the counts kept
    // Slots holding a randomizer save in use whose own section copy is a known seed's.
    std::set<int> ap_file_slots() const;

    // Game thread: the Area Map as INTEL CHIP while on (`intel`, intel.h).
    void set_intel(bool on) { intel_.set(on); }

    // Game thread: Feats and achievements kept out of AP saves (feats.h; watch_feats).
    // ap_save_loaded: a randomizer save of a seed the helper knows is loaded. restore_feats puts
    // both changes back; release_steam, from the pipe thread once no frame call can watch, puts
    // back Steam's slot alone.
    bool ap_save_loaded() const;
    void watch_feats();
    void add_known_seeds(const std::vector<OfflineSeed>& seeds);
    size_t known_seeds() const { return known_seeds_.size(); }  // tests
    void restore_feats();
    void release_steam() { feats_.release_steam(); }
    std::string feats_state() const { return feats_.state(); }
    const FeatsGuard& feats() const { return feats_; }  // tests
    // An AP save's ending was jumped over (watch_feats): the Feats of Natu screen that may follow
    // says no Feats were earned (feats_screen_note). Until a save loads, or that screen has gone.
    bool feats_ended() const { return feats_ended_; }

    // Game thread: AP saves without the client (offline.h). set_offline replaces the seeds' files
    // (no client); merge_offline adds to them (with a client), leaving the dressed save as it is.
    // offline_state, for `hello`: "-", or the count (then ":" and the dressed seed's file).
    void set_offline(std::vector<OfflineSeed> seeds);
    void merge_offline(std::vector<OfflineSeed> seeds);
    void watch_offline();
    void leave_offline();
    std::string offline_state() const;
    // Game thread (`other_save`): the loaded randomizer save is not the client's seed's. A known
    // seed's save gets the other-seed notice once a load (again after a reconnect) and its
    // offline look: "told" or "already told". A save of no known seed: "unknown", left alone.
    std::string other_save();
    // Game thread (`client_back`, once the client has seen its seed's save in play): if that save
    // was played offline, ARCHIPELAGO CLIENT CONNECTED. pops up, once. Whether it did.
    bool client_back();
    bool intel_on() const { return intel_.on(); }
    // Game thread: the title screen's mod lines (title.h), hidden before an unload. title_state,
    // for `hello`: "1" up, "0" not, else why they could not be made.
    void set_title_version(const std::string& version) { title_.set_version(version); }
    // Game thread: the map cursor's names for the stand-ins' squares (mapmarks.h); map_after runs
    // from the map handler's wrapper, after the game's handler.
    void map_after();
    void* map_original(void* child) const;
    void hide_title();
    std::string title_state() const;
    void restore_intel();
    void mission_open();
    void* mission_yes() { return mission_.yes(); }
    void mission_after_no();  // YES, once the game's NO has closed the prompt (mission.h)
    // The ending's wrapper (mission.h, end): the game's ending runs, configured or not
    // (configure's addresses, kept until the next one).
    void mission_end() { mission_.end(at_); }
    std::string mission_status() const { return mission_.status(); }
    std::string mission_black() const { return mission_.black(); }
    std::string equipment_status() const { return equip_status_; }

    // Game thread. Other players' items (apitem.h): armed by set_labels; restored before unload.
    void set_labels(std::map<int, Label> labels);
    // The labels by map cell, each laid out, an @N one resolved once records load.
    const std::map<int, Label>& labels() const { return ap_items_.labels(); }
    void watch_ap_items();
    // The live stand-in objects in both pools, with their cells and positions (name_cells), from
    // this frame's copy of the item pool (null: not read) and the item sprites' frame list.
    std::vector<StandInSeen> stand_ins_seen(const BYTE* pool, BYTE* base) const;
    const ItemLooks& looks() const { return looks_; }  // tests
    void restore_ap_items();
    // Game thread, every frame: notes how long the player has been in play in one cell.
    void note_play(uint64_t now_ms);
    // Game thread (`shield`): spawns a received Energy Shield as its pickup does, once in play
    // in one cell for a second: "spawned", or "refused <why>".
    std::string spawn_shield(uint64_t now_ms);
    // Game thread, every frame: spawns the drop of each received Scrap Metal, Scrap Energy or
    // Scrap Chunk whose popup has shown, as its pickup does, one at a time, once in play in one
    // cell for a second in the save its popup was for (pump_popups queues them), and not in an
    // ending that began in the load (kept for the next).
    void pump_drops(uint64_t now_ms);
    // `drop_status` (for live checks): "queued N; last <outcome>", the outcome "none", "spawned
    // <item>" or "refused <why> <item>".
    std::string drop_status() const;
    size_t queued_drops() const { return drops_.size(); }
    // Game thread, every frame, and not while an unload runs (dllmain.cpp: with the watches,
    // after watch_all): sets off each trap an `apply` took (a received one's popup, or
    // `trap` for this world's own), as the game's touch does (trap.h), one a frame, at once once
    // in play for a moment in the save it was for, not halted, not from RETURN HOME's fade
    // through its ending, and not in an ending that began in the load (kept for the next); and
    // keeps AILMENTS's tests patched while a trap's timer runs.
    void pump_traps(uint64_t now_ms);
    // `trap_status` (for live checks): "queued N; last <outcome>", the outcome "none", "set
    // <item>", or "nothing <item>" (its timer already longer: as the game's op does nothing).
    bool ailments_patched() const { return hit_patch_.patched() || wave_patch_.patched(); }
    std::string trap_status() const;
    size_t queued_traps() const { return traps_.size(); }
    // Game thread (`die`): energy 0 and the game's death routine, once settled in one cell for a
    // second, not halted, no message up, no RETURN HOME prompt or ending on its way: "died" or
    // "refused <why>".
    std::string kill(uint64_t now_ms);

    // An item record restyled (the stand-ins, the Area Map), an object dressed, or the pickup
    // popup's line width changed: unload must wait for a frame to put them back.
    bool ap_items_restyled() const {
        return ap_items_.restyled() || (popup_width_ > 0 && popup_width_ != kGamePopupWidth) ||
               looks_.dressed() || intel_.restyled();
    }
    int popup_width() const { return popup_width_; }  // tests
    // The first thing an unload must wait a frame to put back, as its refusal names it; or null.
    const char* unload_blocker() const {
        if (menu_installed()) return "a menu still holds the helper's callback";
        if (ap_items_restyled()) return "an item record is still restyled";
        if (scan_written()) return "the scanner's message is still the helper's";
        if (keys_branch_patched()) return "the pause screen's branch is still the helper's";
        if (feats_.ending_patched()) return "the ending's patch is still the helper's";
        if (seen_marker_.patched()) return "the map's on-screen marker is still the helper's";
        if (text_cut_.patched()) return "the text window's cut is still the helper's";
        if (hit_patch_.patched() || wave_patch_.patched()) {
            return "the AILMENTS tests are still the helper's";
        }
        if (popup_areas_.in_use()) return "a popup still shows the helper's translated text";
        // Entry 70 still pointing into the run: put back by the unload's frame, so only an unload
        // no frame came for is refused (the next helper would take it for the game's).
        if (popup_areas_.split()) return "a translated popup's entry is still the helper's";
        if (tr_entries_.held() > 0) return "a translated name is still the helper's";
        return nullptr;
    }

    // The translation (translation.h): `hello`'s `lang`; what the helper holds (tests).
    std::string language() const { return configured_ ? language_name(at_) : "?"; }
    // Game thread, in an unload's frame once all is put back: popups still to show take no
    // borrowed entry, so none is left the helper's; the next watch_all (the unload refused)
    // takes them back.
    void hold_translated_popups() { translated_popups_ = false; }
    const EntryWriter& tr_entries() const { return tr_entries_; }
    const PopupAreas& popup_areas() const { return popup_areas_; }

    // A label in the translation's font (translation.h, hd_case): its glyph as hd_icon (left out
    // without `icon`), its first line, and with `first_line` false, whom it is for on the next.
    // Another Zexion player's item it gives no name for: the translation's name for it.
    std::wstring hd_label_text(const Label& label, bool first_line, bool icon) const;

private:
    // The game's last message and its sequence, as the game keeps them: whether both read.
    bool last_message(BYTE*& last, int32_t& sequence) const;
    // The watches only watch_all calls (it checks the configuration).
    void watch_equipment();
    void watch_mission();
    void watch_files();
    void watch_intel();
    void watch_title();
    void watch_map_names();
    void watch_map_marks();
    // In play in an AP save: owned items the game lists (pickups::listed), but the new game's
    // starting ones, appended to the major-item list as a pickup would: grants set the byte alone
    // (docs/re/memory.md, "The pickup record holds the major-item list").
    void watch_pickups();
    // The helper's names and scanner line in the translation's tables; popup areas freed.
    void watch_translation();
    // A popup in the translation's font: the icon and translated name (a stand-in's own, not its
    // label), then the client's suffix on the next line; a notice's text alone (hd_case).
    std::wstring hd_popup_text(const Popup& popup, const BYTE* rec, const Loaded& now) const;
    // A font glyph painted while wanted, cleared once not (if it was painted).
    void show_glyph(FontGlyph& glyph, bool wanted);
    // A client's or the offline look put back and dropped: stand-ins, INTEL CHIP, the ASSIST
    // lock, the disks. put_back_save_look: the game's memory only, the look kept.
    void drop_save_look();
    void put_back_save_look();
    void put_back_ap_items();  // restore_ap_items' put back, the labels kept
    // A randomizer save loaded (its marker set): the loaded section is its own.
    bool rando_save_loaded() const;
    // A new game queued: the game's new-game routine set as the next transition.
    bool newgame_queued() const { return at<void*>(at_.transition_fn, 0) == at_.newgame_fn; }
    bool in_play() const;
    // In play outside home's cell; false with no home given, or the player's cell off the map.
    bool away_from_home() const;
    BYTE* player() const;
    BYTE* record(uint8_t item) const;
    bool popup_busy() const;
    // Frames left of the game's last message while it still shows, else 0.
    int32_t popup_frames_left() const;
    // Whether a queued popup may show in the save loaded now (Popup::origin).
    bool showable(const Popup& popup) const;
    // A save loaded with flag 250 set and 211 clear, the client's goal: the ending begun, in
    // this load or before (a save past its ending reads so throughout).
    bool ending_flag() const;
    // Whether `origin` (a popup's or a drop's) is the save loaded now, or none.
    bool origin_loaded(const std::shared_ptr<const OfflineSeed>& origin) const;
    void refresh_hud();

    bool configured_ = false;
    bool active_ = false;
    GameAddresses at_;  // configure's; the hooks from set_hooks
    BYTE* base_ = nullptr;  // the game's image
    // Offline (offline.h).
    void leave_offline_seed();
    // Withdraws a queued notice of the helper's (each is for its load; the other-seed one and
    // CLIENT CONNECTED. for their client too). Whether one was.
    bool drop_notice(const char* text);
    // The not-connected warning withdrawn; if it had not shown, given again with the next dress.
    void drop_offline_warning();
    // A client gone, or a client's config: its notices not shown yet withdrawn, other_save reset.
    void drop_client_notices();
    bool offline_warned_ = false;  // this load's warning queued
    // The seed dressed offline this load (-1: none), kept through short connections (client_back).
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
    bool offline_settled_ = false;  // the loaded save in play since it loaded
    // The file select's counts: the client's (disk_slots) and the offline ones (offline.h).
    std::map<int, int> client_file_counts_, offline_file_counts_;
    void apply_file_counts();
    std::deque<Popup> popups_;
    // Received scraps whose popup has shown, their drop not spawned yet (pump_drops), each with
    // its popup's save; lost, as queued popups are, with the helper.
    struct Drop {
        uint8_t item;
        std::shared_ptr<const OfflineSeed> origin;
    };
    std::deque<Drop> drops_;
    // Traps to set off (pump_traps), each with the save it was for; lost, as queued popups are,
    // with the helper.
    std::deque<Drop> traps_;
    std::string trap_status_ = "none";
    // Sets off trap `item` now: "set", "shield" or "nothing" (trap_status).
    std::string run_trap(uint8_t item);
    std::string disorient(BYTE* player);  // op 5's writes: run_trap's outcome
    // Game thread: each timer's AILMENTS tests patched while a trap's runs (trap.h).
    void watch_trap_patches();
    trap::HitPatch hit_patch_;
    trap::WavePatch wave_patch_;
    bool trap_hit_ = false, trap_wave_ = false;  // a trap started the timer, and it runs
    uint64_t in_play_since_ms_ = 0;               // in play since (any cell), for a trap
    void play_sound(const trap::Sound& sound);
    // Whether drops and traps wait for the ending (load_before_ending_), read every frame.
    bool held_for_ending();
    // This load has been in play outside its ending (ending_flag false), or has read flag 211
    // set (the ship, or the escape skip's YES: an ending on its way), since the last frame with
    // no save loaded. While ending_flag then reads true, drops wait (pump_drops).
    bool load_before_ending_ = false;
    std::string drop_status_ = "none";
    // Frame calls since the helper's last popup (more of them than game frames on a fast monitor).
    int frames_since_popup_ = 1 << 20;
    // The helper's last popup as the game's last message holds it (own_popup_up).
    BYTE* own_message_ = nullptr;
    bool own_disk_ = false;  // that popup a received disk's (its glyph kept while it shows)
    int32_t own_sequence_ = 0;
    bool own_popup_up() const;

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
    // built it, or checked against it; over a RANDOMIZER game, the game's own. The plan's second
    // target must be in the table used. "" or why not.
    std::string ensure_start_table();
    // The seed's placement: the start table, the menu set to the seed's settings and the plan
    // written, the game's placement run, the menu put back (however it ends); then, if the
    // section holds `expect`, the plan written over it. "" or why not.
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
    // The labels' seed (the loaded item table and start when set): the map changes only in it.
    std::optional<OfflineSeed> labels_seed_;
    // That seed's save loaded, in play or in a menu (such as the map).
    bool labels_seed_loaded() const;
    MapCursorNames map_names_;
    SeenMarker seen_marker_;  // the on-screen square's write, in the labels' seed's save
    TextCut text_cut_;        // the text window's top cut, while configured
    bool discs_told_ = true;
    MissionPrompt mission_;
    int mission_save_ = -1;  // *save_loaded when last watched (0 or 0xFF; -1: not yet)
    // Home's cell for RETURN HOME, the player state's row and column (request_mission); -1 none.
    int home_row_ = -1, home_column_ = -1;
    std::string equip_status_ = "not shown yet";
    std::string scan_line_;
    int disc_count_ = 0, discs_required_ = 0;
    int scan_frames_ = 0;  // frames until the next scanner check
    ApItems ap_items_;
    IconSheets icon_sheets_;  // the helper's icons in the translations' fonts
    // Runs a received item's pickup event (the Energy Shield's, a scrap's): "" or why not.
    std::string run_item_event(uint8_t item);
    int play_cell_ = -1;
    uint64_t play_since_ms_ = 0;
    int popup_width_ = -1;  // what the game holds, once read; -1 before
    ItemLooks looks_;  // other Zexion players' items shown as themselves
    // translation.h: the entries the helper writes, the popup areas, each stand-in's translated
    // name as last named ("" for none).
    EntryWriter tr_entries_;
    PopupAreas popup_areas_;
    bool translated_popups_ = true;  // hold_translated_popups
    std::wstring stand_in_hd_[kStandInCount];
    bool stand_in_near_[kStandInCount] = {};  // a stand-in of each record within reach now
    // Each stand-in's name held while its pickup message shows: the serial, the name ("" not yet).
    struct NameHold {
        int32_t serial = 0;
        std::wstring name;
    };
    NameHold name_holds_[kStandInCount];
    void hold_stand_in_names(std::map<EntryKey, std::wstring>& want);
    // The item pool copied into pool_copy_, once a frame; null if it could not be read.
    const BYTE* read_item_pool();
    std::vector<BYTE> pool_copy_;
    void resolve_item_labels();
    // Item k's name in the loaded translation ("" if none): with `own`, the game's own under a
    // name the helper wrote over it (tr_entries_).
    std::wstring translated_name(int item, bool own, const std::optional<Loaded>& now) const;
    void set_popup_width(int width);
    GlFunctions gl_;
    TextureCheck texture_check_;  // for everything painted into the sprite texture
    FeatsGuard feats_;
    std::set<int> feats_slots_;  // the AP saves' slots, for the stub's load test
    int feats_slot_frames_ = 0;  // frames until the next look for them
    bool feats_ended_ = false;
    bool note_seen_ = false;  // the Feats of Natu screen was up since the ending
    int note_frames_ = 0;  // frames until the next look for it
    // Every seed the helper has known since it loaded (table and start only), the oldest first.
    std::vector<OfflineSeed> known_seeds_;
};

}  // namespace zap
