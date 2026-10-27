#include "feats.h"

#include <cstring>

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
// Data: the original function, the stats context, the stand-in's switch, the holder the stub
// returns (pointing at the object), the object (pointing at the vtable), and the vtable.
constexpr ptrdiff_t kOriginal = 0x00, kContext = 0x08, kBlocked = 0x10, kHolder = 0x18,
                    kObject = 0x20, kVtable = 0x28;
constexpr int kVtableEntries = 64;  // ISteamUserStats v013 has 51; the game calls 6, 7 and 9
// Code: the stub at 0, the stand-in's every method at kReturnZero.
constexpr ptrdiff_t kReturnZero = 0x20;

int32_t rel32(const BYTE* from_next, const BYTE* to) {
    return static_cast<int32_t>(to - from_next);
}

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

// The Feats of Natu screen (feats.h): its panels, the Feat rows' size, the title label's lines,
// what its middle line names, and the menu under it, MISSION COMPLETE's statistics.
constexpr int32_t kScreenPanels = 4, kFeatRowPanels[] = {2, 3}, kFeatsPerRow = 7;
constexpr int32_t kTitleLines = 3, kMaxColumns = 64;
constexpr char kScreenName[] = "NATU";
constexpr int32_t kStatsPanels = 1, kStatsLines = 10;
constexpr int32_t kFontFrames = 128;  // every character of the note has its frame

// The panel count of `menu`, and the child count of its panel `i`, or -1.
int32_t panel_count(const BYTE* menu) {
    int32_t n = -1;
    return read(menu, menu::kPanelCount, n) ? n : -1;
}
int32_t child_count(const BYTE* menu, int i) {
    int32_t n = -1;
    return read(panel_at(menu, i), panel::kCapacity, n) ? n : -1;
}

// The note's frame at column `i` of a line `columns` wide: its character, or blank (0).
int32_t note_frame(int32_t i, int32_t columns) {
    const auto length = static_cast<int32_t>(sizeof(kFeatsNote) - 1);
    const int32_t left = (columns - length + 1) / 2;
    return i >= left && i < left + length ? static_cast<uint8_t>(kFeatsNote[i - left]) : 0;
}

}  // namespace

bool feats_screen_note(const GameAddresses& a, bool show) {
    BYTE* top = pointer_at(a.current_menu);
    if (panel_count(top) != kScreenPanels) return false;
    for (int32_t i : kFeatRowPanels) {
        if (child_count(top, i) != kFeatsPerRow) return false;
    }
    // Only the screen after an ending: MISSION COMPLETE's statistics under it.
    BYTE* below = pointer_at(top, menu::kBelow);
    if (panel_count(below) != kStatsPanels || child_count(below, 0) != kStatsLines) return false;
    BYTE* title = child_at(panel_at(top, 0), 0);
    if (!live_widget(title, a.widget_handler_fn)) return false;
    BYTE* label = pointer_at(title, widget::kLabel);
    int32_t length = 0, columns = 0;
    BYTE* list = nullptr;
    BYTE* glyphs[kTitleLines * kMaxColumns] = {};
    if (!read(label, label::kLength, length) || !read(label, label::kColumns, columns) ||
        columns <= 0 || columns > kMaxColumns || length != kTitleLines * columns ||
        !read(label, label::kGlyphs, list) ||
        !safe_read(list, glyphs, static_cast<size_t>(length) * sizeof(BYTE*))) {
        return false;
    }
    std::string middle;
    for (int32_t i = 0; i < length; ++i) {
        if (glyphs[i] == nullptr) return false;
        if (i >= columns && i < 2 * columns) {
            const int32_t f = frame_of(glyphs[i]);
            middle += f > 0 && f < 128 ? static_cast<char>(f) : ' ';
        }
    }
    if (middle.find(kScreenName) == std::string::npos) return false;
    // The last line: blank (frame 0), or the note centered with blank glyphs around it.
    if (static_cast<int32_t>(sizeof(kFeatsNote) - 1) > columns) return true;
    BYTE** last = glyphs + 2 * columns;
    bool blank = true, noted = true, fits = true;
    for (int32_t i = 0; i < columns; ++i) {
        const int32_t f = frame_of(last[i]);
        int32_t frames = 0;
        blank = blank && f == 0;
        noted = noted && f == note_frame(i, columns);
        fits = fits && writable(last[i], sprite::kSize) &&
               read(last[i], sprite::kFrameCount, frames) && frames >= kFontFrames;
    }
    // The game's own text there, or glyphs the whole note cannot be written into: left alone.
    if ((!blank && !noted) || !fits || show == noted) return true;
    for (int32_t i = 0; i < columns; ++i) set_frame(last[i], show ? note_frame(i, columns) : 0);
    return true;
}

void FeatsGuard::set_image(HMODULE image, void** context_init_by_name) {
    image_ = image;
    by_name_ = context_init_by_name;
    steam_checked_ = false;
    steam_error_.clear();
}

void FeatsGuard::watch(const GameAddresses& a, bool on) {
    on_ = on;
    if (on) {
        patch_ending(a);
        hook_steam(a);
    } else {
        restore_ending();
        unhook_steam();
        ending_error_.clear();
        steam_slot_error_.clear();
    }
}

void FeatsGuard::restore() {
    on_ = false;
    restore_ending();
    unhook_steam();
}

void FeatsGuard::release_steam() { unhook_steam(); }

std::string FeatsGuard::state() const {
    if (!on_) return "off";
    std::string why = ending_error_;
    for (const std::string* e : {&steam_error_, &steam_slot_error_}) {
        if (!e->empty()) why += (why.empty() ? "" : "; ") + *e;
    }
    return why.empty() ? "on" : "on:" + why;
}

void* FeatsGuard::stand_in() const { return data_ == nullptr ? nullptr : data_ + kHolder; }

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
    put(patch, 2, rel32(site + kStoreSize, flag251));
    patch[6] = 0;
    patch[7] = kJmp;
    put(patch, 8, rel32(site + kEndingPatchSize, join));
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
    // cmp dword [rip+blocked], 0   83 3D rel32 00   0..6
    // je pass                      74 11            7..8
    // cmp rcx, [rip+context]       48 3B 0D rel32   9..15
    // jne pass                     75 08            16..17
    // lea rax, [rip+holder]        48 8D 05 rel32   18..24
    // ret                          C3               25
    // pass: jmp [rip+original]     FF 25 rel32      26..31
    // return_zero: xor eax, eax    31 C0            32..33
    // ret                          C3               34
    // No stack is touched, so it needs no unwind information: a stack walk takes it as a leaf.
    const BYTE code[] = {0x83, 0x3D, 0, 0, 0, 0, 0x00, 0x74, 0x11, 0x48, 0x3B, 0x0D,
                         0,    0,    0, 0, 0x75, 0x08, 0x48, 0x8D, 0x05, 0,    0,    0,
                         0,    0xC3, 0xFF, 0x25, 0, 0, 0, 0, 0x31, 0xC0, 0xC3};
    std::memcpy(page, code, sizeof(code));
    put(page, 2, rel32(page + 7, data + kBlocked));
    put(page, 12, rel32(page + 16, data + kContext));
    put(page, 21, rel32(page + 25, data + kHolder));
    put(page, 28, rel32(page + 32, data + kOriginal));
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

void FeatsGuard::hook_steam(const GameAddresses& a) {
    if (slot_ != nullptr) return;
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
    InterlockedExchange(&at<LONG>(data_, kBlocked), 1);
    if (!swap_slot(slot, real_, code_)) {
        InterlockedExchange(&at<LONG>(data_, kBlocked), 0);
        steam_slot_error_ = "the slot could not be written";
        return;
    }
    slot_ = slot;
    steam_slot_error_.clear();
}

void FeatsGuard::unhook_steam() {
    void** slot = slot_;
    if (slot == nullptr) return;
    // Off first: a call already past the slot passes through.
    InterlockedExchange(&at<LONG>(data_, kBlocked), 0);
    // Back only if it still holds the stub; if something hooked over it since, the stub stays in
    // its chain, passing everything through.
    swap_slot(slot, code_, at<void*>(data_, kOriginal));
    slot_ = nullptr;
}

}  // namespace zap
