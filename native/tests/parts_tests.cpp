// Host tests for the helper's parts that do not need the game: the import-table hook and the
// trampoline (on this test program's own import of GetTickCount), game_mem.h's shared readers,
// memory check, frames and code patches, the build check and the pipe.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "check.h"
#include "build.h"
#include "game_mem.h"
#include "iat.h"
#include "pipe.h"
#include "pipe_client.h"

namespace {

DWORD WINAPI fake_tick_count() { return 42; }

LONG g_seen_inside = -1;
volatile LONG* g_in_flight = nullptr;

DWORD WINAPI counting_tick_count() {
    g_seen_inside = *g_in_flight;
    return 7;
}

DWORD WINAPI raising_tick_count() {
    RaiseException(0xE0000001, 0, 0, nullptr);
    return 0;
}

// Calls GetTickCount through the import table and reports whether an exception raised behind
// it was caught here, which needs the unwinder to get through the trampoline.
bool caught_through_import() {
    __try {
        GetTickCount();
    } __except (GetExceptionCode() == 0xE0000001 ? EXCEPTION_EXECUTE_HANDLER
                                                 : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}

void* real_tick_count() {
    return reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount"));
}

void test_import_slot_hook() {
    HMODULE self = GetModuleHandleW(nullptr);
    void** slot = zap::find_import_slot(self, "kernel32.dll", "GetTickCount");
    check(slot != nullptr, "finds this program's GetTickCount import (any case)");
    if (slot == nullptr) return;
    void* real = real_tick_count();
    check(*slot == real, "the slot holds kernel32's GetTickCount");
    check(zap::swap_slot(slot, real, reinterpret_cast<void*>(&fake_tick_count)), "swaps the slot");
    check(GetTickCount() == 42, "calls through the import table reach the replacement");
    check(!zap::swap_slot(slot, real, real), "refuses a swap when the slot holds something else");
    check(zap::swap_slot(slot, reinterpret_cast<void*>(&fake_tick_count), real), "swaps it back");
    check(*slot == real, "the real function is back in the slot");
    check(zap::find_import_slot(self, "kernel32.dll", "NoSuchFunction") == nullptr,
          "a function that is not imported has no slot");
    check(zap::find_import_slot(self, "SDL2.dll", "SDL_GL_SwapWindow") == nullptr,
          "a DLL that is not imported has no slot");
}

void test_trampoline() {
    zap::Trampoline t;
    check(zap::make_trampoline(reinterpret_cast<void*>(&counting_tick_count), t), "builds");
    g_in_flight = t.in_flight;
    void** slot = zap::find_import_slot(GetModuleHandleW(nullptr), "kernel32.dll", "GetTickCount");
    void* real = real_tick_count();
    check(zap::swap_slot(slot, real, t.entry), "points the slot at the trampoline");
    check(GetTickCount() == 7, "the trampoline forwards the call and its result");
    check(g_seen_inside == 1, "a call is counted while it is inside");
    check(*t.in_flight == 0, "and uncounted once it returns");
    *t.target = real;
    check(GetTickCount() != 7, "a changed target takes effect at once");
    check(zap::swap_slot(slot, t.entry, real), "puts the slot back");
    DWORD64 image = 0;
    check(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(t.entry) + 12, &image, nullptr) !=
                  nullptr &&
              image == reinterpret_cast<DWORD64>(t.entry),
          "the trampoline has unwind information");
    zap::Trampoline raising;
    check(zap::make_trampoline(reinterpret_cast<void*>(&raising_tick_count), raising), "builds");
    check(zap::swap_slot(slot, real, raising.entry), "points the slot at a raising trampoline");
    check(caught_through_import(), "an exception behind the trampoline reaches the caller");
    check(zap::swap_slot(slot, raising.entry, real), "puts the slot back again");
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(t.entry, &info, sizeof(info));
    check(info.Protect == PAGE_EXECUTE_READ, "the code page is not writable");
}

// game_mem.h's readers of menus, panels and labels, and its swap of a handler slot: each reads
// fault-safely and refuses what it cannot read (or write) whole.
void test_game_mem_helpers() {
    // Three pages: read-write, read-only (filled first) and not readable.
    auto* pages = static_cast<BYTE*>(
        VirtualAlloc(nullptr, 0x3000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    BYTE* read_only = pages + 0x1000;
    BYTE* none = pages + 0x2000;
    BYTE a = 0, b = 0, c = 0;
    put<void*>(read_only, 0, &a);
    DWORD old = 0;
    VirtualProtect(read_only, 0x1000, PAGE_READONLY, &old);
    VirtualProtect(none, 0x1000, PAGE_NOACCESS, &old);

    // A menu of one panel (its list at +0, the count at +8), the panel's three children (the
    // list at +0x1E0, the count at +0x1E8).
    BYTE panel[0x200] = {};
    BYTE* children[3] = {&a, &b, &c};
    put(panel, 0x1E0, children);
    put<int32_t>(panel, 0x1E8, 3);
    BYTE menu[0x10] = {};
    BYTE* panels[1] = {panel};
    put(menu, 0x00, panels);
    put<int32_t>(menu, 0x08, 1);
    check(zap::panel_count(menu) == 1 && zap::child_count(panel) == 3, "the counts");
    check(zap::panel_count(nullptr) == -1 && zap::child_count(none) == -1, "unreadable: -1");
    BYTE* out[4] = {};
    check(zap::children_of(panel, out, 4) == 3 && out[2] == &c && out[3] == nullptr,
          "the children");
    BYTE* two[2] = {};
    check(zap::children_of(panel, two, 2) == 3 && two[0] == &a && two[1] == &b,
          "more than room for: the first copied, the whole count told");
    BYTE* untouched[3] = {};
    put(panel, 0x1E0, none - 8);  // the first child readable, the rest not
    check(zap::children_of(panel, untouched, 3) == -1 && untouched[0] == nullptr,
          "a list that cannot be read whole: -1, nothing copied");

    // A label of two glyphs (the count at +0x1D8, the list at +0x228), each glyph's frame (its
    // character) at +0x1C4.
    BYTE h[0x1C8] = {}, i[0x1C8] = {};
    put<int32_t>(h, 0x1C4, 'H');
    put<int32_t>(i, 0x1C4, 'I');
    BYTE* list[2] = {h, i};
    BYTE label[0x230] = {};
    put<int32_t>(label, 0x1D8, 2);
    put(label, 0x228, list);
    BYTE* glyphs[2] = {};
    check(zap::label_glyphs(label, 2, glyphs) && glyphs[0] == h && glyphs[1] == i,
          "a label's glyphs");
    check(!zap::label_glyphs(label, 3, glyphs) && !zap::label_glyphs(label, 0, glyphs) &&
              !zap::label_glyphs(nullptr, 2, glyphs),
          "another length, none, or no label: refused");
    check(zap::glyphs_show(glyphs, "HI") && zap::glyphs_show(glyphs, "H") &&
              !zap::glyphs_show(glyphs, "HO"),
          "the glyphs read as their text");
    BYTE* unreadable[1] = {none};
    check(!zap::glyphs_show(unreadable, "H"), "an unreadable glyph shows nothing");

    // A handler slot at +8.
    BYTE widget[0x10] = {};
    put<void*>(widget, 8, &a);
    check(!zap::swap_pointer(widget, 8, &b, &c) && get<void*>(widget, 8) == &a,
          "a slot holding another handler is left alone");
    check(!zap::swap_pointer(widget, 0, nullptr, &c) && get<void*>(widget, 0) == nullptr,
          "a null `from` never matches an empty slot");
    check(zap::swap_pointer(widget, 8, &a, &b) && get<void*>(widget, 8) == &b, "swapped");
    check(!zap::swap_pointer(read_only, 0, &a, &b) && get<void*>(read_only, 0) == &a,
          "a slot that cannot be written: refused, left as it was");
    check(!zap::swap_pointer(none, 0, &a, &b) && !zap::swap_pointer(nullptr, 0, &a, &b),
          "an unreadable slot: refused");
    VirtualFree(pages, 0, MEM_RELEASE);
}

// The memory check under every read and write of the game's memory: each page on the way,
// across regions; a guard page (another thread's stack) neither read nor written, and so still
// armed.
void test_memory_checks() {
    // Pages: read-write, read-only, guard, no access, and one reserved but not committed.
    auto* pages = static_cast<BYTE*>(VirtualAlloc(nullptr, 0x5000, MEM_RESERVE, PAGE_READWRITE));
    VirtualAlloc(pages, 0x4000, MEM_COMMIT, PAGE_READWRITE);
    BYTE *read_only = pages + 0x1000, *guard = pages + 0x2000, *none = pages + 0x3000;
    BYTE* reserved = pages + 0x4000;
    DWORD old = 0;
    VirtualProtect(read_only, 0x1000, PAGE_READONLY, &old);
    VirtualProtect(guard, 0x1000, PAGE_READWRITE | PAGE_GUARD, &old);
    VirtualProtect(none, 0x1000, PAGE_NOACCESS, &old);
    BYTE out[16] = {};
    check(zap::safe_read(pages, out, 16) && zap::writable(pages, 16), "read-write: both");
    check(zap::safe_read(read_only, out, 16) && !zap::writable(read_only, 16),
          "read-only: read, not written");
    check(zap::safe_read(pages + 0xFF8, out, 16) && !zap::writable(pages + 0xFF8, 16),
          "across two regions: the second's page checked too");
    check(!zap::writable(guard, 1) && !zap::safe_read(guard, out, 1), "a guard page: neither");
    MEMORY_BASIC_INFORMATION info{};
    check(VirtualQuery(guard, &info, sizeof(info)) == sizeof(info) &&
              (info.Protect & PAGE_GUARD) != 0,
          "and never touched (still armed)");
    check(!zap::safe_read(read_only + 0xFF8, out, 16), "a read running onto one: refused");
    check(!zap::safe_read(none, out, 1) && !zap::writable(none, 1), "no access: neither");
    check(!zap::safe_read(reserved, out, 1) && !zap::writable(reserved, 1),
          "not committed: neither");
    check(!zap::safe_read(nullptr, out, 1) && !zap::writable(nullptr, 1), "null: neither");
    VirtualFree(pages, 0, MEM_RELEASE);
}

// A sprite's frames (its list at +0x1A8, its count at +0x1BC): 0 to count - 1 only.
void test_frames() {
    BYTE frames[3 * 0x38] = {};
    BYTE s[0x1D8] = {};
    put<BYTE*>(s, 0x1A8, frames);
    put<int32_t>(s, 0x1BC, 3);
    check(zap::frame_list(s, 0) == frames && zap::frame_list(s, 2) == frames, "frames 0 to 2");
    check(zap::frame_list(s, 3) == nullptr && zap::frame_list(s, -1) == nullptr &&
              zap::frame_list(nullptr, 0) == nullptr,
          "past the last, before the first, or no sprite: no list");
    check(zap::set_frame(s, 2) && zap::frame_of(s) == 2 &&
              get<BYTE*>(s, 0x1B0) == frames + 2 * 0x38,
          "set_frame: the index (+0x1C4) and its entry (+0x1B0)");
    check(!zap::set_frame(s, 3) && zap::frame_of(s) == 2 &&
              get<BYTE*>(s, 0x1B0) == frames + 2 * 0x38,
          "one past the last: nothing written");
    put<BYTE*>(s, 0x1A8, nullptr);
    check(zap::frame_list(s, 0) == nullptr, "no list: none");
}

// The code patches (game_mem.h): put back only where the code still reads as written, and
// still the helper's while it does.
void test_code_patches() {
    const BYTE game[2] = {0x75, 0x2E}, ours[2] = {0xEB, 0x2E}, other[2] = {0x74, 0x2E};
    auto* code = static_cast<BYTE*>(
        VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::memcpy(code, game, sizeof(game));
    DWORD old = 0;
    VirtualProtect(code, 0x1000, PAGE_EXECUTE_READ, &old);
    check(zap::code_reads(code, game, 2) && !zap::code_reads(code, ours, 2), "code_reads");
    check(!zap::code_reads(code, game, zap::kMaxCodePatch + 1) &&
              !zap::code_reads(nullptr, game, 2),
          "longer than a patch, or unreadable: no");
    check(zap::patch_code(code, ours, 2) && zap::code_reads(code, ours, 2), "patched");
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(code, &info, sizeof(info));
    check(info.Protect == PAGE_EXECUTE_READ, "its page as it was");
    check(zap::put_back_code(code, ours, game, 2) && zap::code_reads(code, game, 2),
          "put back where it reads as written");
    zap::patch_code(code, other, 2);
    check(zap::put_back_code(code, ours, game, 2) && zap::code_reads(code, other, 2),
          "rewritten since: let go, left as it is");
    VirtualFree(code, 0, MEM_RELEASE);

    // A view of a section that is not executable: it cannot be made writable and executable.
    HANDLE section =
        CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 0x1000, nullptr);
    auto* view = static_cast<BYTE*>(MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, 0x1000));
    check(view != nullptr, "a section view");
    if (view == nullptr) return;
    std::memcpy(view, ours, sizeof(ours));
    check(!zap::patch_code(view, game, 2) && zap::code_reads(view, ours, 2),
          "a page that cannot be made writable: nothing written");
    check(!zap::put_back_code(view, ours, game, 2), "a put-back that fails: still the helper's");
    static const BYTE big[zap::kMaxCodePatch + 1] = {};
    check(!zap::put_back_code(view, big, big, sizeof(big)), "one too long to check: held");
    UnmapViewOfFile(view);
    CloseHandle(section);
}

void test_build_check() {
    check(std::string(zap::identify_build(0x6A3F82DE, 0x1204000)) == "B1", "knows B1");
    check(zap::identify_build(0x6A3F82DE, 0x1205000) == nullptr, "refuses another image size");
    check(zap::identify_build(0, 0x1204000) == nullptr, "refuses another timestamp");
    check(zap::identify_build(GetModuleHandleW(nullptr)) == nullptr, "this program is no build");
}

void test_pipe() {
    std::wstring name = zap::pipe_name(GetCurrentProcessId()) + L"-test";
    std::atomic<bool> served{false};
    std::thread server([&] {
        served = zap::serve_pipe(name, [](const std::string& request, bool& stop) {
            if (request == "stop") stop = true;
            return "echo " + request;
        });
    });
    HANDLE pipe = zap_test::connect(name);
    check(pipe != INVALID_HANDLE_VALUE, "connects to the pipe");
    check(!zap::serve_pipe(name, [](const std::string&, bool&) { return std::string(); }),
          "a second pipe by the same name is refused");
    if (pipe != INVALID_HANDLE_VALUE) {
        check(zap_test::ask(pipe, "hello") == "echo hello", "one reply per request");
        CloseHandle(pipe);
        pipe = zap_test::connect(name);
        check(pipe != INVALID_HANDLE_VALUE, "a new client after one leaves");
        check(zap_test::ask(pipe, "stop") == "echo stop", "replies before stopping");
        CloseHandle(pipe);
    }
    server.join();
    check(served.load(), "serve_pipe reports a clean stop");
}

}  // namespace

int main() {
    test_import_slot_hook();
    test_trampoline();
    test_game_mem_helpers();
    test_memory_checks();
    test_frames();
    test_code_patches();
    test_build_check();
    test_pipe();
    return report("parts tests");
}
