#include "mission.h"

#include <cstring>

#include "menu.h"  // safe_read, writable

namespace zap {

// Centered on the escape skip's line, which has room for 30 characters (0x166D9F); the owner's
// wording, after the disks' description (equip.cpp).
const char kMissionQuestion[] = " RETURN HOME WITH THE DISKS?";

namespace {

constexpr ptrdiff_t kMenuBelow = 0x78;
constexpr int kMaxMenus = 64;
constexpr ptrdiff_t kPanelChildren = 0x1E0, kPanelCapacity = 0x1E8;
constexpr ptrdiff_t kChildLabel = 0x1F0, kChildCallback = 0x2C0, kChildHandler = 0x2C8;
constexpr ptrdiff_t kReleased = 0x1C8, kLabelCapacity = 0x1D8;
// The prompt as 0x1668A0 builds it: one panel of three children, YES, NO and the question.
constexpr int32_t kPromptChildren = 3;
constexpr int kMinQuestionGlyphs = 28;  // kMissionQuestion's length

template <typename T>
bool read(const BYTE* base, ptrdiff_t offset, T& out) {
    return base != nullptr && safe_read(base + offset, &out, sizeof(T));
}

template <typename T>
void put(BYTE* base, ptrdiff_t offset, T value) {
    std::memcpy(base + offset, &value, sizeof(value));
}

bool is_open(BYTE* current_menu, const BYTE* wanted) {
    BYTE* menu = nullptr;
    if (!read(current_menu, 0, menu)) return false;
    for (int i = 0; i < kMaxMenus && menu != nullptr; ++i) {
        if (menu == wanted) return true;
        if (!read(menu, kMenuBelow, menu)) return false;
    }
    return false;
}

void* routine(const MissionAddresses& a, int index) {
    void* fn = nullptr;
    read(a.routine_table, index * 8, fn);
    return fn;
}

bool writable_fields(const MissionAddresses& a) {
    return writable(a.transition_fn, sizeof(void*)) &&
           writable(a.transition_timer, sizeof(int32_t));
}

}  // namespace

void MissionPrompt::request() {
    if (yes_child_ != nullptr || armed_ || ending_) return;  // already on its way
    requested_ = true;
    status_ = "asked";
}

void MissionPrompt::watch(const MissionAddresses& a, bool idle_in_play) {
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
    if (armed_ && fn != a.open_hook) {
        // Not ours any more and not run (open() clears armed_): another transition took the
        // place (a door, a death). Asked again once idle.
        armed_ = false;
        requested_ = true;
        status_ = "asked again: another transition came first";
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
    if (requested_ && a.open_hook != nullptr && routine(a, kPromptRoutine) != nullptr) {
        put(a.transition_fn, 0, a.open_hook);
        put<int32_t>(a.transition_timer, 0, 1);
        requested_ = false;
        armed_ = true;
        status_ = "opening";
    }
}

std::string MissionPrompt::open(const MissionAddresses& a) {
    armed_ = false;
    auto builder = reinterpret_cast<void (*)()>(routine(a, kPromptRoutine));
    if (builder == nullptr) return status_ = "no prompt builder";
    builder();
    // The prompt the game just built: the top menu, its panel's three children.
    BYTE* menu = nullptr;
    BYTE** panels = nullptr;
    BYTE* panel = nullptr;
    BYTE** children = nullptr;
    int32_t capacity = 0;
    BYTE* child[kPromptChildren] = {};
    if (!read(a.current_menu, 0, menu) || !read(menu, 0, panels) ||
        !read(reinterpret_cast<BYTE*>(panels), 0, panel) ||
        !read(panel, kPanelCapacity, capacity) || capacity != kPromptChildren ||
        !read(panel, kPanelChildren, children) ||
        !safe_read(children, child, sizeof(child))) {
        return status_ = "not the prompt as traced";
    }
    // First of all, YES does what NO does: whatever else is not as traced, the escape skip's
    // YES never stays on a prompt outside the escape.
    void* first_no = nullptr;
    if (read(child[1], kChildCallback, first_no) && first_no != nullptr &&
        writable(child[0] + kChildCallback, sizeof(void*))) {
        put(child[0], kChildCallback, first_no);
    }
    void* callbacks[kPromptChildren] = {};
    callbacks[0] = first_no;  // YES, now NO's
    for (int i = 1; i < kPromptChildren; ++i) {
        int32_t released = 1;
        void* handler = nullptr;
        if (!read(child[i], kReleased, released) || released != 0 ||
            !read(child[i], kChildHandler, handler) || handler != a.widget_handler ||
            !read(child[i], kChildCallback, callbacks[i])) {
            return status_ = "not the prompt as traced";
        }
    }
    int32_t yes_released = 1;
    void* yes_handler = nullptr;
    if (!read(child[0], kReleased, yes_released) || yes_released != 0 ||
        !read(child[0], kChildHandler, yes_handler) || yes_handler != a.widget_handler) {
        return status_ = "not the prompt as traced";
    }
    BYTE* question = nullptr;
    int32_t glyphs = 0;
    void* no = callbacks[1];
    if (no == nullptr || callbacks[0] != no || callbacks[2] != nullptr) {
        return status_ = "not the prompt as traced";
    }
    if (!read(child[2], kChildLabel, question) || !read(question, kLabelCapacity, glyphs) ||
        glyphs < kMinQuestionGlyphs) {
        return status_ = "the question's label is not as traced: YES is NO";
    }
    reinterpret_cast<void (*)(void*, const char*, int, int)>(a.text_set_fn)(
        question, kMissionQuestion, 0, glyphs);
    if (a.yes_hook != nullptr) put(child[0], kChildCallback, a.yes_hook);
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

void MissionPrompt::cancel(const MissionAddresses& a) {
    requested_ = false;
    ending_ = false;
    void* fn = nullptr;
    if (armed_ && read(a.transition_fn, 0, fn)) {
        if (fn != a.open_hook) {
            armed_ = false;  // not ours any more
        } else if (writable_fields(a)) {
            put(a.transition_fn, 0, routine(a, kIdleRoutine));
            put<int32_t>(a.transition_timer, 0, 0);
            armed_ = false;
        }
    }
    if (!armed_ && yes_child_ == nullptr) status_ = "not asked";
}

void MissionPrompt::restore(const MissionAddresses& a) {
    cancel(a);  // an opener it could not take off stays armed_: still held
    void* cb = nullptr;
    if (yes_child_ != nullptr && is_open(a.current_menu, menu_) &&
        read(yes_child_, kChildCallback, cb) && cb == a.yes_hook) {
        put(yes_child_, kChildCallback, no_fn_);
    }
    yes_child_ = nullptr;
    menu_ = nullptr;
}

}  // namespace zap
