// Host tests for END THE MISSION? (mission.h): a stand-in transition (routine, timer, routine
// table) and a stand-in prompt builder laid out as 0x1668A0 builds the escape skip's prompt.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "mission.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

template <typename T>
T& at(BYTE* p, ptrdiff_t offset) {
    return *reinterpret_cast<T*>(p + offset);
}

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

std::map<BYTE*, std::string> g_set;
void text_set(void* text, const char* str, int, int) { g_set[static_cast<BYTE*>(text)] = str; }

// The game's state the prompt touches.
void* g_fn = nullptr;
int32_t g_timer = 0;
BYTE* g_current = nullptr;
void* g_table[16] = {};

// The prompt as built: a menu, one panel of YES, NO and the question (30 glyphs).
struct Prompt {
    BYTE menu[0x80] = {};
    BYTE* panels[1] = {};
    BYTE panel[0x300] = {};
    BYTE* children[3] = {};
    BYTE child[3][0x2D0] = {};
    BYTE question[0x268] = {};
    Prompt() {
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
Prompt* g_prompt = nullptr;
// The game's builder (0x1668A0): the prompt on top, the transition cleared.
void builder() {
    g_current = g_prompt->menu;
    g_fn = nullptr;
    g_timer = 0;
}

zap::MissionAddresses addresses() {
    zap::MissionAddresses a;
    a.current_menu = reinterpret_cast<BYTE*>(&g_current);
    a.transition_fn = reinterpret_cast<BYTE*>(&g_fn);
    a.transition_timer = reinterpret_cast<BYTE*>(&g_timer);
    a.routine_table = reinterpret_cast<BYTE*>(g_table);
    a.widget_handler = reinterpret_cast<void*>(&widget_handler);
    a.text_set_fn = reinterpret_cast<void*>(&text_set);
    a.open_hook = reinterpret_cast<void*>(&open_hook);
    a.yes_hook = reinterpret_cast<void*>(&yes_hook);
    return a;
}

void reset() {
    g_table[zap::kIdleRoutine] = reinterpret_cast<void*>(&pause_routine);
    g_table[zap::kEndingRoutine] = reinterpret_cast<void*>(&ending_routine);
    g_table[zap::kPromptRoutine] = reinterpret_cast<void*>(&builder);
    g_fn = reinterpret_cast<void*>(&pause_routine);  // idle in play
    g_timer = 0;
    g_current = nullptr;
    g_set.clear();
}

void test_offered_when_idle_then_yes_ends() {
    Prompt prompt;
    g_prompt = &prompt;
    reset();
    zap::MissionPrompt m;
    zap::MissionAddresses a = addresses();
    m.watch(a, true);
    check(g_fn == reinterpret_cast<void*>(&pause_routine), "not asked: nothing set");
    m.request();
    m.watch(a, false);
    check(g_fn == reinterpret_cast<void*>(&pause_routine), "not idle in play: nothing set");
    g_timer = 5;  // a transition pending
    m.watch(a, true);
    check(g_timer == 5 && g_fn == reinterpret_cast<void*>(&pause_routine),
          "a transition pending: nothing set");
    g_timer = 0;
    m.watch(a, true);
    check(g_fn == a.open_hook && g_timer == 1 && m.holds_hook(), "the opener set, timer 1");

    // The game runs the opener: the prompt built, the question relabeled, YES the helper's.
    check(m.open(a).empty(), m.status().c_str());
    check(g_set[prompt.question] == zap::kMissionQuestion, "the question relabeled");
    check(at<void*>(prompt.child[0], 0x2C0) == a.yes_hook, "YES the helper's");
    check(at<void*>(prompt.child[1], 0x2C0) == reinterpret_cast<void*>(&game_no),
          "NO the game's");

    // YES: the game's NO to close it, then the ending once idle.
    check(m.yes() == reinterpret_cast<void*>(&game_no), "YES calls the game's NO");
    m.watch(a, true);
    check(g_fn == nullptr, "not while the prompt is still up");
    g_current = nullptr;  // closed: the game's close sets the pause routine back
    g_fn = reinterpret_cast<void*>(&pause_routine);
    m.watch(a, true);
    check(g_fn == reinterpret_cast<void*>(&ending_routine) && g_timer == 1,
          "the ending, as the ship's event starts it");
    check(!m.holds_hook() && m.status() == "ending started", "and nothing held");
}

void test_another_transition_first_is_asked_again() {
    Prompt prompt;
    g_prompt = &prompt;
    reset();
    zap::MissionPrompt m;
    zap::MissionAddresses a = addresses();
    m.request();
    m.watch(a, true);
    g_fn = reinterpret_cast<void*>(&door_routine);  // a door took the routine
    g_timer = 30;
    m.watch(a, true);
    check(!m.holds_hook(), "given up");
    g_fn = reinterpret_cast<void*>(&pause_routine);
    g_timer = 0;
    m.watch(a, true);
    check(g_fn == a.open_hook, "asked again once idle");
}

void test_unload_puts_back() {
    Prompt prompt;
    g_prompt = &prompt;
    reset();
    zap::MissionPrompt m;
    zap::MissionAddresses a = addresses();
    m.request();
    m.watch(a, true);
    m.restore(a);
    check(g_fn == reinterpret_cast<void*>(&pause_routine) && g_timer == 0 && !m.holds_hook(),
          "an opener not run: the idle routine back");

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
    g_prompt = &prompt;
    reset();
    at<int32_t>(prompt.question, 0x1D8) = 10;  // a question too short for ours
    zap::MissionPrompt m;
    zap::MissionAddresses a = addresses();
    m.request();
    m.watch(a, true);
    check(!m.open(a).empty(), "reported");
    check(at<void*>(prompt.child[0], 0x2C0) == reinterpret_cast<void*>(&game_no),
          "YES is NO: no escape skip outside the escape");
    check(g_set.empty() && !m.holds_hook(), "nothing relabeled, nothing held");
}

void test_cancel_drops_it_for_another_save() {
    Prompt prompt;
    g_prompt = &prompt;
    reset();
    zap::MissionPrompt m;
    zap::MissionAddresses a = addresses();
    m.request();
    m.cancel(a);  // the save changed before it was idle
    m.watch(a, true);
    check(g_fn == reinterpret_cast<void*>(&pause_routine), "asked, then cancelled: never set");

    m.request();
    m.watch(a, true);  // the opener set
    m.cancel(a);
    check(g_fn == reinterpret_cast<void*>(&pause_routine) && g_timer == 0 && !m.holds_hook(),
          "an opener not run: taken off");

    m.request();
    m.watch(a, true);
    m.open(a);
    m.yes();
    m.cancel(a);  // YES, then the save changed before the ending
    g_current = nullptr;
    g_fn = reinterpret_cast<void*>(&pause_routine);
    m.watch(a, true);
    check(g_fn == reinterpret_cast<void*>(&pause_routine), "no ending in another save");
}

}  // namespace

int main() {
    test_cancel_drops_it_for_another_save();
    test_offered_when_idle_then_yes_ends();
    test_another_transition_first_is_asked_again();
    test_unload_puts_back();
    test_an_unexpected_prompt_never_keeps_the_escape_skip();
    if (g_failures == 0) std::printf("mission tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
