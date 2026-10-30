// Host tests for RETURN HOME WITH THE DISKS? (mission.h): a stand-in transition (routine,
// timer, routine table), a stand-in prompt builder laid out as 0x1668A0 builds the game's
// SKIP ESCAPE AND FINAL BATTLE? prompt, stand_in.h's event table holding event 3667 (the fade)
// with its handler, and the game's fade, which that handler sets and a stand-in stepper runs.

#include <windows.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "mission.h"
#include "stand_in.h"

namespace {

using namespace stand_in;

int g_calls = 0;
// Bodies of their own: the linker folds identical functions into one address.
void widget_handler() { g_calls += 1; }
void pause_routine() { g_calls += 2; }
void ending_routine() { g_calls += 3; }
void door_routine() { g_calls += 4; }
void escape_yes(void*, int) { g_calls += 5; }
void game_no(void*, int) { g_calls += 6; }
void open_hook() { g_calls += 7; }
void yes_hook(void*, int) { g_calls += 8; }
void* const kIdle = reinterpret_cast<void*>(&pause_routine);  // the routine idle in play

std::map<BYTE*, std::string> g_set;
void text_set(void* text, const char* str, int, int) { g_set[static_cast<BYTE*>(text)] = str; }

// The game's state the prompt touches.
void* g_fn = nullptr;
int32_t g_timer = 0;
BYTE* g_current = nullptr;
void* g_table[16] = {};

// Passes of the stepper (0x1F7900) over the global level: the countdown down while the step is
// set; at 0 the level one toward the target, then the step off there, or the countdown again.
void run_fade(int passes) {
    int8_t& step = at<int8_t>(g_fade, zap::fade_state::kStep);
    int8_t& countdown = at<int8_t>(g_fade, zap::fade_state::kCountdown);
    int8_t target = at<int8_t>(g_fade, zap::fade_state::kTarget);
    for (int i = 0; i < passes; ++i) {
        if (step == 0 || --countdown != 0) continue;
        if (target > g_fade_level) ++g_fade_level;
        if (target < g_fade_level) --g_fade_level;
        if (target == g_fade_level) {
            step = 0;
        } else {
            countdown = step;
        }
    }
}

void builder();
void reset() {
    g_table[zap::kIdleRoutine] = kIdle;
    g_table[zap::kEndingRoutine] = reinterpret_cast<void*>(&ending_routine);
    g_table[zap::kPromptRoutine] = reinterpret_cast<void*>(&builder);
    g_fn = kIdle;  // idle in play
    g_timer = 0;
    g_current = nullptr;
    g_set.clear();
    reset_events();
    g_event_handlers[1] = reinterpret_cast<void*>(&fade_handler);
    reset_fade();
}

// The prompt as built: a menu, one panel of YES, NO and the question (30 glyphs). Made, it is
// the one the builder shows, the game idle in play.
struct Prompt;
Prompt* g_prompt = nullptr;
struct Prompt {
    BYTE menu[0x80] = {};
    BYTE* panels[1] = {};
    BYTE panel[0x300] = {};
    BYTE* children[3] = {};
    BYTE child[3][0x2D0] = {};
    BYTE question[0x268] = {};
    Prompt() {
        g_prompt = this;
        reset();
        at<BYTE*>(menu, 0) = reinterpret_cast<BYTE*>(panels);
        panels[0] = panel;
        at<BYTE**>(panel, 0x1E0) = children;
        at<int32_t>(panel, 0x1E8) = 3;
        void* callbacks[3] = {reinterpret_cast<void*>(&escape_yes),
                              reinterpret_cast<void*>(&game_no), nullptr};
        for (int i = 0; i < 3; ++i) {
            children[i] = child[i];
            at<void*>(child[i], 0x2C0) = callbacks[i];
            at<void*>(child[i], 0x2C8) = reinterpret_cast<void*>(&widget_handler);
        }
        at<BYTE*>(child[2], 0x1F0) = question;
        at<int32_t>(question, 0x1D8) = 30;
    }
};
// The game's builder (0x1668A0): the prompt on top, the transition cleared.
void builder() {
    g_current = g_prompt->menu;
    g_fn = nullptr;
    g_timer = 0;
}

zap::GameAddresses addresses() {
    zap::GameAddresses a;
    a.current_menu = reinterpret_cast<BYTE*>(&g_current);
    a.transition_fn = reinterpret_cast<BYTE*>(&g_fn);
    a.transition_timer = reinterpret_cast<BYTE*>(&g_timer);
    a.routine_table = reinterpret_cast<BYTE*>(g_table);
    a.widget_handler_fn = reinterpret_cast<BYTE*>(&widget_handler);
    a.text_set_fn = reinterpret_cast<BYTE*>(&text_set);
    a.event_table = reinterpret_cast<BYTE*>(&g_event_table);
    a.event_count = reinterpret_cast<BYTE*>(&g_event_count);
    a.event_handlers = reinterpret_cast<BYTE*>(g_event_handlers);
    a.fade = g_fade;
    a.fade_level = reinterpret_cast<BYTE*>(&g_fade_level);
    a.hooks.mission_open = reinterpret_cast<void*>(&open_hook);
    a.hooks.mission_yes = reinterpret_cast<void*>(&yes_hook);
    return a;
}

// A prompt with this program as the game's image (the fade's handler is its code).
zap::MissionPrompt prompt_in_image() {
    zap::MissionPrompt m;
    m.set_image(GetModuleHandleW(nullptr));
    return m;
}

// Asked for, and watched once idle in play: the fade run, the opener set.
zap::MissionPrompt fading(const zap::GameAddresses& a) {
    zap::MissionPrompt m = prompt_in_image();
    m.request();
    m.watch(a, true);
    return m;
}

// The fade back: event 3667 aimed at level 4, the level left as it is, a step every 6 frames,
// and +0xC 1 (none of the side fields).
std::vector<BYTE> fade_back() {
    std::vector<BYTE> r(event_3667(), event_3667() + 0x28);
    const int16_t words[] = {4, 0xFE, 6, 0, 1};
    std::memcpy(r.data() + 4, words, sizeof(words));
    return r;
}

void test_offered_when_idle_then_yes_ends() {
    Prompt prompt;
    zap::MissionPrompt m = prompt_in_image();
    zap::GameAddresses a = addresses();
    m.watch(a, true);
    check(g_fn == kIdle, "not asked: nothing set");
    m.request();
    m.watch(a, false);
    check(g_fn == kIdle, "not idle in play: nothing set");
    g_timer = 5;  // a transition pending
    m.watch(a, true);
    check(g_timer == 5 && g_fn == kIdle, "a transition pending: nothing set");
    g_timer = 0;
    m.watch(a, true);
    check(g_fn == a.hooks.mission_open && g_timer == zap::kFadeDelay && m.holds_hook() &&
              m.opening(),
          "the opener set with event 3666's delay");
    check(g_fades.size() == 1 && g_fade_event == event_3667() &&
              std::memcmp(g_fades[0].data(), event_3667(), 0x28) == 0,
          "event 3667 run, the game's own record");
    check(m.status() == "opening after the fade", m.status().c_str());

    // The game runs the opener: the prompt built, the question relabeled, YES the helper's.
    check(m.open(a).empty(), m.status().c_str());
    check(!m.opening() && m.holds_hook(), "opened: popups go on, YES still held");
    check(g_set[prompt.question] == zap::kMissionQuestion, "the question relabeled");
    check(at<void*>(prompt.child[0], 0x2C0) == a.hooks.mission_yes, "YES the helper's");
    check(at<void*>(prompt.child[1], 0x2C0) == reinterpret_cast<void*>(&game_no),
          "NO the game's");

    // YES: the game's NO to close it, then the ending once idle.
    check(m.yes() == reinterpret_cast<void*>(&game_no), "YES calls the game's NO");
    m.watch(a, true);
    check(g_fn == nullptr, "not while the prompt is still up");
    g_current = nullptr;  // closed: the game's close sets the pause routine back
    g_fn = kIdle;
    m.watch(a, true);
    check(g_fn == reinterpret_cast<void*>(&ending_routine) && g_timer == 1,
          "the ending, as the ship's event starts it");
    check(!m.holds_hook() && m.status() == "ending started", "and nothing held");
    check(g_fades.size() == 1, "no fade back: NO's own close fades in");
}

void test_another_transition_first_is_asked_again() {
    Prompt prompt;
    zap::GameAddresses a = addresses();
    zap::MissionPrompt m = fading(a);
    run_fade(1);
    m.watch(a, true);
    g_fn = reinterpret_cast<void*>(&door_routine);  // a door took the routine, with its fade
    g_timer = 30;
    set_fade(-1, static_cast<int8_t>(0xFE), 12);
    m.watch(a, true);
    check(!m.holds_hook() && !m.opening(), "given up");
    check(g_fn == reinterpret_cast<void*>(&door_routine) && g_timer == 30 && g_fades.size() == 1,
          "the door's routine and fade left alone");
    check(m.status() == "asked again: another transition came first", m.status().c_str());
    g_fn = kIdle;
    g_timer = 0;
    m.watch(a, true);
    check(g_fn == a.hooks.mission_open && g_fades.size() == 2,
          "asked again once idle: fades again");

    // Taken over again, then cancelled: nothing of the door's turned back.
    g_fn = reinterpret_cast<void*>(&door_routine);
    set_fade(-1, static_cast<int8_t>(0xFE), 12);
    m.cancel(a);
    check(g_fn == reinterpret_cast<void*>(&door_routine) && g_fades.size() == 2 && !m.holds_hook(),
          "cancelled under a door: its routine and fade left alone");
}

void test_unload_puts_back() {
    Prompt prompt;
    zap::GameAddresses a = addresses();
    zap::MissionPrompt m = fading(a);
    check(m.holds_hook(), "while it fades, the game holds the opener");
    m.restore(a);
    check(g_fn == kIdle && g_timer == 0 && !m.holds_hook(),
          "an opener not run: the idle routine back");
    check(g_fades.size() == 2 && g_fades[1] == fade_back(), "and the fade turned back");

    reset();
    m.request();
    m.watch(a, true);
    m.open(a);
    m.restore(a);
    check(at<void*>(prompt.child[0], 0x2C0) == reinterpret_cast<void*>(&game_no),
          "a prompt up: YES is NO, never the game's own YES");
    check(!m.holds_hook(), "nothing held");
    check(g_fades.size() == 1, "no fade back once the prompt is up: NO's close does that");
}

void test_an_unexpected_prompt_never_keeps_the_games_yes() {
    Prompt prompt;
    at<int32_t>(prompt.question, 0x1D8) = 10;  // a question too short for ours
    zap::GameAddresses a = addresses();
    zap::MissionPrompt m = fading(a);
    check(!m.open(a).empty(), "reported");
    check(at<void*>(prompt.child[0], 0x2C0) == reinterpret_cast<void*>(&game_no),
          "YES is NO: the escape never skipped outside it");
    check(g_set.empty() && !m.holds_hook(), "nothing relabeled, nothing held");
}

void test_cancel_drops_it_for_another_save() {
    Prompt prompt;
    zap::MissionPrompt m = prompt_in_image();
    zap::GameAddresses a = addresses();
    m.request();
    m.cancel(a);  // the save changed before it was idle
    m.watch(a, true);
    check(g_fn == kIdle, "asked, then cancelled: never set");

    m.request();
    m.watch(a, true);  // the opener set, the screen fading
    m.cancel(a);
    check(g_fn == kIdle && g_timer == 0 && !m.holds_hook() && !m.opening(),
          "an opener not run: taken off");
    check(g_fades.size() == 2 && g_fades[1] == fade_back(),
          "the fade turned back to the normal level, from the level it reached");

    m.request();
    m.watch(a, true);
    m.open(a);
    m.yes();
    m.cancel(a);  // YES, then the save changed before the ending
    g_current = nullptr;
    g_fn = kIdle;
    m.watch(a, true);
    check(g_fn == kIdle, "no ending in another save");
}

// Event 3667 not as traced, or its handler not the game's code: the prompt at once, as before.
void test_no_fade_unless_as_traced() {
    Prompt prompt;
    zap::GameAddresses a = addresses();
    struct Case {
        const char* what;
        void (*change)();
        const char* why;
    };
    const Case cases[] = {
        {"another step", [] { event_3667()[8] = 9; }, "event 3667 not as traced"},
        {"a next event", [] { event_3667()[0x20] = 0; }, "event 3667 not as traced"},
        {"too few events", [] { g_event_count = zap::kFadeEvent; }, "no event 3667"},
        {"no table", [] { g_event_table = nullptr; }, "no event 3667"},
        {"a handler in data", [] { g_event_handlers[1] = g_events; },
         "its handler not in the game's code"},
    };
    for (const Case& c : cases) {
        reset();
        c.change();
        zap::MissionPrompt m = prompt_in_image();
        m.request();
        m.watch(a, true);
        check(g_fn == a.hooks.mission_open && g_timer == 1 && g_fades.empty() &&
                  m.status() == std::string("opening without the fade: ") + c.why,
              c.what);
        m.cancel(a);
        check(g_fn == kIdle && g_fades.empty(), "nothing to turn back");
    }
    reset();
    zap::MissionPrompt no_image;  // configured without the game's image
    no_image.request();
    no_image.watch(a, true);
    check(g_timer == 1 && g_fades.empty(), "no image: no fade");
}

// The opener run with no builder in the table any more: the idle routine back, the fade back.
void test_no_builder_after_the_fade() {
    Prompt prompt;
    zap::GameAddresses a = addresses();
    zap::MissionPrompt m = fading(a);
    g_table[zap::kPromptRoutine] = nullptr;
    g_timer = 0;  // the game runs the opener
    check(m.open(a) == "no prompt builder", m.status().c_str());
    check(g_fn == kIdle && g_timer == 0 && !m.holds_hook() && g_fades.size() == 2 &&
              g_fades[1] == fade_back(),
          "the idle routine back and the screen out of the fade");
}


// A save at the save point during the fade sets the pause opener back in the opener's place and
// brings no fade (0x1FC8F1): the fade turned back, mid-way or done, and asked again once idle.
void test_a_save_during_the_fade_turns_it_back() {
    for (int passes : {0, 1, 8, 20, 39, 40, 55}) {
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = fading(a);
        for (int i = 0; i < passes; ++i) {
            run_fade(1);
            m.watch(a, true);
        }
        run_fade(1);
        int8_t level = g_fade_level;
        g_fn = kIdle;  // saved: the pause opener back, the timer still running
        g_timer = zap::kFadeDelay - passes - 1 > 0 ? zap::kFadeDelay - passes - 1 : 0;
        m.watch(a, true);
        std::string what = "a save after " + std::to_string(passes) + " passes";
        check(!m.holds_hook() && !m.opening(), what.c_str());
        check(g_fades.size() == 2 && g_fades[1] == fade_back(), (what + ": faded back").c_str());
        check(g_fade_level == level && g_fade[zap::fade_state::kTarget] == 4 &&
                  g_fade[zap::fade_state::kStep] == 6,
              (what + ": toward level 4 from where it was").c_str());
        check(m.status() == "asked again: another transition came first, the fade turned back",
              m.status().c_str());
        run_fade(30);
        check(g_fade_level == 4, (what + ": back at the normal level").c_str());
        g_timer = 0;
        m.watch(a, true);
        check(g_fn == a.hooks.mission_open && g_fades.size() == 3, "asked again once idle");
    }
}

// Another transition with a fade of its own: left alone, even one that sets 3667's own values
// (0x1D93DE: the level back to 4, then target -1, a step every 8 frames), or a level set since.
// With the timer lower than when last seen, the fade alone tells; in the first frames, or with
// the fade's end state set at once, the timer the transition sets (0x1D93DE: 70) does.
void test_another_fade_is_left_alone() {
    struct Case {
        const char* what;
        int passes;
        int32_t timer;
        void (*fade)();
    };
    const Case cases[] = {
        {"a fade to black of its own", 20, 30,
         [] { set_fade(-1, static_cast<int8_t>(0xFE), 12); }},
        {"3667's values over again", 20, 30, [] { set_fade(-1, 4, 8); }},
        {"3667's values once done", 45, 1, [] { set_fade(-1, 4, 8); }},
        {"a fade in", 45, 1, [] { set_fade(4, static_cast<int8_t>(0xFE), 6); }},
        {"a level set at once", 45, 1, [] { g_fade_level = 4; }},
        {"3667's values in the first frame", 0, 70, [] { set_fade(-1, 4, 8); }},
        {"black at once after 3667's end", 45, 70, [] { set_fade(-1, -1, 0); }},
    };
    for (const Case& c : cases) {
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = fading(a);
        for (int i = 0; i < c.passes; ++i) {
            run_fade(1);
            m.watch(a, true);
        }
        g_fn = reinterpret_cast<void*>(&door_routine);
        g_timer = c.timer;
        c.fade();
        m.watch(a, true);
        check(!m.holds_hook() && g_fades.size() == 1, c.what);
        check(g_fn == reinterpret_cast<void*>(&door_routine) && g_timer == c.timer, c.what);
    }
}

// `discs` turned off (cancel) after a save took the opener's place, before the helper saw it:
// the fade turned back there too, and nothing asked again.
void test_cancel_after_a_save_turns_the_fade_back() {
    Prompt prompt;
    zap::GameAddresses a = addresses();
    zap::MissionPrompt m = fading(a);
    run_fade(25);
    m.watch(a, true);
    run_fade(1);
    g_fn = kIdle;  // saved
    m.cancel(a);
    check(!m.holds_hook() && !m.asked() && m.status() == "not asked", m.status().c_str());
    check(g_fades.size() == 2 && g_fades[1] == fade_back(), "faded back");
    g_timer = 0;
    m.watch(a, true);
    check(g_fn == kIdle && g_fades.size() == 2, "not asked again");
}

}  // namespace

int main() {
    test_cancel_drops_it_for_another_save();
    test_offered_when_idle_then_yes_ends();
    test_another_transition_first_is_asked_again();
    test_unload_puts_back();
    test_an_unexpected_prompt_never_keeps_the_games_yes();
    test_no_fade_unless_as_traced();
    test_no_builder_after_the_fade();
    test_a_save_during_the_fade_turns_it_back();
    test_another_fade_is_left_alone();
    test_cancel_after_a_save_turns_the_fade_back();
    return report("mission tests");
}
