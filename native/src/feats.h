// Feats and Steam achievements kept out of AP saves (Game::ap_save_loaded): the ending's unlock
// stretch jumped over (kEndingPatchSize bytes over its first Steam call, flag 251 cleared, flag
// 250 and the goal untouched), and Steam's SteamInternal_ContextInit import slot pointed at a
// stub that returns a do-nothing stand-in for stats_context while blocking. The stub decides at
// call time: blocking while the guard is on, and while an AP save loads (the load test). Both are
// put back once no AP save is loaded and before an unload; the slot stays hooked, passing calls
// through, while the load test is set. The stub, stand-in and vtable are in two pages never
// freed: a call still in them after an unload lands in live memory and runs no helper code.
// On Linux there is no slot and no stub: the helper exports SteamInternal_ContextInit and
// feats_context_init decides each call (below; set_switch sets what it reads); the ending is
// patched the same way over the Linux ending's own first achievement call (setTheAchievement,
// feats.cpp). docs/re/memory.md, "Feats and achievements"; native/README.md, "Feats and
// achievements in AP saves".

#pragma once

#include "platform.h"

#include <cstdint>
#include <set>
#include <string>

#include "addresses.h"

namespace zap {

constexpr size_t kEndingPatchSize = 12;
// Linux: the frames between tries of a failed check of Steam's library (hook_steam).
constexpr int kSteamRetryFrames = 60;

// Under FEATS OF NATU! after an AP save's ending (in capitals in pixel glyphs: text.h).
inline constexpr char kFeatsNote[] = "Archipelago save: no Feats earned";

// Game thread: if the Feats of Natu screen that follows an ending is the top menu, its title
// label's last line made kFeatsNote, centered (`show`), or blank again; only a blank line or the
// note, whole. With the game's hint at the title: FEATS OF NATU! and the note over it, or the
// hint put back if the label still reads as the helper wrote it. Returns whether the screen is up.
bool feats_screen_note(const GameAddresses& addresses, bool show);

#ifndef _WIN32
// The Linux helper's SteamInternal_ContextInit, which it exports in the libsteam_api's place
// (native/linux/src/helper.cpp; docs/re/linux.md, "Mapped by disassembly: the Feats guard's Steam
// and load test"): for the stats context while the guard blocks, the stand-in's holder (an
// interface whose every method returns 0); any other call `real(context)`, or, where there is no
// libsteam_api to go on to, the same holder (never null). Any thread, no lock.
void* feats_context_init(void* context);
// The game's SteamUserStats() context, from its own symbols; before any thread runs.
void set_feats_stats_context(const void* context);
// The helper's own exported SteamInternal_ContextInit: the guard holds only if the game's call
// binds to it (check_steam), and it is never taken for libsteam_api's; at start.
void set_feats_export(const void* export_fn);
// The game's setTheAchievement(int), from its own symbols: the ending's call must reach it
// (FeatsGuard's patch on Linux); at start.
void set_feats_achievement_fn(const void* fn);
#endif

class FeatsGuard {
public:
    // Game thread, at configure: the image, and the import slot that names steam_api64's
    // SteamInternal_ContextInit (null if the image has none); the Steam checks are made again.
    void set_image(HMODULE image, void** context_init_by_name);

    // Game thread, each frame: on (an AP save loaded) or off; `loading`: the save slots (1 to
    // slot::kCount) holding AP saves, for the load test.
    void watch(const GameAddresses& addresses, bool on, const std::set<int>& loading = {});

    // Game thread: both put back (before an unload). The ending only where it still reads as
    // the helper wrote it.
    void restore();

    // Any thread, once no frame call can watch (an unload past its last frame): the stand-in and
    // load test off, the slot put back if it still holds the stub. The stub stays, passing through.
    void release_steam();

    bool on() const { return on_; }
    bool ending_patched() const { return ending_patched_; }
    bool steam_hooked() const { return slot_ != nullptr; }
    // The load test armed in the stub (tests).
    bool load_test() const;
    // For `hello`: "off", "on", or "on:" and why a change could not be made.
    std::string state() const;

    // Tests: the stub's entry, and the stand-in interface pointer's holder it returns.
    void* stub() const { return code_; }
    void* stand_in() const;

private:
    void patch_ending(const GameAddresses& a);
    void restore_ending();
    void hook_steam(const GameAddresses& a, bool on, const std::set<int>& loading);
    // The stub's switch: blocking always (`on`), and the load test over `loading` (empty: off).
    void set_switch(const GameAddresses& a, bool on, const std::set<int>& loading);
    // "" if the slot and context are as traced, else why not (made once per configure).
    std::string check_steam(const GameAddresses& a);
    bool make_pages();

    HMODULE image_ = nullptr;
    void** by_name_ = nullptr;
    bool on_ = false;
    bool ending_patched_ = false;
    BYTE* site_ = nullptr;  // where the ending was patched
    BYTE original_[kEndingPatchSize] = {};
    BYTE written_[kEndingPatchSize] = {};
    std::string ending_error_;
    bool steam_checked_ = false;
    int steam_wait_ = 0;  // Linux: frames before a failed check_steam is tried again
    std::string steam_error_;       // check_steam's
    std::string steam_slot_error_;  // the last hook's
    void* real_ = nullptr;          // steam_api64's SteamInternal_ContextInit
    void** volatile slot_ = nullptr;  // while hooked
    BYTE* code_ = nullptr;  // the stub's pages (feats.cpp), never freed
    BYTE* data_ = nullptr;
};

}  // namespace zap
