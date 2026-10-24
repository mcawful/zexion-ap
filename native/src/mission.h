// RETURN HOME WITH THE DISKS?: the game's ending, offered once the player has enough OS Install
// Disks.
//
// In the OS Install Disks goal the disks are found in play, and the goal is the game's ending
// reached with enough of them; once the player has enough, the client asks the helper to offer
// the ending with a YES/NO prompt (docs/re/memory.md, "The mission prompt"). The game builds such a prompt for the escape skip: routine 15 of its
// transition-routine table (0x1668A0), run when the transition timer ends, as area event 3666
// sets it. The helper sets the same two fields, but to its own opener, which calls the game's
// builder and, before the prompt's first frame, relabels the question (the game's text routine)
// and points YES at the helper. YES then closes the prompt as NO does (the game's own NO
// callback) and asks for the ending: once the game is idle again, the helper sets routine 4
// (0x94A30), as the ship's ending event 2567 does, with no menu open (0x94A30 skips recording
// the ending while one is). NO is the game's own: the prompt closes and play goes on.
//
// The helper only sets the transition while the game is idle in play: no transition pending
// (timer 0), the routine the idle one (table entry 2, the pause), no menu, a save loaded and the
// player alive. On unload, an opener not yet run is replaced by the idle routine, and a YES still
// wrapped is pointed at NO (never back at the escape skip's YES, which would end the escape).
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace zap {

struct MissionAddresses {
    BYTE* current_menu = nullptr;      // holds the top menu
    BYTE* transition_fn = nullptr;     // the routine the game runs when its timer ends
    BYTE* transition_timer = nullptr;  // 32-bit frames left
    BYTE* routine_table = nullptr;     // the transition routines (0x3CF5F0)
    void* widget_handler = nullptr;    // the generic widget handler, the prompt's
    void* text_set_fn = nullptr;       // (text, string, first glyph, glyphs)
    void* open_hook = nullptr;         // the helper's opener (no arguments)
    void* yes_hook = nullptr;          // the helper's stand-in for YES's callback
};

constexpr int kIdleRoutine = 2, kEndingRoutine = 4, kPromptRoutine = 15;
extern const char kMissionQuestion[];

class MissionPrompt {
public:
    // Any thread holding the game's frame (the mailbox): offer the prompt when next idle.
    void request();

    // Game thread, each frame. `idle_in_play`: a save loaded, the player alive, no game menu
    // state, and no popup showing or queued. Sets the opener (or, after YES, the ending) once the
    // game is idle, and gives up on an opener another transition took the place of (asked again).
    void watch(const MissionAddresses& addresses, bool idle_in_play);

    // Game thread, from the opener: runs the game's prompt builder, then relabels the question
    // and wraps YES. Returns "" or what was not as traced (YES then points at NO).
    std::string open(const MissionAddresses& addresses);

    // Game thread, from YES: the game's NO callback to call (it closes the prompt), and the
    // ending asked for.
    void* yes();

    // Game thread: puts back what the helper holds (before an unload). An opener it could not
    // take off stays held (the helper then refuses to unload).
    void restore(const MissionAddresses& addresses);

    // Game thread: drops a prompt asked for or the ending after YES, and an opener not yet run
    // (the save changed, or the disk count is off), so neither comes in another save.
    void cancel(const MissionAddresses& addresses);

    // Whether the game holds a pointer into the helper: the opener set, or YES wrapped.
    bool holds_hook() const { return armed_ || yes_child_ != nullptr; }
    const std::string& status() const { return status_; }

private:
    bool requested_ = false;  // to offer when idle
    bool armed_ = false;      // the opener is the game's routine, its timer running
    bool ending_ = false;     // YES: the ending, when idle
    BYTE* menu_ = nullptr;
    BYTE* yes_child_ = nullptr;  // YES, wrapped
    void* no_fn_ = nullptr;      // NO's callback, the game's
    std::string status_ = "not asked";
};

}  // namespace zap
