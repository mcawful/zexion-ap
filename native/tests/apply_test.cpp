// Grants and popups end to end through the real pipe and hook: the hook-test helper (argv[1])
// hooks this program's GetTickCount, which two busy threads call as the game's frames, and is
// configured with the stand-in game (stand_in.h). Covers a batch and its popup, a batch no
// frame takes, and an unload refused with something hooked over the helper (what the client
// set is armed again).

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "check.h"
#include "iat.h"
#include "pipe.h"
#include "pipe_client.h"
#include "stand_in.h"

namespace {

std::atomic<bool> g_stop{false};

// Something else hooked over the helper: it calls what the slot held before it, the helper's
// trampoline.
std::atomic<void*> g_next{nullptr};
DWORD WINAPI chained_tick() { return reinterpret_cast<DWORD(WINAPI*)()>(g_next.load())(); }

void busy() { zap_test::busy(g_stop); }

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

    // A refused unload: the new-game plan the client armed is armed again.
    reply = zap_test::ask(pipe, "ap_newgame\nwrite 0x310 11");
    check(reply == "armed", "a plan armed", reply);
    // A menu open in a save: refused before anything is put back.
    stand_in::g_menu = 2;
    reply = zap_test::ask(pipe, "unload");
    check(reply == "error a menu is open; not unloading", "refused with a menu open", reply);
    reply = zap_test::ask(pipe, "newgame_status");
    check(reply == "armed", "nothing put back under the menu", reply);
    stand_in::g_menu = 0;
    void** slot = zap::find_import_slot(GetModuleHandleW(nullptr), "kernel32.dll", "GetTickCount");
    void* other = reinterpret_cast<void*>(&chained_tick);
    g_next = slot != nullptr ? *slot : nullptr;
    check(slot != nullptr && zap::swap_slot(slot, g_next.load(), other),
          "something else hooked over the helper");
    reply = zap_test::ask(pipe, "unload");
    check(reply == "error the frame hook was replaced by something else; not unloading",
          "the unload refused", reply);
    reply = zap_test::ask(pipe, "newgame_status");
    check(reply == "armed", "the plan armed again", reply);
    check(slot != nullptr && zap::swap_slot(slot, other, g_next.load()), "the other hook gone");
    Sleep(50);  // no call left in chained_tick

    check(zap_test::ask(pipe, "unload") == "bye", "unload");
    CloseHandle(pipe);
    check(zap_test::unloaded(L"zexion_ap_hooktest.dll"), "unloaded");
    g_stop = true;
    c.join();
    return report("apply_in_frames test");
}
