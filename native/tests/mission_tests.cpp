// Host tests for RETURN HOME WITH THE DISKS? (mission.h) on stand-ins of the transition, the
// prompt builder (0x1668A0), event 3667 (the fade) and its handler, and the fade's stepper.

#include <windows.h>

#include <cmath>
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
void ending_routine();
void door_routine() { g_calls += 4; }
void escape_yes(void*, int) { g_calls += 5; }
void game_no(void*, int) { g_calls += 6; }
void open_hook() { g_calls += 7; }
void yes_hook(void*, int) { g_calls += 8; }
void end_hook() { g_calls += 9; }  // the ending's wrapper: frame() calls end in its place
void after_ending() { g_calls += 10; }  // 0x948A0
void* const kIdle = reinterpret_cast<void*>(&pause_routine);  // the routine idle in play
void* const kEnding = reinterpret_cast<void*>(&ending_routine);

std::map<BYTE*, std::string> g_set;
void text_set(void* text, const char* str, int, int) { g_set[static_cast<BYTE*>(text)] = str; }

// The game's state the prompt touches.
void* g_fn = nullptr;
int32_t g_timer = 0;
BYTE* g_current = nullptr;
void* g_table[16] = {};

// The ending routine (0x94A30) as B1 runs it with no menu open: the level set to 4 (0x95230),
// and 0x948A0 set as the transition with timer 1 (0x9511B, 0x952FE).
int g_endings = 0;
void ending_routine() {
    g_calls += 3;
    ++g_endings;
    g_fade_level = 4;
    g_fn = reinterpret_cast<void*>(&after_ending);
    g_timer = 1;
}

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
    g_endings = 0;
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
    a.hooks.mission_end = reinterpret_cast<void*>(&end_hook);
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

// One frame of the game's main loop: the transition run when its timer reaches 0 (0x7ACA1 to
// 0x7AE43), the stepper's pass (0x7BA35); returns the level the frame is drawn at.
int8_t frame(zap::MissionPrompt& m, const zap::GameAddresses& a) {
    if (g_timer > 0 && --g_timer == 0 && g_fn != nullptr) {
        if (g_fn == a.hooks.mission_end) {
            m.end(a);
        } else if (g_fn == reinterpret_cast<void*>(&after_ending)) {
            g_fn = nullptr;
        } else {
            reinterpret_cast<void (*)()>(g_fn)();
        }
    }
    run_fade(1);
    return g_fade_level;
}

// The ending started: the frames that run the helper's wrapper (and the routine) and 0x948A0.
void run_ending(zap::MissionPrompt& m, const zap::GameAddresses& a) {
    frame(m, a);
    frame(m, a);
}

// The fade the ending's wrapper starts: from black toward 4, a step every kEndingFadeStep frames.
bool fading_in_from_black() {
    return at<int8_t>(g_fade, zap::fade_state::kTarget) == 4 &&
           g_fade[zap::fade_state::kStep] == zap::kEndingFadeStep;
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

    check(m.open(a).empty(), m.status().c_str());
    check(!m.opening() && m.holds_hook(), "opened: popups go on, YES still held");
    check(g_set[prompt.question] == " RETURN HOME WITH THE DISKS?", "the question relabeled");
    check(at<void*>(prompt.child[0], 0x2C0) == a.hooks.mission_yes, "YES the helper's");
    check(at<void*>(prompt.child[1], 0x2C0) == reinterpret_cast<void*>(&game_no),
          "NO the game's");

    check(m.yes() == reinterpret_cast<void*>(&game_no), "YES calls the game's NO");
    m.watch(a, true);
    check(g_fn == nullptr, "not while the prompt is still up");
    g_current = nullptr;  // closed: the game's close sets the pause routine back
    g_fn = kIdle;
    m.watch(a, true);
    check(g_fn == a.hooks.mission_end && g_timer == 1,
          "the ending (the helper's wrapper), as the ship's event starts the routine");
    check(m.holds_hook() && m.status() == "ending started", "the wrapper held until it runs");
    frame(m, a);
    check(g_endings == 1 && !m.holds_hook(), "run: nothing held");
    check(g_fades.size() == 1, "no fade back: NO's own close fades in");
}

// In a translation the question is built again where the game built SmallText 374's text, and
// centered as the game centers it.
void test_the_question_in_a_translation() {
    reset_translation();
    load_translation();
    Prompt prompt;
    zap::MissionPrompt m = fading(addresses());
    zap::GameAddresses a = addresses();
    with_translation(a);
    // As the game builds it: at the first pixel glyph less 1 (0x8AC30), then centered on x 156
    // (0x85B00), its glyphs moved and not the place it was given.
    HdGlyphs<90> hd(prompt.question);
    BYTE pixel[0x1D8] = {};
    BYTE* pixels[1] = {pixel};
    put<BYTE**>(prompt.question, 0x228, pixels);
    put<double>(pixel, 0x40, 40.0);
    put<double>(pixel, 0x48, 97.0);
    built(prompt.question, 39.0, 96.0);
    check(m.open(a).empty() && g_builds.size() == 2 &&
              g_builds[0].text == L"Return home with the disks?" &&
              g_builds[0].label == prompt.question && g_builds[0].x == 39.0f &&
              g_builds[0].y == 96.0f && g_builds[1].text == g_builds[0].text &&
              g_builds[1].y == 96.0f && g_builds[0].flag == 0 && g_builds[1].flag == 0,
          "the question built again in the translation's font, then again centered");
    const int32_t n = get<int32_t>(prompt.question, 0x248);
    const double left = hd.x(0), right = hd.x(n - 1) + kHdWidth;
    check(n == 23 && (left + right) / 2 == 156.0 && hd.y(0) == 95.0,
          "centered as the game centers its own, on its line");
    m.restore(a);
    g_builds.clear();
    put<int32_t>(g_translation, 0xA8, 1);
    zap::MissionPrompt pixel_font = fading(addresses());
    check(pixel_font.open(a).empty() && g_builds.size() == 2 &&
              g_builds[1].x == static_cast<float>(std::round(g_builds[1].x)),
          "a pixel font's question moved a whole number of pixels");
    pixel_font.restore(a);
    put<int32_t>(g_translation, 0xA8, 0);
    g_builds.clear();
    g_translation[0x91] = 0;
    zap::MissionPrompt plain = fading(addresses());
    check(plain.open(a).empty() && g_builds.empty(), "the default English: nothing built");
    plain.restore(a);
    reset_translation();
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

    // Not on its way once shown, nor once NO has closed it (YES still wrapped until the next
    // watch): a refused unload in that frame does not ask for it again (Game::put_back_game).
    reset();
    m.request();
    m.watch(a, true);
    m.open(a);
    check(!m.on_its_way(), "a prompt up: not on its way");
    g_current = nullptr;  // NO: the prompt closed
    check(m.holds_hook() && !m.on_its_way(), "closed by NO, before the next watch: nor then");
    m.watch(a, true);
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

// Event 3667 not as traced, or its handler not the game's code: the prompt at once, no fade.
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


// A save during the fade sets the pause opener back with no fade (0x1FC8F1): the fade turned
// back, mid-way or done, and asked again once idle.
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

// Another transition's fade is left alone, even one with 3667's own values (0x1D93DE); in the
// first frames the timer it sets (70) tells it apart.
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

// A cancel after a save took the opener's place, before the helper saw it: the fade turned back.
void test_cancel_after_a_save_turns_the_fade_back() {
    Prompt prompt;
    zap::GameAddresses a = addresses();
    zap::MissionPrompt m = fading(a);
    run_fade(25);
    m.watch(a, true);
    run_fade(1);
    g_fn = kIdle;  // saved
    m.cancel(a);
    check(!m.holds_hook() && !m.on_its_way() && m.status() == "not asked", m.status().c_str());
    check(g_fades.size() == 2 && g_fades[1] == fade_back(), "faded back");
    g_timer = 0;
    m.watch(a, true);
    check(g_fn == kIdle && g_fades.size() == 2, "not asked again");
}

// The fade as NO's close (0x92A60) leaves it: what the builder saved, level -1 and the fade
// toward 4 a step every 6 frames, the countdown 6.
void close_with_no() {
    g_current = nullptr;
    g_fn = kIdle;  // the close sets the pause opener back, timer 0
    g_timer = 0;
    set_fade(4, -1, 6);
}

bool black_and_off() {
    return g_fade_level == -1 && at<int8_t>(g_fade, zap::fade_state::kTarget) == -1 &&
           g_fade[zap::fade_state::kStep] == 0 && g_fade[zap::fade_state::kCountdown] == 0;
}

bool fading_back_in() {
    return at<int8_t>(g_fade, zap::fade_state::kTarget) == 4 &&
           g_fade[zap::fade_state::kStep] == 6 && g_fade[zap::fade_state::kCountdown] == 6;
}


// A prompt opened and YES pressed, NO's close run: what the helper's YES then sees.
zap::MissionPrompt yes_pressed(const zap::GameAddresses& a) {
    zap::MissionPrompt m = fading(a);
    run_fade(40);
    m.open(a);
    m.yes();
    close_with_no();
    return m;
}

// After YES, NO's close leaves the screen black, the fade off: the ending routine sets the level.
void test_yes_keeps_the_screen_black() {
    {
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = yes_pressed(a);
        size_t fades = g_fades.size();
        m.after_no(a);
        check(m.dark() && black_and_off() && g_fades.size() == fades,
              "black, the fade off, nothing run through type 1");
        check(m.status() == "yes: ending when idle, the screen kept black", m.status().c_str());
        run_fade(30);
        check(g_fade_level == -1, "no fade back in");
        m.watch(a, false);  // not idle yet (the game's last message, say)
        check(m.dark() && g_fn == kIdle, "still black, the ending waiting");
        m.watch(a, true);
        check(g_fn == a.hooks.mission_end && g_timer == 1 && m.dark() && m.holds_hook() &&
                  m.status() == zap::kEndingStarted && black_and_off(),
              "then the ending, through the helper's wrapper, the screen still black for it");
        run_ending(m, a);
        m.watch(a, false);
        check(g_endings == 1 && !m.dark() && !m.holds_hook() && g_fn == nullptr &&
                  m.status() == zap::kEndingStarted,
              "run, nothing held");
    }
    // Not as NO leaves it: left to fade back in.
    struct Case {
        const char* what;
        void (*change)();
    };
    const Case cases[] = {
        {"another level", [] { g_fade_level = 2; }},
        {"another target", [] { at<int8_t>(g_fade, zap::fade_state::kTarget) = 9; }},
        {"another step", [] { set_fade(4, -1, 5); }},
        {"a step under way", [] { g_fade[zap::fade_state::kCountdown] = 3; }},
    };
    for (const Case& c : cases) {
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = yes_pressed(a);
        c.change();
        std::vector<BYTE> before(g_fade, g_fade + sizeof(g_fade));
        int8_t level = g_fade_level;
        m.after_no(a);
        check(!m.dark() && std::memcmp(before.data(), g_fade, sizeof(g_fade)) == 0 &&
                  g_fade_level == level,
              c.what);
        check(m.status() ==
                  "yes: ending when idle (the fade not as NO leaves it, so it fades back in)",
              m.status().c_str());
        m.watch(a, true);
        check(g_fn == a.hooks.mission_end, "the ending as before");
        run_fade(30);  // faded back in, as NO's close left it
        check(frame(m, a) == 4 && g_endings == 1 && !fading_in_from_black(),
              "not black when it runs: its level 4 left as it is");
    }
    {
        Prompt prompt;  // no YES: nothing
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = prompt_in_image();
        close_with_no();
        m.after_no(a);
        check(!m.dark() && fading_back_in(), "not after YES: left alone");
    }
}

// The ending sets the level to 4 itself (0x95230), drawn before the helper's frame call; through
// the helper's wrapper its frames are drawn black, then fade in.
void test_the_ending_comes_out_of_the_black() {
    Prompt prompt;
    zap::GameAddresses a = addresses();
    zap::MissionPrompt m = yes_pressed(a);
    m.after_no(a);
    size_t fades = g_fades.size();
    bool black = true;
    for (int i = 0; i < 5; ++i) {
        black = black && frame(m, a) == -1;
        m.watch(a, false);  // the game's last message still up
    }
    check(black, "black while the ending waits");
    m.watch(a, true);
    std::vector<int8_t> drawn;
    for (int i = 0; i < 60; ++i) {
        drawn.push_back(frame(m, a));
        m.watch(a, false);
    }
    check(g_endings == 1 && !m.dark() && !m.holds_hook() && m.status() == zap::kEndingStarted,
          "the ending run once, nothing held");
    std::vector<BYTE> in(event_3667(), event_3667() + 0x28);
    const int16_t words[] = {4, -1, zap::kEndingFadeStep, 0, 1};
    std::memcpy(in.data() + 4, words, sizeof(words));
    check(g_fades.size() == fades + 1 && g_fades.back() == in,
          "event 3667 aimed from black to 4, a step every kEndingFadeStep frames, no side fields");
    check(drawn[0] == -1, "the routine's frame drawn black");
    // The stepper's first pass comes in the routine's own frame.
    const int first_lit = zap::kEndingFadeStep - 1, lit = first_lit + 4 * zap::kEndingFadeStep;
    bool rising = true;
    for (int i = 0; i < static_cast<int>(drawn.size()); ++i) {
        int want = i < first_lit ? -1 : (i - first_lit) / zap::kEndingFadeStep;
        if (i >= lit) want = 4;
        rising = rising && drawn[i] == want;
    }
    check(rising, "black for its first frames, then a level each kEndingFadeStep frames to 4");
}

// The ending's wrapper is never left as the transition: on unload, with no routine or one not as
// traced, or called again.
void test_the_endings_wrapper_is_never_left_behind() {
    {
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = yes_pressed(a);
        m.watch(a, true);  // NO's fade back left as it is: no black
        check(g_fn == a.hooks.mission_end && m.holds_hook(), "set: held");
        m.restore(a);
        check(g_fn == kEnding && g_timer == 1 && !m.holds_hook(),
              "unload: the game's routine in its place, nothing held");
        m.watch(a, false);
        check(g_fn == kEnding && m.started() && !m.ending(), "not asked again: it runs");
    }
    {
        // A door in the wrapper's place, then an unload in the same frame.
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = yes_pressed(a);
        m.watch(a, true);
        g_fn = reinterpret_cast<void*>(&door_routine);
        g_timer = 70;
        m.restore(a);
        check(!m.holds_hook() && g_fn == reinterpret_cast<void*>(&door_routine), "left alone");
        g_fn = kIdle;
        g_timer = 0;
        m.watch(a, true);
        check(g_fn == kIdle && !m.ending(), "and no ending asked again");
    }
    {
        // Called again (a pause press with it at timer 0), or after the save was left.
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = yes_pressed(a);
        m.watch(a, true);
        frame(m, a);
        g_fn = a.hooks.mission_end;
        g_timer = 0;
        m.end(a);
        check(g_endings == 1 && g_fn == kIdle && !m.holds_hook(), "called again: taken off");
        zap::MissionPrompt n = yes_pressed(a);
        n.watch(a, true);
        n.left_save();
        frame(n, a);
        check(g_endings == 1 && g_fn == kIdle && !n.holds_hook(),
              "after the save was left: taken off, no ending");
    }
    {
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = yes_pressed(a);
        m.after_no(a);
        m.watch(a, true);
        g_table[zap::kEndingRoutine] = reinterpret_cast<void*>(&door_routine);  // sets nothing
        check(frame(m, a) == -1 && g_fn == kIdle && !m.holds_hook(),
              "the wrapper left as the transition: the idle routine");
    }
    {
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = yes_pressed(a);
        m.after_no(a);
        m.watch(a, true);
        g_table[zap::kEndingRoutine] = nullptr;
        frame(m, a);
        check(g_fn == kIdle && !m.holds_hook() && !m.started() && m.ending() && m.dark() &&
                  m.status() == "yes: ending when idle (no ending routine)",
              m.status().c_str());
        g_table[zap::kEndingRoutine] = kEnding;
        m.watch(a, true);
        check(g_fn == a.hooks.mission_end && m.started(), "asked again once idle");
        frame(m, a);
        check(g_endings == 1 && g_fade_level == -1 && fading_in_from_black(), "and run from black");
    }
}

// The black's limit is in game frames, not frame calls, which come faster at a high refresh rate
// (docs/re/memory.md, "The per-frame call: the game's SDL_GL_SwapWindow import").
void test_the_black_lasts_in_game_frames() {
    Prompt prompt;
    zap::GameAddresses a = addresses();
    int64_t clock = 5000;
    a.frame_clock = reinterpret_cast<BYTE*>(&clock);
    zap::MissionPrompt m = yes_pressed(a);
    m.after_no(a);
    check(m.black() == "kept black", m.black().c_str());
    // 4 game frames to 11 calls (60 to 166 a second): GAME SAVED's 90 frames take about 250.
    int calls = 0;
    for (int frames = 0; frames < 90; ++calls) {
        if (calls % 11 < 4) {
            ++clock;
            ++frames;
        }
        m.watch(a, false);  // the message still up: not idle
    }
    check(calls > 180 && m.dark() && black_and_off(), "still black when GAME SAVED ends");
    m.watch(a, true);
    check(g_fn == a.hooks.mission_end, "then the ending");
    run_ending(m, a);
    check(g_endings == 1 && m.black() == "ended: the ending, faded in from black",
          m.black().c_str());

    zap::MissionPrompt n = yes_pressed(a);
    n.after_no(a);
    for (int i = 0; i < zap::kDarkFrames * 3; ++i) n.watch(a, false);  // the clock still
    check(n.dark(), "no game frames: still black");
    for (int i = 0; i < zap::kDarkFrames; ++i) {
        ++clock;
        n.watch(a, false);
    }
    check(n.dark(), "kDarkFrames game frames: still black");
    ++clock;
    n.watch(a, false);
    check(!n.dark() && fading_back_in() &&
              n.black() == "ended: kDarkFrames in play with no ending, faded back in",
          n.black().c_str());
    zap::MissionPrompt j = yes_pressed(a);  // a clock that jumps (a reload)
    j.after_no(a);
    j.watch(a, false);
    clock += 100000;
    j.watch(a, false);
    check(j.dark(), "a jump: still black");
    j.end_dark(a, "ended: by hand");
    check(j.black() == "ended: by hand", j.black().c_str());
}

// No ending for kDarkFrames frames in play: faded back in. Menu frames do not count: the pause
// saves the fade and puts it back.
void test_the_black_after_yes_ends() {
    {
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = yes_pressed(a);
        m.after_no(a);
        BYTE pause[8] = {};
        g_current = pause;
        for (int i = 0; i < zap::kDarkFrames * 2; ++i) m.watch(a, false);
        check(m.dark(), "a menu open: not counted");
        g_current = nullptr;
        for (int i = 0; i < zap::kDarkFrames; ++i) m.watch(a, false);
        check(m.dark() && black_and_off(), "kDarkFrames frames: still black");
        m.watch(a, false);
        check(!m.dark() && fading_back_in() && g_fade_level == -1, "then back in, as NO's close");
        check(m.status() == "yes: ending when idle, faded back in after the black",
              m.status().c_str());
        run_fade(30);
        check(g_fade_level == 4, "at the normal level");
        m.watch(a, true);
        check(g_fn == a.hooks.mission_end, "the ending still comes");
    }
    {
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = yes_pressed(a);
        m.after_no(a);
        set_fade(4, static_cast<int8_t>(0xFE), 12);  // a fade of the game's own
        m.watch(a, false);
        check(!m.dark() && g_fade[zap::fade_state::kStep] == 12, "another fade: left alone");
        for (int i = 0; i < zap::kDarkFrames + 1; ++i) m.watch(a, false);
        check(g_fade[zap::fade_state::kStep] == 12, "and not turned back later");
    }
    {
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = yes_pressed(a);
        m.after_no(a);
        m.end_dark(a, "ended: by hand");
        check(!m.dark() && fading_back_in(), "end_dark: back in");
        m.end_dark(a, "ended: by hand");
        check(fading_back_in(), "once");
    }
    {
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = yes_pressed(a);
        m.after_no(a);
        m.cancel(a);
        check(!m.dark() && !m.ending() && fading_back_in(), "cancel: no ending, back in");
        m.watch(a, true);
        check(g_fn == kIdle, "and no ending");
    }
    {
        // Canceled with the pause open: its close puts the black back, so the fade back waits.
        Prompt prompt;
        zap::GameAddresses a = addresses();
        zap::MissionPrompt m = yes_pressed(a);
        m.after_no(a);
        BYTE pause[8] = {};
        g_current = pause;
        m.cancel(a);
        check(m.dark() && black_and_off(), "a menu open: not yet");
        m.watch(a, false);
        check(m.dark() && black_and_off(), "nor while it stays open");
        g_current = nullptr;  // closed: the black it saved put back
        m.watch(a, false);
        check(!m.dark() && fading_back_in(), "then back in");
        m.watch(a, true);
        check(g_fn == kIdle, "and no ending");
    }
}

// A save in the ending's wrapper's place before it ran: the ending asked again, still black.
void test_an_ending_replaced_before_it_ran_is_asked_again() {
    Prompt prompt;
    zap::GameAddresses a = addresses();
    zap::MissionPrompt m = yes_pressed(a);
    m.after_no(a);
    m.watch(a, true);
    check(m.started() && g_fn == a.hooks.mission_end, "set");
    g_fn = kIdle;
    g_timer = 0;
    m.watch(a, false);
    check(!m.started() && m.ending() && m.dark() && black_and_off() && !m.holds_hook() &&
              m.status() == "yes: ending when idle (another transition came first)",
          m.status().c_str());
    m.watch(a, true);
    check(g_fn == a.hooks.mission_end && m.started(), "set again");
    run_ending(m, a);
    m.watch(a, false);
    check(m.started() && !m.dark() && g_endings == 1, "run");

    // Once run, 0x948A0's close sets the pause opener back with timer 0, as a save would.
    zap::MissionPrompt n = yes_pressed(a);
    n.after_no(a);
    n.watch(a, true);
    frame(n, a);
    g_fn = kIdle;
    g_timer = 0;
    n.watch(a, false);
    check(n.started() && !n.ending() && !n.dark() && n.status() == zap::kEndingStarted,
          "the pause opener after the ending ran: not taken for a save");

    // A door's or a death's transition first: the black kept only while it is the helper's.
    struct Case {
        const char* what;
        void* fn;
        int32_t timer;
        void (*fade)();
    };
    const Case cases[] = {
        {"a door, the fade left alone", reinterpret_cast<void*>(&door_routine), 70, [] {}},
        {"a door with its own fade", reinterpret_cast<void*>(&door_routine), 70,
         [] { set_fade(-1, 4, 8); }},
        {"a death's, timer 30", reinterpret_cast<void*>(&door_routine), 30,
         [] { set_fade(-1, static_cast<int8_t>(0xFE), 5); }},
    };
    for (const Case& c : cases) {
        zap::MissionPrompt d = yes_pressed(a);
        d.after_no(a);
        d.watch(a, true);
        g_fn = c.fn;
        g_timer = c.timer;
        c.fade();
        bool ours = black_and_off();
        d.watch(a, false);
        check(!d.started() && d.ending() && d.dark() == ours &&
                  d.status() == "yes: ending when idle (another transition came first)",
              c.what);
        g_fn = kIdle;
        g_timer = 0;
        d.watch(a, true);
        check(g_fn == a.hooks.mission_end && d.started(),
              (std::string(c.what) + ": the ending set again once idle").c_str());
        d.left_save();
    }
}

// Quitting to the title while black drops the black (the title owns the fade), so nothing holds
// an unload there.
void test_leaving_the_save_drops_the_black() {
    Prompt prompt;
    zap::GameAddresses a = addresses();
    zap::MissionPrompt m = yes_pressed(a);
    m.after_no(a);
    BYTE pause[8] = {};
    g_current = pause;
    m.cancel(a);
    check(m.dark(), "a menu open: the fade back waits");
    m.left_save();
    check(!m.dark() && black_and_off(), "the save left: dropped, the fade not touched");
}

// Once the ending is started, not offered again in that load (its cutscenes read as play, its
// own save as one at the pod); the status says so until another save is loaded.
void test_not_offered_again_after_the_ending() {
    Prompt prompt;
    zap::GameAddresses a = addresses();
    zap::MissionPrompt m = yes_pressed(a);
    m.after_no(a);
    m.watch(a, true);
    check(m.status() == zap::kEndingStarted, m.status().c_str());
    run_ending(m, a);
    m.watch(a, false);
    g_fn = kIdle;  // idle-looking later in its cutscenes
    g_timer = 0;
    m.request();
    m.watch(a, true);
    check(g_fn == kIdle && !m.on_its_way() && m.status() == zap::kEndingStarted,
          "asked during the ending: nothing");
    m.cancel(a);
    check(m.status() == zap::kEndingStarted, "cancel (discs off, the client gone): still said");
    m.left_save();
    check(m.status() == "not asked", "another save loaded");
    m.request();
    m.watch(a, true);
    check(g_fn == a.hooks.mission_open, "offered again in a later load");
    m.cancel(a);
}

}  // namespace

int main() {
    test_yes_keeps_the_screen_black();
    test_the_ending_comes_out_of_the_black();
    test_the_endings_wrapper_is_never_left_behind();
    test_the_black_lasts_in_game_frames();
    test_the_black_after_yes_ends();
    test_not_offered_again_after_the_ending();
    test_an_ending_replaced_before_it_ran_is_asked_again();
    test_leaving_the_save_drops_the_black();
    test_the_question_in_a_translation();
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
