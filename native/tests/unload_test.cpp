// Loads and unloads the helper many times while two threads call the hooked function nonstop,
// as the game calls SDL_GL_SwapWindow. The hook-test build of the helper
// (zexion_ap_hooktest.dll, argv[1]) hooks this program's own import of GetTickCount. If an
// unload could free code a thread was still running, this crashes.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "pipe.h"
#include "pipe_client.h"

namespace {

std::atomic<bool> g_stop{false};
std::atomic<uint64_t> g_calls{0};

void busy() { zap_test::busy(g_stop, &g_calls); }

bool fail(const char* what, int round) {
    std::printf("FAIL in round %d: %s\n", round, what);
    return false;
}

bool one_round(const wchar_t* dll, const wchar_t* dll_name, int round) {
    if (LoadLibraryW(dll) == nullptr) return fail("LoadLibrary", round);
    HANDLE pipe = zap_test::connect(zap::pipe_name(GetCurrentProcessId()));
    if (pipe == INVALID_HANDLE_VALUE) return fail("connect", round);
    std::string hello = zap_test::ask(pipe, "hello");
    if (hello.find("hooked=1") == std::string::npos) return fail(hello.c_str(), round);
    // Let the busy thread go through the hook a while.
    std::string pong;
    for (int i = 0; i < 1000; ++i) {
        pong = zap_test::ask(pipe, "ping");
        if (pong != "pong frames=0") break;
        Sleep(1);
    }
    if (pong == "pong frames=0") return fail("the hook never ran", round);
    if (zap_test::ask(pipe, "unload") != "bye") return fail("unload", round);
    CloseHandle(pipe);
    if (!zap_test::unloaded(dll_name)) return fail("still loaded", round);
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: zap_unload_test PATH\\zexion_ap_hooktest.dll [ROUNDS]\n");
        return EXIT_FAILURE;
    }
    int rounds = argc > 2 ? _wtoi(argv[2]) : 200;
    std::thread worker1(busy);
    std::thread worker2(busy);
    bool ok = true;
    for (int round = 0; ok && round < rounds; ++round) {
        ok = one_round(argv[1], L"zexion_ap_hooktest.dll", round);
    }
    g_stop = true;
    worker1.join();
    worker2.join();
    if (ok) {
        std::printf("%d rounds, %llu calls through the hooked import\n", rounds,
                    static_cast<unsigned long long>(g_calls.load()));
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
