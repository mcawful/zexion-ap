// Feats and Steam achievements kept out of AP saves (Game::ap_save_loaded): the ending's unlock
// stretch jumped over (kEndingPatchSize bytes over its first Steam call, flag 251 cleared, flag
// 250 and the goal untouched), and Steam's SteamInternal_ContextInit import slot pointed at a
// stub that returns a do-nothing stand-in for stats_context while blocking. The stub decides at
// call time: blocking while the guard is on, and while an AP save loads (the load test). Both are
// put back once no AP save is loaded and before an unload; the slot stays hooked, passing calls
// through, while the load test is set. The stub, stand-in and vtable are in two pages never
// freed: a call still in them after an unload lands in live memory and runs no helper code.
// docs/re/memory.md, "Feats and achievements"; native/README.md, "Feats and achievements in AP
// saves".
#pragma once

#include <windows.h>

#include <cstdint>
#include <set>
#include <string>

#include "addresses.h"

namespace zap {

constexpr size_t kEndingPatchSize = 12;

// Under FEATS OF NATU! after an AP save's ending (in capitals in pixel glyphs: text.h).
inline constexpr char kFeatsNote[] = "Archipelago save: no Feats earned";

// Game thread: if the Feats of Natu screen that follows an ending is the top menu, its title
// label's last line made kFeatsNote, centered (`show`), or blank again; only a blank line or the
// note, whole. With the game's hint at the title: FEATS OF NATU! and the note over it, or the
// hint put back if the label still reads as the helper wrote it. Returns whether the screen is up.
bool feats_screen_note(const GameAddresses& addresses, bool show);

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
    std::string steam_error_;       // check_steam's
    std::string steam_slot_error_;  // the last hook's
    void* real_ = nullptr;          // steam_api64's SteamInternal_ContextInit
    void** volatile slot_ = nullptr;  // while hooked
    BYTE* code_ = nullptr;  // the stub's pages (feats.cpp), never freed
    BYTE* data_ = nullptr;
};

}  // namespace zap
