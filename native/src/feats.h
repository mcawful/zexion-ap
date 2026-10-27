// Feats and Steam achievements kept out of AP saves (docs/re/memory.md, "Feats and achievements").
// While Game says an AP save is loaded (Game::ap_save_loaded), the helper changes two things, and
// puts both back as soon as it does not, and before an unload:
//
// - The ending. Its routine (0x94A30 on B1) sets 14Victory, the ending's database entries, this
//   run's Feat bytes and flag 251 in one stretch, from its first Steam call (ending_unlocks, the
//   6-byte `call [SteamInternal_ContextInit]`) to just before it sets flag 250 (ending_join).
//   The helper writes 12 bytes over that call: `mov byte [flag 251], 0` and `jmp ending_join`.
//   Every callee-saved register is stored before the call, the stretch leaves the stack as it
//   found it, and what the code at the join reads is either rbx (set before the stretch, never
//   changed in it) or set afresh there, so jumping over it is as if each test in it had failed.
//   Flag 251 is cleared because the code after the join reads it too (database entry 218, with a
//   Steam call), as do the tests that lead to the Feats of Natu screen (0x948B7, 0x18A5F2); a
//   save that already holds it, from an ending before this guard, has it cleared too. Flag 250,
//   the save that follows and the client's goal are untouched. Nothing jumps into the bytes written
//   past the call (checked over the whole of B1's code).
//
// - Steam. Every achievement call goes `SteamInternal_ContextInit(stats_context)` through the
//   import slot context_init_slot, then a call through the interface's vtable (SetAchievement,
//   ClearAchievement or StoreStats); no call site keeps the pointer or tests a result. The helper
//   points the slot at a stub that, for stats_context only, returns a stand-in interface whose
//   every vtable entry is `xor eax, eax; ret`, and passes every other call (the SteamUtils
//   context, any other) to steam_api64.
//
// The stub, the stand-in and its vtable are in two pages allocated once and never freed: a call
// that read the slot, or holds the stand-in, just before an unload still lands in live memory,
// and none of it runs helper code, so an unload need not wait for one. While off, the stub passes
// everything through, so a slot something else has hooked over the stub since stays safe.
//
// The Feats of Natu screen may still follow such an ending (by a path not traced), with nothing
// flashing. On it the helper writes kFeatsNote into the blank last line of its title label,
// under FEATS OF NATU! (feats_screen_note). The screen, as read live on B1: the top menu has 4
// panels; panel 0's first child is a live widget whose label is three lines of 38 glyphs (y 16,
// 32 and 48), the title on the middle one and the last all blank (frame 0); panels 2 and 3 hold
// the two rows of 7 Feat icons; the menu under it is MISSION COMPLETE's statistics (1 panel of 10
// lines) (docs/re/memory.md, "Feats and achievements").
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "addresses.h"

namespace zap {

constexpr size_t kEndingPatchSize = 12;

// Under FEATS OF NATU! after an AP save's ending.
inline constexpr char kFeatsNote[] = "ARCHIPELAGO SAVE: NO FEATS EARNED";

// Game thread: if the Feats of Natu screen that follows an ending is the top menu, its title
// label's last line made kFeatsNote, centered (`show`), or blank again. Only a line that reads
// blank or as the note is written, whole, and only if every glyph of it can take the note.
// Returns whether the screen is up.
bool feats_screen_note(const GameAddresses& addresses, bool show);

class FeatsGuard {
public:
    // Game thread, at configure: the image, and the import slot that names steam_api64's
    // SteamInternal_ContextInit (null if the image has none); the Steam checks are made again.
    void set_image(HMODULE image, void** context_init_by_name);

    // Game thread, each frame: on (an AP save loaded) or off.
    void watch(const GameAddresses& addresses, bool on);

    // Game thread: both put back (before an unload). The ending only where it still reads as
    // the helper wrote it.
    void restore();

    // Any thread, once no frame call can watch (an unload past its last frame): the stand-in off
    // and the slot put back if it still holds the stub. The stub stays, passing calls through.
    void release_steam();

    bool on() const { return on_; }
    bool ending_patched() const { return ending_patched_; }
    bool steam_hooked() const { return slot_ != nullptr; }
    // For `hello`: "off", "on", or "on:" and why a change could not be made.
    std::string state() const;

    // Tests: the stub's entry, and the stand-in interface pointer's holder it returns.
    void* stub() const { return code_; }
    void* stand_in() const;

private:
    void patch_ending(const GameAddresses& a);
    void restore_ending();
    bool reads_as_written() const;  // the ending's bytes are still the helper's
    void hook_steam(const GameAddresses& a);
    void unhook_steam();
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
