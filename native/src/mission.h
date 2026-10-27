// RETURN HOME WITH THE DISKS?: offers the game's ending once the player has enough OS Install
// Disks (docs/re/memory.md, "The mission prompt"). When idle in play (timer 0, routine table
// entry 2 the pause opener, no menu; the game's last message far enough in,
// kFadeMessageLeft), the helper opens it as the escape skip's prompt opens:
// event 3666 sets the transition routine (its opener here, routine 15 0x1668A0 there) with
// timer 60, and runs event 3667, the game's fade to black, through the area-event handlers.
// If event 3667 is not as traced, the opener comes with timer 1 and no fade. The opener runs the
// builder, relabels the question and points YES at the helper; YES runs the game's NO (which
// fades the screen back in) and then, once idle, starts routine 4 (0x94A30, which records the
// ending only with no menu open). An opener the helper takes back before it runs (cancel,
// unload) becomes the idle routine, and its fade is turned back to the normal level; one another
// transition took the place of (a save sets the pause opener back) has its fade turned back only
// while the game's fade is still event 3667's (fade_state); on unload a wrapped YES points at NO
// (never the escape skip's YES, which would end the escape).
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "addresses.h"

namespace zap {

constexpr int kIdleRoutine = 2, kEndingRoutine = 4, kPromptRoutine = 15;
// Event 3667, the fade (type 1), and event 3666's delay: the fade takes 40 frames of the 60.
constexpr int kFadeEvent = 3667;
constexpr int32_t kFadeDelay = 60;
// The fade starts with this many frames of the game's last message (GAME SAVED, 180 frames,
// 0x1FC876) left: 30 frames (half a second) after it appears, the owner's pick in game
// (2026-10-27). Before that it was 70 left, as far in as Zexion's popup is when the escape skip
// fades (docs/re/memory.md, "The mission prompt"), which felt slow after a save.
constexpr int32_t kFadeMessageLeft = 150;
extern const char kMissionQuestion[];

// The game's fade of the global level, at `fade` (docs/re/memory.md, "The mission prompt"):
// the level it moves toward, the frames between two steps (0: off) and the frames to the next
// step, signed bytes; the level itself is the byte at `fade_level`.
namespace fade_state {
constexpr ptrdiff_t kTarget = 0x93, kStep = 0x9F, kCountdown = 0xAB;
constexpr uint64_t kSize = 0xAC;  // through the countdown
}  // namespace fade_state

class MissionPrompt {
public:
    // Game thread, at configure: the game's image, which the fade's handler must be code of.
    void set_image(HMODULE image) { image_ = image; }

    // Game thread (the `mission_prompt` request): offer the prompt when next idle.
    void request();

    // Game thread, each frame. `idle_in_play`: a save loaded, the player alive, no game menu
    // state, no popup of the helper's to show, and the game's last message down to its last
    // kFadeMessageLeft frames (game_calls.cpp; for the ending, ended). `away`: the player in
    // play outside the seed's start cell. Sets the opener (or, after YES, the ending) once the
    // game is idle, and gives up on an opener another transition took the place of (asked
    // again; taken_over). A request not yet opened lapses while `away` (popups held it until the
    // player had left the landing pod): the client asks again after the next save there.
    void watch(const GameAddresses& addresses, bool idle_in_play, bool away = false);

    // Game thread, from the opener: runs the game's prompt builder, then relabels the question
    // and wraps YES. Returns "" or what was not as traced; once the three children are read,
    // YES points at NO (if NO's callback reads) whatever else fails.
    std::string open(const GameAddresses& addresses);

    // Game thread, from the opener when not in play (a death during the fade): the opener
    // taken off, the fade turned back if it is still the opener's (fade_still_ours), and the
    // prompt asked for again.
    void put_off(const GameAddresses& addresses);

    // Game thread, from YES: the game's NO callback to call (it closes the prompt), and the
    // ending asked for.
    void* yes();

    // Game thread: puts back what the helper holds (before an unload). An opener it could not
    // take off stays held (the helper then refuses to unload).
    void restore(const GameAddresses& addresses);

    // Game thread: drops a prompt asked for or the ending after YES, and an opener not yet run
    // (a save loaded or closed, or `discs` turned off), so neither comes in another save; an
    // opener another transition took the place of as in watch (taken_over).
    void cancel(const GameAddresses& addresses);

    // Whether the game holds a pointer into the helper: the opener set, or YES wrapped.
    bool holds_hook() const { return armed_ || yes_child_ != nullptr; }
    // The opener set and not run yet (the fade, then the prompt): popups wait.
    bool opening() const { return armed_; }
    // For a refused unload: a prompt on its way (asked for, opening or shown), the ending after
    // YES; `resume` asks for them again after restore.
    bool asked() const { return requested_ || armed_ || yes_child_ != nullptr; }
    bool ending() const { return ending_; }
    // Whether the ending routine is the game's transition now (set and not run yet); false
    // without the routine table.
    bool ending_queued(const GameAddresses& addresses) const;
    void resume(bool asked, bool ending);
    const std::string& status() const { return status_; }

private:
    // Runs event 3667 (`out`), or a copy of it aimed back at the normal level, through the
    // type 1 handler: "" or what was not as traced (nothing run).
    std::string fade(const GameAddresses& addresses, bool out);
    // Turns the fade back to the normal level, if it ran for the opener: whether it did.
    bool fade_back(const GameAddresses& addresses);
    // Whether the game's fade is still the one the opener ran: on event 3667's way (fade_frames)
    // and no further back than when last seen with the opener in place, nor the transition timer
    // higher (a save leaves it counting). The same values from another transition's fade
    // (0x1D93DE: the level back to 4, then 3667's, and a timer of 70) start it over.
    bool fade_still_ours(const GameAddresses& addresses) const;
    // Gives up the opener another transition took the place of, and turns the fade back if it
    // is still the opener's (a save sets the pause opener back and brings no fade of its own):
    // whether it did.
    bool taken_over(const GameAddresses& addresses);
    // Takes the opener off before it runs: the idle routine, timer 0, the fade turned back.
    void take_back(const GameAddresses& addresses);

    HMODULE image_ = nullptr;
    bool requested_ = false;  // to offer when idle
    bool armed_ = false;      // the opener is the game's routine, its timer running
    bool faded_ = false;      // and the fade to black was run with it
    int fade_seen_ = -1;      // its frames (fade_frames) when last seen with the opener in place
    int32_t timer_seen_ = 0;  // and the transition timer then
    bool ending_ = false;     // YES: the ending, when idle
    BYTE* menu_ = nullptr;
    BYTE* yes_child_ = nullptr;  // YES, wrapped
    void* no_fn_ = nullptr;      // NO's callback, the game's
    std::string status_ = "not asked";
};

}  // namespace zap
