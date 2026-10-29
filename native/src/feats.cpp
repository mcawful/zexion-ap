#include "feats.h"

#include <cstring>

#include "game_layout.h"
#include "game_mem.h"
#include "iat.h"

namespace zap {

namespace {

// The ending's call and the code around the join, as traced on B1 (docs/re/memory.md, "Feats and
// achievements"): `call [rip+slot]` then `mov rdx, [rip+...]` at the call; `mov byte [rip+flag
// 251], 1` just before the join, `movsxd rdx, [rip+...]` at it and `mov byte [rip+flag 250], 1`
// 0x11 bytes after it.
constexpr ptrdiff_t kMaxStretch = 0x1000;  // the join lies after the call, within the routine
constexpr BYTE kCallIndirect[] = {0xFF, 0x15};  // call [rip+rel32]: 6 bytes
constexpr ptrdiff_t kCallSize = 6;
constexpr BYTE kMovRdx[] = {0x48, 0x8B, 0x15};
constexpr BYTE kMovsxdRdx[] = {0x48, 0x63, 0x15};
constexpr BYTE kStoreByte[] = {0xC6, 0x05};  // mov byte [rip+rel32], imm8: 7 bytes
constexpr size_t kStoreSize = 7;
constexpr ptrdiff_t kFlag250Store = 0x11;  // from the join
constexpr BYTE kJmp = 0xE9;

// The stats context's init function loads this interface's name within its first bytes.
constexpr char kStatsInterface[] = "STEAMUSERSTATS_INTERFACE_VERSION";
constexpr size_t kInitScan = 32;
constexpr BYTE kLeaRdx[] = {0x48, 0x8D, 0x15};  // lea rdx, [rip+rel32]: 7 bytes
constexpr size_t kLeaSize = 7;

// The stub's two pages (feats.h): code, then data, adjacent.
constexpr size_t kPage = 0x1000;
// Data: the original function, the stats context, the stand-in's switch (blocking always), the
// holder the stub returns (pointing at the object), the object (pointing at the vtable), the
// vtable; then the load test: where the live marker word is (null: no test), where current_slot
// is, where save_loaded is, and a byte per slot, set for an AP save's.
constexpr ptrdiff_t kOriginal = 0x00, kContext = 0x08, kBlocked = 0x10, kHolder = 0x18,
                    kObject = 0x20, kVtable = 0x28;
constexpr int kVtableEntries = 64;  // ISteamUserStats v013 has 51; the game calls 6, 7 and 9
constexpr ptrdiff_t kMarker = kVtable + kVtableEntries * 8, kSlot = kMarker + 8,
                    kSaved = kSlot + 8, kApSlots = kSaved + 8;
static_assert(kApSlots + slot::kCount <= static_cast<ptrdiff_t>(kPage), "in the data page");
// Code: the stub at 0, the stand-in's every method at kReturnZero.
constexpr ptrdiff_t kReturnZero = 90;

bool fits_rel32(const BYTE* from_next, const BYTE* to) {
    ptrdiff_t d = to - from_next;
    return d >= INT32_MIN && d <= INT32_MAX;
}

// Where `mov byte [rip+rel32], value` at `where` (7 bytes, read into `bytes`) stores, if it is
// one storing `value`; null otherwise.
const BYTE* store_target(const BYTE* where, const BYTE* bytes, BYTE value) {
    if (std::memcmp(bytes, kStoreByte, sizeof(kStoreByte)) != 0 || bytes[6] != value) {
        return nullptr;
    }
    return where + kStoreSize + field<int32_t>(bytes, 2);
}

// The Feats of Natu screen (feats.h): its panels (a fifth, the secret row, when the builder adds
// it), the Feat rows' sizes, the title label's lines, what its middle line names, and the menu
// under it, MISSION COMPLETE's statistics: 10 lines, and, seen with the assist menu unlocked,
// ASSISTS USED's 8 (the game's condition not traced).
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

// The title's hint for a game that has never reached Ending B (the Text file's description 219,
// COMPLETE ANY ONE OF THESE / TO SEE THE EXPANDED ENDING!), named by its second line; and what
// the helper writes over it: FEATS OF NATU! and the note on two lines that clear the top row of
// Feats (a line's glyphs span its y to 7 below it, the row's icons start 6 above the row's y:
// measured on the owner's screenshots, docs/re/memory.md).
constexpr char kHintName[] = "EXPANDED ENDING";
constexpr char kScreenTitle[] = "FEATS OF NATU!";
constexpr double kRoom = 16;

// The frame at column `i` of a line `columns` wide holding `text` centered: its character, or
// blank (0).
int32_t centered_frame(const char* text, int32_t i, int32_t columns) {
    const auto length = static_cast<int32_t>(std::strlen(text));
    const int32_t left = (columns - length + 1) / 2;
    return i >= left && i < left + length ? static_cast<uint8_t>(text[i - left]) : 0;
}

int32_t note_frame(int32_t i, int32_t columns) { return centered_frame(kFeatsNote, i, columns); }

// Glyph `i` of the hint's stand-in: FEATS OF NATU! on line `first`, the note under it, the
// other line blank.
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
        int32_t frames = 0;
        BYTE* list = nullptr;
        if (!writable(glyphs[i], sprite::kSize) || !read(glyphs[i], sprite::kFrameCount, frames) ||
            frames < kFontFrames || !read(glyphs[i], sprite::kFrameList, list) ||
            list == nullptr) {
            return false;
        }
    }
    return true;
}

// The hint at the title (feats.h), when `show`: its frames kept to put back, then FEATS OF NATU!
// and the note written over it, on the lower two lines if the note clears the top row of Feats
// there, else the upper two. Nothing written unless every glyph takes text and one pair has room.
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
            return true;
        }
        g_replaced = {};  // the game's own text again: nothing to put back
    }
    if (!titled) {
        // Not FEATS OF NATU! on the middle line: the hint, or not this screen.
        if (text.find(kHintName) == std::string::npos) return false;
        replace_hint(top, label, glyphs, frames, columns, show);
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
    if ((!blank && !noted) || show == noted || !takes_text(last, columns)) return true;
    for (int32_t i = 0; i < columns; ++i) set_frame(last[i], show ? note_frame(i, columns) : 0);
    return true;
}

void FeatsGuard::set_image(HMODULE image, void** context_init_by_name) {
    image_ = image;
    by_name_ = context_init_by_name;
    steam_checked_ = false;
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
    const bool test = !loading.empty() && a.rando_marker != nullptr &&
                      a.current_slot != nullptr && a.save_loaded != nullptr;
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

void* FeatsGuard::stand_in() const { return data_ == nullptr ? nullptr : data_ + kHolder; }

bool FeatsGuard::load_test() const {
    return data_ != nullptr && at<BYTE*>(data_, kMarker) != nullptr;
}

void FeatsGuard::patch_ending(const GameAddresses& a) {
    BYTE* site = a.ending_unlocks;
    BYTE* join = a.ending_join;
    BYTE now[kEndingPatchSize];
    if (site == nullptr || join == nullptr || !safe_read(site, now, sizeof(now))) {
        ending_error_ = "the ending cannot be read";
        return;
    }
    if (ending_patched_ && site == site_ && std::memcmp(now, written_, sizeof(now)) == 0) return;
    // The call as traced: through the configured import slot, then the mov after it.
    if (std::memcmp(now, kCallIndirect, sizeof(kCallIndirect)) != 0 ||
        site + kCallSize + field<int32_t>(now, 2) != a.context_init_slot ||
        std::memcmp(now + kCallSize, kMovRdx, sizeof(kMovRdx)) != 0) {
        ending_error_ = "the ending's call reads otherwise";
        return;
    }
    // The join after it, between the flag stores as traced.
    BYTE around[kStoreSize + kFlag250Store + kStoreSize];
    if (join <= site || join - site > kMaxStretch ||
        !safe_read(join - kStoreSize, around, sizeof(around))) {
        ending_error_ = "the ending's join is not after its call";
        return;
    }
    BYTE* flag250 = a.flags + flag::kEnding;
    BYTE* flag251 = a.flags + flag::kFeat;
    const BYTE* at_join = around + kStoreSize;
    if (store_target(join - kStoreSize, around, 1) != flag251 ||
        std::memcmp(at_join, kMovsxdRdx, sizeof(kMovsxdRdx)) != 0 ||
        store_target(join + kFlag250Store, at_join + kFlag250Store, 1) != flag250) {
        ending_error_ = "the ending's join reads otherwise";
        return;
    }
    // mov byte [rip+flag 251], 0; jmp join.
    BYTE patch[kEndingPatchSize];
    if (!fits_rel32(site + kStoreSize, flag251) || !fits_rel32(site + kEndingPatchSize, join)) {
        ending_error_ = "the ending is out of reach";
        return;
    }
    std::memcpy(patch, kStoreByte, sizeof(kStoreByte));
    put_rel32(patch, 2, site + kStoreSize, flag251);
    patch[6] = 0;
    patch[7] = kJmp;
    put_rel32(patch, 8, site + kEndingPatchSize, join);
    if (!patch_code(site, patch, sizeof(patch))) {
        ending_error_ = "the ending could not be written";
        return;
    }
    std::memcpy(original_, now, sizeof(now));
    std::memcpy(written_, patch, sizeof(patch));
    site_ = site;
    ending_patched_ = true;
    ending_error_.clear();
}

bool FeatsGuard::reads_as_written() const {
    BYTE now[kEndingPatchSize];
    return safe_read(site_, now, sizeof(now)) && std::memcmp(now, written_, sizeof(now)) == 0;
}

void FeatsGuard::restore_ending() {
    if (!ending_patched_) return;
    if (reads_as_written()) patch_code(site_, original_, sizeof(original_));
    // Still held while it reads as written (put back failed): the helper then refuses to unload.
    if (!reads_as_written()) {
        ending_patched_ = false;
        site_ = nullptr;
    }
}

std::string FeatsGuard::check_steam(const GameAddresses& a) {
    auto** slot = reinterpret_cast<void**>(a.context_init_slot);
    if (slot == nullptr || slot != by_name_) {
        return "the slot is not steam_api64's SteamInternal_ContextInit";
    }
    HMODULE steam = GetModuleHandleW(L"steam_api64.dll");
    real_ = steam == nullptr ? nullptr
                             : reinterpret_cast<void*>(
                                   GetProcAddress(steam, "SteamInternal_ContextInit"));
    if (real_ == nullptr) return "steam_api64 is not loaded";
    // The context's first quadword is its init function, in the game's code, which asks for the
    // user-stats interface by name.
    BYTE* init = pointer_at(a.stats_context);
    if (init == nullptr || !in_image_code(image_, init)) return "the context has no init";
    BYTE code[kInitScan];
    if (!safe_read(init, code, sizeof(code))) return "the context's init cannot be read";
    for (size_t i = 0; i + kLeaSize <= sizeof(code); ++i) {
        if (std::memcmp(code + i, kLeaRdx, sizeof(kLeaRdx)) != 0) continue;
        const auto rel = field<int32_t>(code, static_cast<ptrdiff_t>(i + sizeof(kLeaRdx)));
        const BYTE* name = init + i + kLeaSize + rel;
        char text[sizeof(kStatsInterface) - 1];
        if (safe_read(name, text, sizeof(text)) &&
            std::memcmp(text, kStatsInterface, sizeof(text)) == 0) {
            return "";
        }
    }
    return "the context is not the user stats'";
}

bool FeatsGuard::make_pages() {
    if (code_ != nullptr) return true;
    auto* page = static_cast<BYTE*>(
        VirtualAlloc(nullptr, 2 * kPage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (page == nullptr) return false;
    BYTE* data = page + kPage;
    // cmp rcx, [rip+context]          48 3B 0D rel32     0..6
    // jne pass                         75 4B              7..8
    // cmp dword [rip+blocked], 0       83 3D rel32 00     9..15
    // jne block                        75 3A              16..17
    // mov rax, [rip+marker]            48 8B 05 rel32     18..24   the load test (feats.h)
    // test rax, rax                    48 85 C0           25..27
    // jz pass                          74 36              28..29
    // cmp word [rax], 1                66 83 38 01        30..33
    // jne pass                         75 30              34..35
    // mov rax, [rip+saved]             48 8B 05 rel32     36..42
    // cmp byte [rax], kNoSaveLoaded    80 38 FF           43..45   (the title keeps the marker)
    // je pass                          74 24              46..47
    // mov rax, [rip+slot]              48 8B 05 rel32     48..54
    // mov eax, [rax]                   8B 00              55..56
    // cmp eax, slot::kCount            83 F8 1E           57..59
    // jae pass                         73 16              60..61   (a negative one too)
    // lea r10, [rip+ap_slots]          4C 8D 15 rel32     62..68
    // cmp byte [r10+rax], 0            41 80 3C 02 00     69..73
    // je pass                          74 08              74..75
    // block: lea rax, [rip+holder]     48 8D 05 rel32     76..82
    // ret                              C3                 83
    // pass: jmp [rip+original]         FF 25 rel32        84..89
    // return_zero: xor eax, eax        31 C0              90..91
    // ret                              C3                 92
    // No stack is touched, so it needs no unwind information: a stack walk takes it as a leaf.
    // Only rax and r10 are changed before the pass, both volatile and neither an argument.
    static_assert(slot::kCount == 0x1E && kNoSaveLoaded == 0xFF, "the cmps' imm8s");
    const BYTE code[] = {
        0x48, 0x3B, 0x0D, 0,    0,    0,    0,    0x75, 0x4B, 0x83, 0x3D, 0,    0,    0,
        0,    0x00, 0x75, 0x3A, 0x48, 0x8B, 0x05, 0,    0,    0,    0,    0x48, 0x85, 0xC0,
        0x74, 0x36, 0x66, 0x83, 0x38, 0x01, 0x75, 0x30, 0x48, 0x8B, 0x05, 0,    0,    0,
        0,    0x80, 0x38, 0xFF, 0x74, 0x24, 0x48, 0x8B, 0x05, 0,    0,    0,    0,    0x8B,
        0x00, 0x83, 0xF8, 0x1E, 0x73, 0x16, 0x4C, 0x8D, 0x15, 0,    0,    0,    0,    0x41,
        0x80, 0x3C, 0x02, 0x00, 0x74, 0x08, 0x48, 0x8D, 0x05, 0,    0,    0,    0,    0xC3,
        0xFF, 0x25, 0,    0,    0,    0,    0x31, 0xC0, 0xC3};
    static_assert(sizeof(code) == kReturnZero + 3, "as laid out");
    std::memcpy(page, code, sizeof(code));
    put_rel32(page, 3, page + 7, data + kContext);
    put_rel32(page, 11, page + 16, data + kBlocked);
    put_rel32(page, 21, page + 25, data + kMarker);
    put_rel32(page, 39, page + 43, data + kSaved);
    put_rel32(page, 51, page + 55, data + kSlot);
    put_rel32(page, 65, page + 69, data + kApSlots);
    put_rel32(page, 79, page + 83, data + kHolder);
    put_rel32(page, 86, page + 90, data + kOriginal);
    at<BYTE*>(data, kHolder) = data + kObject;
    at<BYTE*>(data, kObject) = data + kVtable;
    for (int i = 0; i < kVtableEntries; ++i) {
        at<BYTE*>(data, kVtable + static_cast<ptrdiff_t>(i) * 8) = page + kReturnZero;
    }
    DWORD old = 0;
    if (!VirtualProtect(page, kPage, PAGE_EXECUTE_READ, &old)) {
        VirtualFree(page, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), page, kPage);
    code_ = page;
    data_ = data;
    return true;
}

void FeatsGuard::hook_steam(const GameAddresses& a, bool on, const std::set<int>& loading) {
    if (!steam_checked_) {
        steam_error_ = check_steam(a);
        steam_checked_ = true;
    }
    if (!steam_error_.empty()) return;
    auto** slot = reinterpret_cast<void**>(a.context_init_slot);
    void* current = nullptr;
    // Only over steam_api64's own function, so nobody else's hook is lost (looked at again each
    // frame: something else may let go of it).
    if (!safe_read(slot, &current, sizeof(current)) || current != real_) {
        steam_slot_error_ = "the slot does not hold steam_api64's function";
        return;
    }
    if (!make_pages()) {
        steam_slot_error_ = "no memory for the stand-in";
        return;
    }
    at<void*>(data_, kOriginal) = real_;
    at<void*>(data_, kContext) = a.stats_context;
    set_switch(a, on, loading);
    if (!swap_slot(slot, real_, code_)) {
        set_switch(a, false, {});
        steam_slot_error_ = "the slot could not be written";
        return;
    }
    slot_ = slot;
    steam_slot_error_.clear();
}

void FeatsGuard::set_switch(const GameAddresses& a, bool on, const std::set<int>& loading) {
    InterlockedExchange(&at<LONG>(data_, kBlocked), on ? 1 : 0);
    auto* marker = reinterpret_cast<void* volatile*>(data_ + kMarker);
    BYTE* table = data_ + kApSlots;
    if (loading.empty()) {
        // The test off before its table is cleared.
        InterlockedExchangePointer(marker, nullptr);
        std::memset(table, 0, slot::kCount);
        return;
    }
    // The table and current_slot's and save_loaded's places before the marker's, which turns
    // the test on.
    at<BYTE*>(data_, kSlot) = a.current_slot;
    at<BYTE*>(data_, kSaved) = a.save_loaded;
    for (int n = 1; n <= slot::kCount; ++n) table[n - 1] = loading.count(n) != 0 ? 1 : 0;
    InterlockedExchangePointer(marker, a.rando_marker);
}

void FeatsGuard::release_steam() {
    void** slot = slot_;
    if (slot == nullptr) return;
    // Off first (the load test too): a call already past the slot passes through.
    set_switch({}, false, {});
    // Back only if it still holds the stub; if something hooked over it since, the stub stays in
    // its chain, passing everything through.
    swap_slot(slot, code_, at<void*>(data_, kOriginal));
    slot_ = nullptr;
}

}  // namespace zap
