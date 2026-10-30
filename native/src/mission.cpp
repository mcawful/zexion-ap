#include "mission.h"

#include <cstring>

#include "game_mem.h"

namespace zap {

// Centered in the 30-glyph line the prompt's builder gives the question (0x166D9F).
const char kMissionQuestion[] = " RETURN HOME WITH THE DISKS?";

namespace {

// The prompt as 0x1668A0 builds it: one panel of three children, YES, NO and the question.
constexpr int32_t kPromptChildren = 3;
constexpr int kMinQuestionGlyphs = static_cast<int>(sizeof(kMissionQuestion) - 1);

// Event 3667 as B1 loads it (docs/re/memory.md, "The mission prompt"): type 1, layers 0 to 0,
// the global level to 4 then toward -1 (black), a step every 8 frames; no sound, no next event.
// The handler (0xD6A0) reads only the record, and its two arguments only to pass to a next one.
constexpr uint8_t kFadeType = 1;
constexpr BYTE kFadeRecord[event::kSize] = {
    0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x53, 0x0E, 0x00, 0x00};
// Back, as the menu close puts the fade the prompt's builder saved (0x92F1B): toward level 4, a
// step every 6 frames, from the level now (0xFE: the level left as it is, 0x1F6E1B).
constexpr ptrdiff_t kFadeTarget = event::kArg1, kFadeLevel = event::kArg2, kFadeStep = event::kArg3;
constexpr int16_t kNormalLevel = 4, kKeepLevel = 0xFE, kBackStep = 6;
constexpr ptrdiff_t kFadeSide = 0xC;
constexpr int16_t kNoSide = 1;
using EventFn = void (*)(char, char, BYTE*);

// Event 3667's fade as the stepper (0x1F7900, once each pass of the main loop) runs it: from
// level 4 (the record's), the countdown from 8 (its step) down; at 0 the level one lower and the
// countdown 8 again, until the level is the target, -1, and the stepper turns the step off
// (0x1F7BFF): 40 frames. How many of them it has run, or -1 if the fade holds anything else.
constexpr int8_t kFadeFrom = static_cast<int8_t>(kFadeRecord[kFadeLevel]);
constexpr int8_t kFadeTo = static_cast<int8_t>(kFadeRecord[kFadeTarget]);
constexpr int8_t kFadeEvery = static_cast<int8_t>(kFadeRecord[kFadeStep]);
static_assert(kFadeFrom == kNormalLevel && kFadeTo == -1 && kFadeEvery == 8, "as traced");
constexpr int kFadeFrames = (kFadeFrom - kFadeTo) * kFadeEvery;
int fade_frames(const GameAddresses& a) {
    int8_t target = 0, step = 0, countdown = 0, level = 0;
    if (!read(a.fade, fade_state::kTarget, target) || !read(a.fade, fade_state::kStep, step) ||
        !read(a.fade, fade_state::kCountdown, countdown) || !read(a.fade_level, 0, level) ||
        target != kFadeTo) {
        return -1;
    }
    if (step == 0) return countdown == 0 && level == kFadeTo ? kFadeFrames : -1;
    if (step != kFadeEvery || level < 0 || level > kFadeFrom || countdown < 1 ||
        countdown > kFadeEvery) {
        return -1;
    }
    return (kFadeFrom - level) * kFadeEvery + (kFadeEvery - countdown);
}

void* routine(const GameAddresses& a, int index) { return pointer_at(a.routine_table, index * 8); }

bool writable_fields(const GameAddresses& a) {
    return writable(a.transition_fn, sizeof(void*)) &&
           writable(a.transition_timer, sizeof(int32_t));
}

}  // namespace

void MissionPrompt::request() {
    if (yes_child_ != nullptr || armed_ || ending_) return;  // already on its way
    requested_ = true;
    status_ = "asked";
}

void MissionPrompt::resume(bool asked, bool ending) {
    if (ending) {
        ending_ = true;
        status_ = "yes: ending when idle";
    } else if (asked) {
        request();
    }
}

void MissionPrompt::watch(const GameAddresses& a, bool idle_in_play, bool away) {
    void* fn = nullptr;
    int32_t timer = -1;
    BYTE* top = nullptr;
    if (!read(a.transition_fn, 0, fn) || !read(a.transition_timer, 0, timer) ||
        !read(a.current_menu, 0, top)) {
        return;
    }
    if (yes_child_ != nullptr && !is_open(a.current_menu, menu_)) {
        yes_child_ = nullptr;  // closed (NO): the game freed it
        menu_ = nullptr;
    }
    if (armed_ && fn != a.hooks.mission_open) {
        // Not ours any more and not run (open() clears armed_): another transition took the
        // place (a save, a door, a death). Asked again once idle.
        bool back = taken_over(a);
        requested_ = true;
        status_ = back ? "asked again: another transition came first, the fade turned back"
                       : "asked again: another transition came first";
    } else if (armed_ && faded_) {
        fade_seen_ = fade_frames(a);  // the opener still in place: how far the fade has got
        timer_seen_ = timer;
    }
    if (requested_ && away) {
        requested_ = false;
        status_ = "lapsed: the player left the landing pod";
    }
    void* idle_fn = routine(a, kIdleRoutine);
    bool idle = idle_in_play && timer == 0 && fn != nullptr && fn == idle_fn && top == nullptr &&
                writable_fields(a);
    if (!idle) return;
    if (ending_) {
        void* ending = routine(a, kEndingRoutine);
        if (ending == nullptr) return;
        put(a.transition_fn, 0, ending);
        put<int32_t>(a.transition_timer, 0, 1);
        ending_ = false;
        status_ = "ending started";
        return;
    }
    if (requested_ && a.hooks.mission_open != nullptr && routine(a, kPromptRoutine) != nullptr) {
        // As event 3666: the routine and its delay, then event 3667's fade. armed_ is set
        // before the fade runs game code, so a fault there still counts the opener as held.
        put(a.transition_fn, 0, a.hooks.mission_open);
        put<int32_t>(a.transition_timer, 0, 1);  // no fade: at once
        requested_ = false;
        armed_ = true;
        std::string why = fade(a, true);
        faded_ = why.empty();
        fade_seen_ = faded_ ? fade_frames(a) : -1;
        if (faded_) {
            put<int32_t>(a.transition_timer, 0, kFadeDelay);
            timer_seen_ = kFadeDelay;
        }
        status_ = faded_ ? "opening after the fade" : "opening without the fade: " + why;
    }
}

std::string MissionPrompt::fade(const GameAddresses& a, bool out) {
    BYTE* table = pointer_at(a.event_table);
    int16_t events = 0;
    if (table == nullptr || !read(a.event_count, 0, events) || events <= kFadeEvent) {
        return "no event 3667";
    }
    BYTE* ev = table + static_cast<ptrdiff_t>(kFadeEvent) * event::kSize;
    BYTE copy[event::kSize];
    if (!safe_read(ev, copy, sizeof(copy)) || std::memcmp(copy, kFadeRecord, sizeof(copy)) != 0) {
        return "event 3667 not as traced";
    }
    BYTE* handler = pointer_at(a.event_handlers, kFadeType * sizeof(void*));
    if (handler == nullptr || image_ == nullptr || !in_image_code(image_, handler)) {
        return "its handler not in the game's code";
    }
    if (out) {
        reinterpret_cast<EventFn>(handler)(0, 0, ev);
    } else {
        std::memcpy(copy + kFadeTarget, &kNormalLevel, sizeof(int16_t));
        std::memcpy(copy + kFadeLevel, &kKeepLevel, sizeof(int16_t));
        std::memcpy(copy + kFadeStep, &kBackStep, sizeof(int16_t));
        // +0xC 1: none of the side fields 3667 also sets (0xD7C3), which the game's own fade
        // back (the menu close) leaves alone too.
        std::memcpy(copy + kFadeSide, &kNoSide, sizeof(int16_t));
        reinterpret_cast<EventFn>(handler)(0, 0, copy);
    }
    return "";
}

bool MissionPrompt::fade_back(const GameAddresses& a) {
    // The record checked again: skipped if it changed since.
    bool back = faded_ && fade(a, false).empty();
    faded_ = false;
    fade_seen_ = -1;
    return back;
}

bool MissionPrompt::fade_still_ours(const GameAddresses& a) const {
    int32_t timer = 0;
    if (!faded_ || fade_seen_ < 0 || !read(a.transition_timer, 0, timer)) return false;
    return fade_frames(a) >= fade_seen_ && timer <= timer_seen_;
}

bool MissionPrompt::taken_over(const GameAddresses& a) {
    armed_ = false;
    if (!fade_still_ours(a)) faded_ = false;  // another fade, or none ran: left alone
    return fade_back(a);
}

void MissionPrompt::take_back(const GameAddresses& a) {
    if (writable_fields(a)) {
        put(a.transition_fn, 0, routine(a, kIdleRoutine));
        put<int32_t>(a.transition_timer, 0, 0);
        armed_ = false;
    }
    fade_back(a);
}

void MissionPrompt::put_off(const GameAddresses& a) {
    armed_ = false;
    if (!fade_still_ours(a)) faded_ = false;  // the death's own fade, or none ran: left alone
    take_back(a);
    requested_ = true;
    status_ = "asked again: not in play when it opened";
}

std::string MissionPrompt::open(const GameAddresses& a) {
    armed_ = false;
    auto builder = reinterpret_cast<void (*)()>(routine(a, kPromptRoutine));
    if (builder == nullptr) {
        take_back(a);  // not built: the idle routine back, and the screen out of the fade
        return status_ = "no prompt builder";
    }
    faded_ = false;  // the builder sets the level and the fade back in itself
    builder();
    // The prompt the game just built: the top menu, its panel's three children.
    BYTE* menu = pointer_at(a.current_menu);
    BYTE* child[kPromptChildren] = {};
    if (children_of(panel_at(menu, 0), child, kPromptChildren) != kPromptChildren) {
        return status_ = "not the prompt as traced";
    }
    // First of all, YES does what NO does: whatever else is not as traced, the game's own
    // YES (which skips the escape and final battle) never stays on a prompt outside the escape.
    void* first_no = nullptr;
    if (read(child[1], widget::kCallback, first_no) && first_no != nullptr &&
        writable(child[0] + widget::kCallback, sizeof(void*))) {
        put(child[0], widget::kCallback, first_no);
    }
    // Each child live with the widget handler; the callbacks of NO and the question.
    void* callbacks[kPromptChildren] = {first_no};  // YES, now NO's
    for (int i = 0; i < kPromptChildren; ++i) {
        if (!live_widget(child[i], a.widget_handler_fn) ||
            (i > 0 && !read(child[i], widget::kCallback, callbacks[i]))) {
            return status_ = "not the prompt as traced";
        }
    }
    BYTE* question = nullptr;
    int32_t glyphs = 0;
    void* no = callbacks[1];
    if (no == nullptr || callbacks[0] != no || callbacks[2] != nullptr) {
        return status_ = "not the prompt as traced";
    }
    if (!read(child[2], widget::kLabel, question) || !read(question, label::kLength, glyphs) ||
        glyphs < kMinQuestionGlyphs) {
        return status_ = "the question's label is not as traced: YES is NO";
    }
    reinterpret_cast<void (*)(void*, const char*, int, int)>(a.text_set_fn)(
        question, kMissionQuestion, 0, glyphs);
    if (a.hooks.mission_yes != nullptr) put(child[0], widget::kCallback, a.hooks.mission_yes);
    no_fn_ = no;
    yes_child_ = child[0];
    menu_ = menu;
    status_ = "shown";
    return "";
}

void* MissionPrompt::yes() {
    void* no = no_fn_;
    yes_child_ = nullptr;  // the prompt closes
    menu_ = nullptr;
    ending_ = true;
    status_ = "yes: ending when idle";
    return no;
}

bool MissionPrompt::ending_queued(const GameAddresses& a) const {
    void* fn = nullptr;
    if (a.routine_table == nullptr || !read(a.transition_fn, 0, fn) || fn == nullptr) return false;
    return fn == routine(a, kEndingRoutine);
}

void MissionPrompt::cancel(const GameAddresses& a) {
    requested_ = false;
    ending_ = false;
    void* fn = nullptr;
    if (armed_ && read(a.transition_fn, 0, fn)) {
        if (fn != a.hooks.mission_open) {
            taken_over(a);  // not ours any more; the fade turned back if it still is
        } else if (writable_fields(a)) {
            take_back(a);  // else still armed_: held
        }
    }
    if (!armed_ && yes_child_ == nullptr) status_ = "not asked";
}

void MissionPrompt::restore(const GameAddresses& a) {
    cancel(a);  // an opener it could not take off stays armed_: still held
    if (yes_child_ != nullptr && is_open(a.current_menu, menu_) &&
        pointer_at(yes_child_, widget::kCallback) == a.hooks.mission_yes &&
        !swap_pointer(yes_child_, widget::kCallback, a.hooks.mission_yes, no_fn_)) {
        return;  // still the helper's: kept, so an unload refuses
    }
    yes_child_ = nullptr;
    menu_ = nullptr;
}

}  // namespace zap
