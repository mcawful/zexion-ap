// RETURN HOME WITH THE DISKS?: offers the game's ending through its own escape-skip prompt
// (routine 15), relabeled, opened as event 3666 opens it, after event 3667's fade. YES runs the
// game's NO, keeps the screen black and starts the ending (routine 4) through the helper's
// wrapper. What the helper set and the game has not run is put back on cancel and unload: a
// wrapped YES never becomes the game's own YES, which skips the escape and final battle
// (docs/re/memory.md, "The mission prompt"; native/README.md, "The OS Install Disks goal").
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "addresses.h"
#include "game_mem.h"

namespace zap {

constexpr int kIdleRoutine = 2, kEndingRoutine = 4, kPromptRoutine = 15;
// Event 3667, the fade (type 1), and event 3666's delay: the fade takes 40 frames of the 60.
constexpr int kFadeEvent = 3667;
constexpr int32_t kFadeDelay = 60;
// Frames of the game's last message (GAME SAVED, 180) left when the fade starts: chosen in game.
constexpr int32_t kFadeMessageLeft = 150;
// The most game frames (GameFrames, not frame calls) kept black after YES with no ending
// started, then it fades back in: a safety (native/README.md, "The OS Install Disks goal").
constexpr int kDarkFrames = 240;
// The ending after YES fades in from black, a level each this many frames.
constexpr int8_t kEndingFadeStep = 8;
extern const char kMissionQuestion[];  // in capitals after a blank in its pixel glyphs
// The status from the frame the ending is set until left_save (client/discs.py, ENDING_STARTED).
extern const char kEndingStarted[];

// The game's fade of the global level, at `fade`: target, step (0: off) and countdown, signed
// bytes; the level is the byte at `fade_level` (docs/re/memory.md, "The mission prompt").
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

    // Game thread, each frame. `idle_in_play`: as game_calls.cpp's watch_mission defines it.
    // `away`: in play outside home's cell. Sets the opener (or, after YES, the ending) once the
    // game is idle; an opener another transition took the place of is asked again (taken_over).
    // A request not yet opened lapses while `away`.
    void watch(const GameAddresses& addresses, bool idle_in_play, bool away = false);

    // Game thread, from the opener: runs the game's prompt builder, then relabels the question
    // and wraps YES. Returns "" or what was not as traced; once the three children are read,
    // YES points at NO (if NO's callback reads) whatever else fails.
    std::string open(const GameAddresses& addresses);

    // Game thread, from the opener when not in play (a death during the fade): the opener taken
    // off, the fade turned back if still the opener's (fade_still_ours), the prompt asked again.
    void put_off(const GameAddresses& addresses);

    // Game thread, from YES: the ending asked for; returns NO's callback to call (it closes it).
    void* yes();

    // Game thread, from YES once the game's NO has returned: if the fade reads as NO's close
    // leaves it, turns it off there, so the screen stays black until the ending; else leaves it.
    void after_no(const GameAddresses& addresses);

    // Game thread, from the ending's wrapper (hooks.mission_end), set as the transition after
    // YES: runs the ending routine once per wrapper set (else only takes the wrapper off), then,
    // if the screen was the helper's black, fades in from black (kEndingFadeStep). A wrapper
    // still the transition after it becomes the idle routine.
    void end(const GameAddresses& addresses);

    // Game thread: kept black after YES, the ending not started: popups and deaths wait.
    bool dark() const { return dark_; }
    // The ending started by the helper in this save's load (until left_save, or another
    // transition took its place): grants, popups, shields and deaths wait (game_calls.cpp).
    bool started() const { return started_; }

    // Game thread: out of the black after YES, fading back in as NO's close would; with a menu
    // open, once it has closed (watch). Nothing if not dark. `why` is what black() then says.
    void end_dark(const GameAddresses& addresses, const char* why);
    // How the last black after YES ended (`mission_black`): "kept black" while it lasts, or "none".
    const std::string& black() const { return black_; }

    // Game thread: another save loaded or none: offered again after an ending the helper started.
    void left_save();

    // Game thread: puts back what the helper holds (before an unload). The ending's wrapper still
    // the transition becomes the game's ending routine (no fade in). An opener or wrapper it
    // could not take off stays held (the helper then refuses to unload).
    void restore(const GameAddresses& addresses);

    // Game thread: drops a prompt asked for, the ending after YES and an opener not yet run (a
    // save loaded or closed, or `discs` off), so neither comes in another save.
    void cancel(const GameAddresses& addresses);

    // Whether the game holds a pointer into the helper (opener, YES or the ending's wrapper).
    bool holds_hook() const { return armed_ || yes_child_ != nullptr || end_set_; }
    // The opener set and not run yet (the fade, then the prompt): popups wait.
    bool opening() const { return armed_; }
    // A prompt asked for or opening (not one shown: an unload refuses then, and a NO's closed one
    // is cleared only by the next watch), which a refused unload asks for again.
    bool on_its_way() const { return requested_ || armed_; }
    bool ending() const { return ending_; }
    // Whether the ending routine, or the helper's wrapper for it, is the game's transition now
    // (set and not run yet); false without the routine table.
    bool ending_queued(const GameAddresses& addresses) const;
    const std::string& status() const { return status_; }

private:
    // Event 3667 itself (kOut, to black), or a copy aimed at the normal level: from the level as
    // it is, a step every 6 frames (kBack), or from black, every kEndingFadeStep frames (kIn).
    enum class Fade { kOut, kBack, kIn };
    // Runs that fade through the type 1 handler: "" or what was not as traced (nothing run).
    std::string fade(const GameAddresses& addresses, Fade kind);
    // Turns the fade back to the normal level, if it ran for the opener: whether it did.
    bool fade_back(const GameAddresses& addresses);
    // Whether the game's fade is still the opener's: on event 3667's way (fade_frames) no further
    // back than last seen with the opener in place, nor the transition timer higher.
    bool fade_still_ours(const GameAddresses& addresses) const;
    // Gives up an opener another transition replaced; whether it turned the fade back (still ours).
    bool taken_over(const GameAddresses& addresses);
    // Takes the opener off before it runs, if its fields can be written; the fade turned back.
    void take_back(const GameAddresses& addresses);
    // Whether the fade still reads as after_no left it: black, off.
    bool dark_still_ours(const GameAddresses& addresses) const;
    void stop_dark(const std::string& why) {
        dark_ = undark_ = false;
        black_ = why;
    }

    HMODULE image_ = nullptr;
    bool requested_ = false;  // to offer when idle
    bool armed_ = false;      // the opener is the game's routine, its timer running
    bool faded_ = false;      // and the fade to black was run with it
    int fade_seen_ = -1;      // its frames (fade_frames) when last seen with the opener in place
    int32_t timer_seen_ = 0;  // and the transition timer then
    bool ending_ = false;     // YES: the ending, when idle
    bool dark_ = false;       // and the screen kept black meanwhile (after_no)
    int dark_frames_ = 0;     // game frames in play kept black so far
    bool undark_ = false;     // the fade back asked for with a menu open: once it closes
    const char* undark_why_ = "";  // and why
    GameFrames clock_;        // game frames, for dark_frames_
    std::string black_ = "none";
    bool queued_ = false;     // the ending's wrapper set, not yet run
    bool end_set_ = false;    // and the game may still hold it (until seen gone, run or put back)
    bool started_ = false;    // the ending started in this save's load: not offered again
    BYTE* menu_ = nullptr;
    BYTE* yes_child_ = nullptr;  // YES, wrapped
    void* no_fn_ = nullptr;      // NO's callback, the game's
    std::string status_ = "not asked";
};

}  // namespace zap
