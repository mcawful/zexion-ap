// Helper phase 2 end to end, through the real pipe and hook: the hook-test build of the helper
// (argv[1]) is loaded into this program, which plays the game. Two busy threads call the
// hooked GetTickCount like the game's frames; the helper is configured with this program's
// stand-in game (stand_in.h) and applies a batch inside those calls. With the calls stopped, a
// batch is refused for want of a frame and is never applied afterwards.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "pipe.h"
#include "pipe_client.h"
#include "stand_in.h"

namespace {

std::atomic<bool> g_stop{false};

void busy() {
    while (!g_stop.load()) {
        volatile DWORD t = GetTickCount();
        (void)t;
    }
}

int g_failures = 0;
void check(bool ok, const char* what, const std::string& detail = "") {
    if (!ok) {
        std::printf("FAIL: %s %s\n", what, detail.c_str());
        ++g_failures;
    }
}

std::string config_request() {
    std::string request = "config";
    for (const auto& [key, rva] : stand_in::stand_in_config()) {
        request += " " + key + "=" + std::to_string(rva);
    }
    return request;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: zap_apply_test PATH\\zexion_ap_hooktest.dll\n");
        return EXIT_FAILURE;
    }
    stand_in::reset_game();
    std::thread a(busy), b(busy);
    if (LoadLibraryW(argv[1]) == nullptr) {
        std::printf("FAIL: LoadLibrary\n");
        return EXIT_FAILURE;
    }
    HANDLE pipe = zap_test::connect(zap::pipe_name(GetCurrentProcessId()));
    check(pipe != INVALID_HANDLE_VALUE, "connects");
    std::string hello = zap_test::ask(pipe, "hello");
    check(hello.find("hooked=1 configured=0") != std::string::npos, "hello before config", hello);
    std::string reply = zap_test::ask(pipe, "apply\ncas 36 0 1");
    check(reply == "refused not configured", "apply before config", reply);
    reply = zap_test::ask(pipe, config_request());
    check(reply == "ok", "config", reply);
    hello = zap_test::ask(pipe, "hello");
    check(hello.find("configured=1") != std::string::npos, "hello after config", hello);

    reply = zap_test::ask(pipe, "apply\ncas 36 0 1\npopup 36 FROM ALICE");
    check(reply == "applied", "apply in the frame calls", reply);
    check(stand_in::g_inventory[36] == 1 && stand_in::field<int16_t>(0xCC0) == 13,
          "the batch landed in the stand-in game");
    for (int i = 0; i < 200 && stand_in::g_messages == 0; ++i) Sleep(5);
    check(stand_in::g_messages == 1 &&
              stand_in::g_text == "\xC5MISSILE BOX FROM ALICE" && stand_in::g_message_args[0] == 12,
          "the popup was shown from a frame call, the sender on its own line", stand_in::g_text);

    reply = zap_test::ask(pipe, "apply\ncas 36 0 1");
    check(reply == "refused item 36 holds 1", "a stale expectation is refused", reply);

    // No frames: the request must be withdrawn and never applied later.
    g_stop = true;
    a.join();
    b.join();
    reply = zap_test::ask(pipe, "apply\ncas 17 0 1");
    check(reply == "refused no frame", "no frame within the wait", reply);
    g_stop = false;
    std::thread c(busy);
    Sleep(200);
    check(stand_in::g_inventory[17] == 0, "a withdrawn batch is never applied");

    check(zap_test::ask(pipe, "unload") == "bye", "unload");
    CloseHandle(pipe);
    for (int i = 0; i < 2000 && GetModuleHandleW(L"zexion_ap_hooktest.dll") != nullptr; ++i) Sleep(1);
    check(GetModuleHandleW(L"zexion_ap_hooktest.dll") == nullptr, "unloaded");
    g_stop = true;
    c.join();
    if (g_failures == 0) std::printf("all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
