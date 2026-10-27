// Host tests for RETURN HOME WITH THE DISKS? (mission.h): a stand-in transition (routine,
// timer, routine table), a stand-in prompt builder laid out as 0x1668A0 builds the escape
// skip's prompt, and a stand-in event table holding event 3667 (the fade) with its handler.

#include <windows.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "mission.h"

namespace {

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

// The area events up to 3667, the fade as B1 loads it, and the handlers by type: type 1's
// records each record it was given, as it was then.
constexpr int kEvents = zap::kFadeEvent + 1;
BYTE g_events[kEvents * 0x28];
BYTE* g_event_table = g_events;
int16_t g_event_count = kEvents;
BYTE* fade_event() { return g_events + zap::kFadeEvent * 0x28; }
const BYTE kFade[0x28] = {0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x04, 0x00, 0x08, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
                          0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x53, 0x0E, 0x00, 0x00};
std::vector<std::vector<BYTE>> g_fades;  // each record the handler ran
BYTE* g_fade_given = nullptr;
void fade_handler(char, char, BYTE* event) {
    g_fade_given = event;
    g_fades.emplace_back(event, event + 0x28);
}
void* g_handlers[32] = {};

void builder();
void reset() {
    g_table[zap::kIdleRoutine] = kIdle;
    g_table[zap::kEndingRoutine] = reinterpret_cast<void*>(&ending_routine);
    g_table[zap::kPromptRoutine] = reinterpret_cast<void*>(&builder);
    g_fn = kIdle;  // idle in play
    g_timer = 0;
    g_current = nullptr;
    g_set.clear();
    std::memcpy(fade_event(), kFade, sizeof(kFade));
    g_event_table = g_events;
    g_event_count = kEvents;
    g_handlers[1] = reinterpret_cast<void*>(&fade_handler);
    g_fades.clear();
    g_fade_given = nullptr;
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
    a.event_handlers = reinterpret_cast<BYTE*>(g_handlers);
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

// The fade back: event 3667 aimed at level 4, the level left as it is, a step every 6 frames,
// and +0xC 1 (none of the side fields).
std::vector<BYTE> fade_back() {
    std::vector<BYTE> r(kFade, kFade + 0x28);
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
    check(g_fades.size() == 1 && g_fade_given == fade_event() &&
              std::memcmp(g_fades[0].data(), kFade, sizeof(kFade)) == 0,
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
    zap::MissionPrompt m = prompt_in_image();
    zap::GameAddresses a = addresses();
    m.request();
    m.watch(a, true);
    g_fn = reinterpret_cast<void*>(&door_routine);  // a door took the routine
    g_timer = 30;
    m.watch(a, true);
    check(!m.holds_hook() && !m.opening(), "given up");
    check(g_fn == reinterpret_cast<void*>(&door_routine) && g_timer == 30 && g_fades.size() == 1,
          "the door's routine and fade left alone");
    g_fn = kIdle;
    g_timer = 0;
    m.watch(a, true);
    check(g_fn == a.hooks.mission_open && g_fades.size() == 2,
          "asked again once idle: fades again");

    // Taken over again, then cancelled: nothing of the door's turned back.
    g_fn = reinterpret_cast<void*>(&door_routine);
    m.cancel(a);
    check(g_fn == reinterpret_cast<void*>(&door_routine) && g_fades.size() == 2 && !m.holds_hook(),
          "cancelled under a door: its routine and fade left alone");
}

void test_unload_puts_back() {
    Prompt prompt;
    zap::MissionPrompt m = prompt_in_image();
    zap::GameAddresses a = addresses();
    m.request();
    m.watch(a, true);
    check(m.holds_hook(), "while it fades, the game holds the opener: an unload refuses");
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
          "a prompt up: YES is NO, never the escape skip's YES");
    check(!m.holds_hook(), "nothing held");
}

void test_an_unexpected_prompt_never_keeps_the_escape_skip() {
    Prompt prompt;
    at<int32_t>(prompt.question, 0x1D8) = 10;  // a question too short for ours
    zap::MissionPrompt m = prompt_in_image();
    zap::GameAddresses a = addresses();
    m.request();
    m.watch(a, true);
    check(!m.open(a).empty(), "reported");
    check(at<void*>(prompt.child[0], 0x2C0) == reinterpret_cast<void*>(&game_no),
          "YES is NO: no escape skip outside the escape");
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
        {"another step", [] { fade_event()[8] = 9; }, "event 3667 not as traced"},
        {"a next event", [] { fade_event()[0x20] = 0; }, "event 3667 not as traced"},
        {"too few events", [] { g_event_count = zap::kFadeEvent; }, "no event 3667"},
        {"no table", [] { g_event_table = nullptr; }, "no event 3667"},
        {"a handler in data", [] { g_handlers[1] = g_events; },
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
    zap::MissionPrompt m = prompt_in_image();
    zap::GameAddresses a = addresses();
    m.request();
    m.watch(a, true);
    g_table[zap::kPromptRoutine] = nullptr;
    g_timer = 0;  // the game runs the opener
    check(m.open(a) == "no prompt builder", m.status().c_str());
    check(g_fn == kIdle && g_timer == 0 && !m.holds_hook() && g_fades.size() == 2 &&
              g_fades[1] == fade_back(),
          "the idle routine back and the screen out of the fade");
}

}  // namespace

int main() {
    test_cancel_drops_it_for_another_save();
    test_offered_when_idle_then_yes_ends();
    test_another_transition_first_is_asked_again();
    test_unload_puts_back();
    test_an_unexpected_prompt_never_keeps_the_escape_skip();
    test_no_fade_unless_as_traced();
    test_no_builder_after_the_fade();
    return report("mission tests");
}
