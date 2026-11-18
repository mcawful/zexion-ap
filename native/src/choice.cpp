#include "choice.h"

#include <cmath>
#include <cstring>
#include <initializer_list>

#include "game_mem.h"
#include "text.h"  // Capitals
#ifdef _WIN32
#include "translation.h"  // rebuild_label, hd_case, read_small_entry
#endif

namespace zap {

namespace {

constexpr ptrdiff_t kPanelIndex = 3;  // the game-mode panel, in its menu's panel list
// The hover border's width and eight pieces, as the panel's handler lays them out (0xAE531 to
// 0xAE545; docs/re/memory.md, "The game-mode panel"). It ends on ARCHIPELAGO's last letter.
constexpr ptrdiff_t kPanelBorderPieces = 0x278, kPanelBorderWidth = 0x2B8;
constexpr int kBorderPieces = 8;
constexpr int kTop = 1, kTopRight = 2, kRight = 3, kBottomRight = 4, kBottom = 5;
constexpr ptrdiff_t kPieceLeft = 0x128, kPieceRight = 0x138;  // x: sprite::kX
constexpr ptrdiff_t kEdgeLength = 0x114, kEdgeLocalRight = 0x158;
constexpr ptrdiff_t kEdgeQuadRight1 = 0xA8, kEdgeQuadRight2 = 0xB8;
constexpr int32_t kGameBorderWidth = kLabelLength * 8 + 8;  // 96
constexpr double kEdgeAsLaid = 80.1;  // the top-right piece's x past the label's (read live)
constexpr double kBorderMove = 8.0;   // one character: as far past the label as on its left
// In capitals in its pixel glyphs; in a translation the panel shows SmallText 354's text
// (0x18027B), built again as this (hd_case).
constexpr char kLabel[] = "Archipelago";
constexpr Capitals kPixelLabel{kLabel};
static_assert(kPixelLabel.size() == kLabelLength);
constexpr char kRandomizer[kLabelLength + 1] = "RANDOMIZER ";  // the game's own label
[[maybe_unused]] constexpr int kRandomizerEntry = 354;  // Windows: put_back
constexpr ptrdiff_t kPanelSelected = 0x2DC;  // 32-bit: the selected child, -1 for none

}  // namespace

BYTE* ArchipelagoChoice::choice_in(BYTE* menu, const GameAddresses& a, void* expected_callback) {
    BYTE* panel = panel_at(menu, kPanelIndex);
    void* handler = pointer_at(panel, panel::kHandler);
    if (handler == nullptr ||
        (handler != a.panel_handler_fn && (a.hooks.panel == nullptr || handler != a.hooks.panel)) ||
        child_count(panel) != 4) {
        return nullptr;
    }
    BYTE* child = child_at(panel, 1);
    return live_widget(child, a.widget_handler_fn, expected_callback) ? child : nullptr;
}

bool ArchipelagoChoice::menu_open(const GameAddresses& a) const {
    return is_open(a.current_menu, menu_);
}

bool ArchipelagoChoice::put_back(const GameAddresses& a, WidgetCallback callback) {
    // Only into a widget whose menu is still open, still holding this helper's callback.
    if (!menu_open(a) || choice_in(menu_, a, reinterpret_cast<void*>(callback)) != child_) {
        return false;
    }
    BYTE* glyphs[kLabelLength] = {};
    if (glyphs_of(child_, glyphs)) {
        // Each frame shown derived afresh from the glyph's frame list now, not a pointer kept.
        for (int i = 0; i < kLabelLength; ++i) set_frame(glyphs[i], frames_[i]);
    }
    // The callback goes back even if the label changed shape: the widget is alive and would
    // otherwise keep pointing into the helper.
    at<void*>(child_, widget::kCallback) = a.rando_menu_fn;
#ifdef _WIN32
    // In a translation, its text back to the entry's (the label the game built from it).
    std::wstring own;
    if (read_small_entry(a, kRandomizerEntry, own)) {
        rebuild_label(a, pointer_at(child_, widget::kLabel), own, {.attached = true});
    }
#endif
    return true;
}

void ArchipelagoChoice::put_handler_back(const GameAddresses& a) {
    if (hook_panel_ == nullptr) return;
    if (!is_open(a.current_menu, hook_menu_)) {
        // Closed: its panels were freed with it (docs/re/memory.md, "The game-mode panel").
        hook_menu_ = hook_panel_ = nullptr;
        return;
    }
    // Only on the panel still in the menu's place: one the menu no longer lists is not known
    // to be alive, so it stays remembered (and the helper loaded) until the menu closes.
    void* handler = pointer_at(hook_panel_, panel::kHandler);
    if (panel_at(hook_menu_, kPanelIndex) != hook_panel_ || handler == nullptr) return;
    if (handler == a.hooks.panel &&
        !swap_pointer(hook_panel_, panel::kHandler, a.hooks.panel, a.panel_handler_fn)) {
        return;  // still the helper's: kept, so an unload refuses
    }
    hook_menu_ = hook_panel_ = nullptr;
}

bool ArchipelagoChoice::install(const GameAddresses& a, WidgetCallback callback) {
    BYTE* top = pointer_at(a.current_menu);
    if (child_ != nullptr && top == menu_ &&
        choice_in(top, a, reinterpret_cast<void*>(callback)) == child_) {
        return false;  // still installed on the menu that is up
    }
    restore(a, callback);
    if (top == nullptr || hook_panel_ != nullptr) return false;
#ifndef _WIN32
    // In a translation the label is rebuilt through the translation's code, not ported to the
    // Linux build yet (its 4-byte characters): RANDOMIZER stays.
    if (a.tr_flag == nullptr || *a.tr_flag != 0) return false;
#endif
    BYTE* child = choice_in(top, a, a.rando_menu_fn);
    BYTE* glyphs[kLabelLength] = {};
    if (child == nullptr || !glyphs_of(child, glyphs)) return false;
    // A label already reading ARCHIPELAGO (left by `release`) saves RANDOMIZER as its original.
    const bool already = glyphs_show(glyphs, kPixelLabel);
    for (int i = 0; i < kLabelLength; ++i) {
        BYTE* g = glyphs[i];
        frames_[i] = already ? static_cast<unsigned char>(kRandomizer[i])
                             : at<int32_t>(g, sprite::kFrameIndex);
        set_frame(g, static_cast<unsigned char>(kPixelLabel.text[i]));  // glyphs_of: count > 'Z'
    }
#ifdef _WIN32
    // In a translation, built again where the attach would build it now; the game sizes its
    // hover border from that text (0xAE500), and fit_border leaves it alone.
    rebuild_label(a, pointer_at(child, widget::kLabel), hd_case(a, kLabel), {.attached = true});
#endif
    at<void*>(child, widget::kCallback) = reinterpret_cast<void*>(callback);
    // The panel's handler the helper's, so the hover border is fitted in the frame it is laid
    // out (only over the game's own; checked by choice_in on the way).
    BYTE* panel = panel_at(top, kPanelIndex);
    if (a.hooks.panel != nullptr &&
        swap_pointer(panel, panel::kHandler, a.panel_handler_fn, a.hooks.panel)) {
        hook_menu_ = top;
        hook_panel_ = panel;
    }
    menu_ = top;
    child_ = child;
    callback_ = callback;
    return true;
}

bool ArchipelagoChoice::fit_border(const GameAddresses& a) {
    if (child_ == nullptr || !menu_open(a)) return false;
    BYTE* panel = panel_at(menu_, kPanelIndex);
    int32_t selected = -1, width = 0;
    BYTE* label = nullptr;
    double label_x = 0;
    // The choice still holding the helper's callback: called from the panel's hook inside the
    // game's frame, the menu may have been closed and built again since install looked.
    if (choice_in(menu_, a, reinterpret_cast<void*>(callback_)) != child_ ||
        !read(panel, kPanelSelected, selected) || selected != 1 ||
        !read(panel, kPanelBorderWidth, width) || width != kGameBorderWidth ||
        !read(child_, widget::kLabel, label) || !read(label, sprite::kX, label_x)) {
        return false;
    }
    BYTE* pieces[kBorderPieces] = {};
    if (!safe_read(panel + kPanelBorderPieces, pieces, sizeof(pieces))) return false;
    for (BYTE* piece : pieces) {
        BYTE probe[kEdgeLocalRight + 8];
        if (piece == nullptr || !safe_read(piece, probe, sizeof(probe)) ||
            !writable(piece, sizeof(probe))) {
            return false;
        }
    }
    // Only a border as the game laid it out around this label: its top-right piece where the
    // top edge ends, 80.1 past the label's x (once moved, it is 8 further and left alone).
    double right_x = 0;
    read(pieces[kTopRight], sprite::kX, right_x);
    if (std::fabs(right_x - (label_x + kEdgeAsLaid)) > 0.5) return false;
    for (int i : {kTopRight, kRight, kBottomRight}) {
        for (ptrdiff_t field : {sprite::kX, kPieceLeft, kPieceRight}) {
            at<double>(pieces[i], field) += kBorderMove;
        }
    }
    for (int i : {kTop, kBottom}) {
        at<float>(pieces[i], kEdgeLength) += static_cast<float>(kBorderMove);
        for (ptrdiff_t field : {kEdgeLocalRight, kPieceRight, kEdgeQuadRight1, kEdgeQuadRight2}) {
            at<double>(pieces[i], field) += kBorderMove;
        }
    }
    return true;
}

bool ArchipelagoChoice::release(const GameAddresses& a, WidgetCallback callback) {
    put_handler_back(a);
    if (child_ == nullptr) return false;
    bool put = menu_open(a) && choice_in(menu_, a, reinterpret_cast<void*>(callback)) == child_;
    if (put) at<void*>(child_, widget::kCallback) = a.rando_menu_fn;
    child_ = nullptr;
    menu_ = nullptr;
    return put;
}

bool ArchipelagoChoice::restore(const GameAddresses& a, WidgetCallback callback) {
    put_handler_back(a);
    if (child_ == nullptr) return false;
    bool put = put_back(a, callback);
    child_ = nullptr;
    menu_ = nullptr;
    return put;
}

void start_archipelago(NewGames& games, ArchipelagoChoice& choice, const GameAddresses& a,
                       const NewGameAddresses& newgame, WidgetCallback callback) {
    // Once per press; nothing after the plan was dropped or while a new game is queued.
    if (!games.armed() || !games.plan().choice || games.starting()) return;
    if (NewGames::queued(newgame)) return;
    games.begin_start();
    // Refused: RANDOMIZER back, the plan dropped, nothing queued.
    auto refuse = [&](const std::string& why) {
        choice.restore(a, callback);
        games.refuse(why);
    };
    try {
        std::string why = games.place_seed(newgame);
        if (!why.empty()) return refuse(why);
        // Queued as the play-anyway path does; watch reports it next frame.
        reinterpret_cast<void (*)()>(a.rando_newgame_fn)();
        if (!NewGames::queued(newgame)) {
            return refuse("the game did not queue a new game (is the slot empty?)");
        }
        // ARCHIPELAGO stays through the fade; only RANDOMIZER's callback goes back.
        choice.release(a, callback);
    } catch (...) {
        choice.restore(a, callback);
        games.disarm();
        throw;
    }
}

}  // namespace zap
