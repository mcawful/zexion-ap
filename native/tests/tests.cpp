// Host tests for the helper's parts that do not need the game: the import-table hook (on this
// test program's own import of GetTickCount), the build check and the pipe.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "build.h"
#include "iat.h"
#include "pipe.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

DWORD WINAPI fake_tick_count() { return 42; }

void test_import_slot_hook() {
    HMODULE self = GetModuleHandleW(nullptr);
    void** slot = zap::find_import_slot(self, "kernel32.dll", "GetTickCount");
    check(slot != nullptr, "finds this program's GetTickCount import (any case)");
    if (slot == nullptr) return;
    void* real = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount"));
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

void test_build_check() {
    check(std::string(zap::identify_build(0x6A3F82DE, 0x1204000)) == "B1", "knows B1");
    check(zap::identify_build(0x6A3F82DE, 0x1205000) == nullptr, "refuses another image size");
    check(zap::identify_build(0, 0x1204000) == nullptr, "refuses another timestamp");
    check(zap::identify_build(GetModuleHandleW(nullptr)) == nullptr, "this program is no build");
}

void test_pipe() {
    std::wstring name = zap::pipe_name(GetCurrentProcessId()) + L"-test";
    std::thread server([&] {
        zap::serve_pipe(name, [](const std::string& request, bool& stop) {
            if (request == "stop") stop = true;
            return "echo " + request;
        });
    });
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 200 && pipe == INVALID_HANDLE_VALUE; ++i) {
        pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) Sleep(10);
    }
    check(pipe != INVALID_HANDLE_VALUE, "connects to the pipe");
    if (pipe != INVALID_HANDLE_VALUE) {
        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
        auto ask = [&](const std::string& text) {
            DWORD n = 0;
            WriteFile(pipe, text.data(), static_cast<DWORD>(text.size()), &n, nullptr);
            char buffer[256];
            ReadFile(pipe, buffer, sizeof(buffer), &n, nullptr);
            return std::string(buffer, n);
        };
        check(ask("hello") == "echo hello", "one reply per request");
        check(ask("stop") == "echo stop", "replies before stopping");
        CloseHandle(pipe);
    }
    server.join();
}

}  // namespace

int main() {
    test_import_slot_hook();
    test_build_check();
    test_pipe();
    if (g_failures == 0) std::printf("all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
