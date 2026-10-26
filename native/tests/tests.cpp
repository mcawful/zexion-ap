// Host tests for the helper's parts that do not need the game: the import-table hook and the
// trampoline (on this test program's own import of GetTickCount), the build check and the
// pipe. unload_test.cpp loads and unloads the whole helper under load.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "check.h"
#include "build.h"
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
    test_build_check();
    test_pipe();
    if (g_failures == 0) std::printf("all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
