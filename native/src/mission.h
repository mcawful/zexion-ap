// RETURN HOME WITH THE DISKS?: offers the game's ending once the player has enough OS Install
// Disks (docs/re/memory.md, "The mission prompt"). When idle in play (timer 0, routine table
// entry 2 the pause opener, no menu), the helper sets the transition routine to its opener with
// timer 1, as event 3666 does for the escape skip's prompt (routine 15, 0x1668A0). The opener
// runs that builder, relabels the question and points YES at the helper; YES runs the game's NO
// and then, once idle, starts routine 4 (0x94A30, which records the ending only with no menu
// open). On unload an opener not yet run becomes the idle routine and a wrapped YES points at NO
// (never the escape skip's YES, which would end the escape).
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "addresses.h"

namespace zap {

constexpr int kIdleRoutine = 2, kEndingRoutine = 4, kPromptRoutine = 15;
extern const char kMissionQuestion[];

class MissionPrompt {
public:
    // Game thread (the `mission_prompt` request): offer the prompt when next idle.
    void request();

    // Game thread, each frame. `idle_in_play`: a save loaded, the player alive, no game menu
    // state, and no popup showing or queued. Sets the opener (or, after YES, the ending) once the
    // game is idle, and gives up on an opener another transition took the place of (asked again).
    void watch(const GameAddresses& addresses, bool idle_in_play);

    // Game thread, from the opener: runs the game's prompt builder, then relabels the question
    // and wraps YES. Returns "" or what was not as traced; once the three children are read,
    // YES points at NO (if NO's callback reads) whatever else fails.
    std::string open(const GameAddresses& addresses);

    // Game thread, from YES: the game's NO callback to call (it closes the prompt), and the
    // ending asked for.
    void* yes();

    // Game thread: puts back what the helper holds (before an unload). An opener it could not
    // take off stays held (the helper then refuses to unload).
    void restore(const GameAddresses& addresses);

    // Game thread: drops a prompt asked for or the ending after YES, and an opener not yet run
    // (a save loaded or closed, or `discs` turned off), so neither comes in another save.
    void cancel(const GameAddresses& addresses);

    // Whether the game holds a pointer into the helper: the opener set, or YES wrapped.
    bool holds_hook() const { return armed_ || yes_child_ != nullptr; }
    // For a refused unload: a prompt on its way (asked for, opening or shown), the ending after
    // YES; `resume` asks for them again after restore.
    bool asked() const { return requested_ || armed_ || yes_child_ != nullptr; }
    bool ending() const { return ending_; }
    void resume(bool asked, bool ending);
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
