#include "feats.h"

#include <cstring>

#include "game_layout.h"
#include "game_mem.h"
#include "text.h"  // Capitals
#include "translation.h"

namespace zap {

namespace {

// The Feats of Natu screen's shape after an ending, MISSION COMPLETE's statistics under it
// (docs/re/memory.md, "Feats and achievements").
constexpr int32_t kScreenPanels = 4, kSecretPanels = 5, kSecretRow = 4, kSecretFeats = 5;
struct FeatRow {
    int32_t panel, fewest, most;
};
constexpr FeatRow kFeatRows[] = {{2, 6, 7}, {3, 5, 7}};
constexpr int32_t kTitleLines = 3, kMaxColumns = 64;
constexpr char kScreenName[] = "NATU";
constexpr int32_t kStatsLines = 10, kAssistLines = 8;
constexpr int32_t kFontFrames = 128;  // every character of the note has its frame
constexpr int32_t kTitleChildren = 3, kExitPanel = 1;  // panel 0: title, name, description

// The title's hint for a game that has never reached Ending B (description 219), named by its
// second line, and what the helper writes over it on two lines that clear the top row of Feats
// by kRoom (docs/re/memory.md, "Feats and achievements").
constexpr char kHintName[] = "EXPANDED ENDING";
constexpr char kScreenTitle[] = "FEATS OF NATU!";
constexpr double kRoom = 16;

// The frame at column `i` of a line `columns` wide holding `text` centered, or blank (0).
int32_t centered_frame(const char* text, int32_t i, int32_t columns) {
    const auto length = static_cast<int32_t>(std::strlen(text));
    const int32_t left = (columns - length + 1) / 2;
    return i >= left && i < left + length ? static_cast<uint8_t>(text[i - left]) : 0;
}

constexpr Capitals kPixelNote{kFeatsNote};
int32_t note_frame(int32_t i, int32_t columns) { return centered_frame(kPixelNote, i, columns); }

// Glyph `i` of the hint's stand-in: FEATS OF NATU! on line `first`, the note under it.
int32_t stand_in_frame(int32_t i, int32_t columns, int32_t first) {
    const int32_t line = i / columns, column = i % columns;
    if (line == first) return centered_frame(kScreenTitle, column, columns);
    if (line == first + 1) return note_frame(column, columns);
    return 0;
}

// The hint the helper wrote over (game thread only): its label and the frames to put back.
struct Replaced {
    const BYTE* label = nullptr;
    int32_t length = 0, first = 0;
    int32_t frames[kTitleLines * kMaxColumns] = {};
};
Replaced g_replaced;

// Whether every glyph can take any character: writable, with the font's frames and a frame list
// (what set_frame needs), so a write over all of them lands whole.
bool takes_text(BYTE* const* glyphs, int32_t count) {
    for (int32_t i = 0; i < count; ++i) {
        if (!writable(glyphs[i], sprite::kSize) ||
            frame_list(glyphs[i], kFontFrames - 1) == nullptr) {
            return false;
        }
    }
    return true;
}

// The hint at the title, when `show`: its frames kept to put back, then the stand-in written,
// the lower pair of lines first. Nothing written unless every glyph takes text and a pair fits.
void replace_hint(BYTE* top, const BYTE* label, BYTE* const* glyphs, const int32_t* frames,
                  int32_t columns, bool show) {
    const int32_t length = kTitleLines * columns;
    if (!show || !takes_text(glyphs, length)) return;
    int16_t row = 0;
    if (!read(panel_at(top, kFeatRows[0].panel), panel::kY, row)) return;
    int32_t first = -1;
    for (int32_t pair = kTitleLines - 2; pair >= 0 && first < 0; --pair) {
        double y = 0;
        if (read(glyphs[(pair + 1) * columns], sprite::kY, y) && row - y >= kRoom) first = pair;
    }
    if (first < 0) return;
    g_replaced.label = label;
    g_replaced.length = length;
    g_replaced.first = first;
    std::memcpy(g_replaced.frames, frames, sizeof(int32_t) * static_cast<size_t>(length));
    for (int32_t i = 0; i < length; ++i) set_frame(glyphs[i], stand_in_frame(i, columns, first));
}

// In a translation the title shows LargeText 220 (or 219, the hint) over the pixel glyphs.
// While the helper's text is there it is built again as 220's with the note under it; once it
// is not, the game's own entry again, if still the helper's. Without LargeText the pixel glyphs
// are shown instead (translation.h). Only on the label feats_screen_note has found live
// (native/README.md, "Feats and achievements in AP saves").
constexpr int kTitleEntry = 220, kHintEntry = 219;
constexpr Rebuild kTitleLayout = {.wrap = true, .center = true, .keep_middle = true};
BYTE* g_swapped = nullptr;  // the label whose pixel glyphs the helper shows
BuiltLabel g_hd;  // the label whose translated text is the helper's
int g_hd_entry = -1;  // the game's own entry for it, to put back
void show_ours(const GameAddresses& a, BYTE* label, bool ours, int game_entry) {
    std::wstring title;
    if (ours && read_large_entry(a, kTitleEntry, title)) {
        bool hd = g_hd.holds(label);
        if (!hd) {
            title.erase(0, title.find_first_not_of(L" \r\n"));
            title.erase(title.find_last_not_of(L" \r\n") + 1);
            g_hd.label = nullptr;
            // Each line centered on the longer with leading spaces, then the two as one block.
            FontMetrics metrics;
            const std::wstring text = centered_lines(title + L"\n" + hd_case(a, kFeatsNote),
                                                     metrics.load(a) ? &metrics : nullptr);
            hd = rebuild_label(a, label, text, kTitleLayout);
            if (hd) {
                g_hd.take(label);
                g_hd_entry = game_entry;
            }
        }
        if (hd) {  // the translated text shown, not the pixel glyphs (an earlier look's)
            if (pixel_glyphs_shown(label)) show_translated_glyphs(label);
            g_swapped = nullptr;
            return;
        }
    }
    if (g_hd.label == label) {
        std::wstring own;
        if (g_hd.holds(label) && read_large_entry(a, g_hd_entry, own)) {
            rebuild_label(a, label, own, kTitleLayout);
        }
        g_hd.label = nullptr;
    }
    if (ours) {
        if (translation_on(a) && !pixel_glyphs_shown(label) && show_pixel_glyphs(label)) {
            g_swapped = label;
        }
    } else if (g_swapped == label) {
        if (pixel_glyphs_shown(label)) show_translated_glyphs(label);
        g_swapped = nullptr;
    }
}

}  // namespace

bool feats_screen_note(const GameAddresses& a, bool show) {
    BYTE* top = pointer_at(a.current_menu);
    const int32_t panels = panel_count(top);
    if (panels != kScreenPanels && panels != kSecretPanels) return false;
    for (const FeatRow& row : kFeatRows) {
        const int32_t feats = child_count(panel_at(top, row.panel));
        if (feats < row.fewest || feats > row.most) return false;
    }
    if (panels == kSecretPanels && child_count(panel_at(top, kSecretRow)) != kSecretFeats) {
        return false;
    }
    // Only the screen after an ending: MISSION COMPLETE's statistics under it.
    BYTE* below = pointer_at(top, menu::kBelow);
    const int32_t stats = panel_count(below);
    if ((stats != 1 && stats != 2) || child_count(panel_at(below, 0)) != kStatsLines ||
        (stats == 2 && child_count(panel_at(below, 1)) != kAssistLines)) {
        return false;
    }
    if (child_count(panel_at(top, 0)) != kTitleChildren ||
        child_count(panel_at(top, kExitPanel)) != 1) {
        return false;
    }
    BYTE* title = child_at(panel_at(top, 0), 0);
    if (!live_widget(title, a.widget_handler_fn)) return false;
    BYTE* label = pointer_at(title, widget::kLabel);
    int32_t columns = 0;
    BYTE* glyphs[kTitleLines * kMaxColumns] = {};
    if (!read(label, label::kColumns, columns) || columns <= 0 || columns > kMaxColumns ||
        !label_glyphs(label, kTitleLines * columns, glyphs)) {
        return false;
    }
    const int32_t length = kTitleLines * columns;
    int32_t frames[kTitleLines * kMaxColumns] = {};
    std::string text;  // the label's characters, line by line, blank as a space
    for (int32_t i = 0; i < length; ++i) {
        if (glyphs[i] == nullptr) return false;
        frames[i] = frame_of(glyphs[i]);
        text += frames[i] > 0 && frames[i] < 128 ? static_cast<char>(frames[i]) : ' ';
    }
    const bool titled = text.substr(columns, columns).find(kScreenName) != std::string::npos;
    if (static_cast<int32_t>(sizeof(kFeatsNote) - 1) > columns) return titled;
    // A hint the helper replaced, still as it wrote it: kept while shown, else put back.
    if (g_replaced.label == label && g_replaced.length == length) {
        bool ours = true;
        for (int32_t i = 0; i < length && ours; ++i) {
            ours = frames[i] == stand_in_frame(i, columns, g_replaced.first);
        }
        if (ours) {
            if (!show && takes_text(glyphs, length)) {
                for (int32_t i = 0; i < length; ++i) set_frame(glyphs[i], g_replaced.frames[i]);
                g_replaced = {};
            }
            show_ours(a, label, g_replaced.label == label, kHintEntry);
            return true;
        }
        g_replaced = {};  // the game's own text again: nothing to put back
    }
    if (!titled) {
        // Not FEATS OF NATU! on the middle line: the hint, or not this screen.
        if (text.find(kHintName) == std::string::npos) return false;
        replace_hint(top, label, glyphs, frames, columns, show);
        show_ours(a, label, g_replaced.label == label, kHintEntry);
        return true;
    }
    // The last line: blank (frame 0), or the note centered with blank glyphs around it.
    BYTE** last = glyphs + 2 * columns;
    bool blank = true, noted = true;
    for (int32_t i = 0; i < columns; ++i) {
        const int32_t f = frames[2 * columns + i];
        blank = blank && f == 0;
        noted = noted && f == note_frame(i, columns);
    }
    // The game's own text there, or glyphs the whole note cannot be written into: left alone.
    if ((!blank && !noted) || show == noted || !takes_text(last, columns)) {
        show_ours(a, label, noted, kTitleEntry);
        return true;
    }
    for (int32_t i = 0; i < columns; ++i) set_frame(last[i], show ? note_frame(i, columns) : 0);
    show_ours(a, label, show, kTitleEntry);
    return true;
}

void FeatsGuard::set_image(HMODULE image, void** context_init_by_name) {
    image_ = image;
    by_name_ = context_init_by_name;
    steam_checked_ = false;
    steam_wait_ = 0;
    steam_error_.clear();
}

void FeatsGuard::watch(const GameAddresses& a, bool on, const std::set<int>& loading) {
    on_ = on;
    if (on) {
        patch_ending(a);
    } else {
        restore_ending();
        ending_error_.clear();
    }
    // Hooked while on, or while the load test can block (a slot holds an AP save).
    const bool test = !loading.empty();
    if (!on && !test) {
        release_steam();
        steam_slot_error_.clear();
        return;
    }
    const std::set<int> none;
    const std::set<int>& slots = test ? loading : none;
    if (slot_ != nullptr) {
        set_switch(a, on, slots);
    } else {
        hook_steam(a, on, slots);
    }
}

void FeatsGuard::restore() {
    on_ = false;
    restore_ending();
    release_steam();
}

std::string FeatsGuard::state() const {
    if (!on_) return "off";
    std::string why = ending_error_;
    for (const std::string* e : {&steam_error_, &steam_slot_error_}) {
        if (!e->empty()) why += (why.empty() ? "" : "; ") + *e;
    }
    return why.empty() ? "on" : "on:" + why;
}


static_assert(kEndingPatchSize <= kMaxCodePatch, "put_back_code reads it whole");

void FeatsGuard::restore_ending() {
    if (ending_patched_ && put_back_code(site_, written_, original_, sizeof(original_))) {
        ending_patched_ = false;
        site_ = nullptr;
    }
}


}  // namespace zap
