// RETURN HOME WITH THE DISKS?: offers the game's ending once the player has enough OS Install
// Disks (docs/re/memory.md, "The mission prompt"). The prompt is the game's own SKIP ESCAPE AND
// FINAL BATTLE? (built by routine 15, 0x1668A0), relabeled. When idle in play (timer 0, routine
// table entry 2 the pause opener, no menu; the game's last message far enough in,
// kFadeMessageLeft), the helper opens it as the game's event 3666 does: that event sets the
// transition routine (the helper's opener here, routine 15 there) with timer 60, and runs event
// 3667, the game's fade to black, through the area-event handlers.
// If event 3667 is not as traced, the opener comes with timer 1 and no fade. The opener runs the
// builder, relabels the question and points YES at the helper; YES runs the game's NO (which
// closes the prompt and puts back the fade the builder saved: black, then back in) and then,
// once idle, starts routine 4 (0x94A30, which records the ending only with no menu open). Right
// after NO, YES turns that fade off with the screen still black (after_no), so play does not
// fade back in before the ending; it fades back in as before when popups of the helper's wait
// (they would show on black), or after kDarkFrames with no ending, or when cancelled. The ending
// routine sets the global level back to 4 itself (0x95230) as it warps the player to the
// ending's first scene (0x1D2850), and the game draws that frame before the helper's frame call:
// so the helper sets its own wrapper as the transition (end), which runs the routine and, while
// the black is still the helper's, sets the level back to black before that frame is drawn and
// fades in from it (kEndingFadeStep), as the game's own fades do. Once the ending is started, the
// prompt is not offered again while that save stays loaded (its cutscenes read as play, and the
// ending's own save reads as a save at the pod). An opener the helper takes back before it runs
// (cancel, unload) becomes the idle routine, and its fade is turned back to the normal level; one
// another transition took the place of (a save sets the pause opener back) has its fade turned
// back only while the game's fade is still event 3667's (fade_state); on unload a wrapped YES
// points at NO (never the game's own YES, which skips the escape and final battle), and the
// ending's wrapper not run yet becomes the game's ending routine.
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
// The fade starts with this many frames of the game's last message (GAME SAVED, 180 frames,
// 0x1FC876) left: 30 frames (half a second) after it appears, the owner's pick in game
// (2026-10-27). Before that it was 70 left, as far in as Zexion's popup is when the game's own
// prompt fades in after the grab (docs/re/memory.md, "The mission prompt"), which felt slow
// after a save.
constexpr int32_t kFadeMessageLeft = 150;
// The most game frames the screen stays black after YES with no ending started (after_no), as a
// safety: after YES the ending waits only for a menu to close and the gap after the helper's
// last popup (one waiting ends the black first; game_calls.cpp, watch_mission), so 240 errs
// long (it was GAME SAVED's 180, 0x1FC876, when the ending also waited for that). Then it fades
// back in. Counted in game frames (GameFrames, game_mem.h): until 1.0.0's release candidate it
// counted the helper's frame calls, 180 of them, about 1.1 s at 165 Hz, which could end the
// black before GAME SAVED did, when the ending still waited for it.
constexpr int kDarkFrames = 240;
// The ending after YES fades in from black, a level each this many frames (event 3667's step,
// run back up): black for its first 8 frames, then the normal level 40 frames in. Before, the
// routine's own frame was drawn at the normal level, possibly still at the Landing Pod (by
// disassembly; not compared in game: docs/re/memory.md, "The mission prompt").
constexpr int8_t kEndingFadeStep = 8;
extern const char kMissionQuestion[];
// The status from the frame the helper sets the ending routine until another save is loaded
// (left_save); the client reads it (client/discs.py, ENDING_STARTED).
extern const char kEndingStarted[];

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
    // kFadeMessageLeft frames (game_calls.cpp; for the ending, any message of the game's, but a
    // popup of the helper's ended). `away`: the player in
    // play outside home's cell (the Landing Pod's, given with the request). Sets the opener (or,
    // after YES, the ending) once the game is idle, and gives up on an opener another transition
    // took the place of (asked again; taken_over). A request not yet opened lapses while `away`
    // (popups held it until the player had left the Landing Pod): the client asks again after
    // the next save there.
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

    // Game thread, from YES once the game's NO has returned: if the fade reads as NO's close
    // leaves it (the builder's: level -1, toward 4 a step every 6 frames, the countdown 6), the
    // fade off there (target -1, step and countdown 0, as event 3667's fade ends), so the screen
    // stays black until the ending. Else left to fade back in.
    void after_no(const GameAddresses& addresses);

    // Game thread, from the ending's wrapper (hooks.mission_end), which the helper sets as the
    // transition in the ending routine's place once idle after YES: runs the game's ending
    // routine, as the main loop would have (only once per wrapper set: called again, or after
    // the save was left, it only takes the wrapper off); then, if the screen was the helper's
    // black, the level back to black at once (the routine set it to 4) and the game's fade toward
    // 4 a step every kEndingFadeStep frames, before the game draws the frame. A wrapper left as the
    // transition after it (the routine not as traced) becomes the idle routine.
    void end(const GameAddresses& addresses);

    // Game thread: the screen kept black after YES, the ending not started yet: popups and
    // received deaths wait (game_calls.cpp).
    bool dark() const { return dark_; }
    // The ending started by the helper in this save's load (until left_save; dropped again if a
    // save took the ending routine's place before it ran): grants, popups, received shields and
    // deaths wait too, as its cutscenes read as play (game_calls.cpp).
    bool started() const { return started_; }

    // Game thread: out of the black after YES (popups of the helper's wait to be shown): the fade
    // back to the normal level, as NO's close would have; with a menu open (the pause, whose
    // close puts the black back), once it has closed (watch). Nothing if not dark. `why` is what
    // black() then says.
    void end_dark(const GameAddresses& addresses,
                  const char* why = "ended: a popup waiting, faded back in");
    // How the last black after YES in this helper ended, or "kept black" while it lasts, "none"
    // before the first (the `mission_black` request): the ending (faded in from black, or not),
    // kDarkFrames, a popup waiting, cancelled, the save left, or another fade.
    const std::string& black() const { return black_; }

    // Game thread: another save loaded or none (save_loaded changed): the prompt can be offered
    // again after an ending this helper started.
    void left_save();

    // Game thread: puts back what the helper holds (before an unload). The ending's wrapper still
    // the transition becomes the game's ending routine (the ending then runs without the fade
    // in). An opener or wrapper it could not take off stays held (the helper then refuses to
    // unload).
    void restore(const GameAddresses& addresses);

    // Game thread: drops a prompt asked for or the ending after YES, and an opener not yet run
    // (a save loaded or closed, or `discs` turned off), so neither comes in another save; an
    // opener another transition took the place of as in watch (taken_over).
    void cancel(const GameAddresses& addresses);

    // Whether the game holds a pointer into the helper: the opener set, YES wrapped, or the
    // ending's wrapper set.
    bool holds_hook() const { return armed_ || yes_child_ != nullptr || end_set_; }
    // The opener set and not run yet (the fade, then the prompt): popups wait.
    bool opening() const { return armed_; }
    // For a refused unload: a prompt on its way (asked for, opening or shown), the ending after
    // YES; `resume` asks for them again after restore.
    bool asked() const { return requested_ || armed_ || yes_child_ != nullptr; }
    bool ending() const { return ending_; }
    // Whether the ending routine, or the helper's wrapper for it, is the game's transition now
    // (set and not run yet); false without the routine table.
    bool ending_queued(const GameAddresses& addresses) const;
    void resume(bool asked, bool ending);
    const std::string& status() const { return status_; }

private:
    // Event 3667 itself (kOut, to black), or a copy of it aimed at the normal level: from the
    // level as it is, a step every 6 frames (kBack), or from black, a step every
    // kEndingFadeStep frames (kIn).
    enum class Fade { kOut, kBack, kIn };
    // Runs that fade through the type 1 handler: "" or what was not as traced (nothing run).
    std::string fade(const GameAddresses& addresses, Fade kind);
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
    // Takes the opener off before it runs, if its fields can be written: the idle routine,
    // timer 0. The fade is turned back either way.
    void take_back(const GameAddresses& addresses);
    // Whether the fade still reads as after_no left it: black, off.
    bool dark_still_ours(const GameAddresses& addresses) const;
    // The black no longer held, and why.
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
