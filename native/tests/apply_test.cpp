// Grants and popups end to end through the real pipe and hook: the hook-test helper (argv[1])
// hooks this program's GetTickCount, which two busy threads call as the game's frames, and is
// configured with the stand-in game (stand_in.h). Covers a batch and its popup, a batch no
// frame takes, and an unload refused with something hooked over the helper (what the client
// set is armed again), and a seed file put while a client is connected reaching the offline look
// from the next frame call, not only the AP-save test (dllmain.cpp, take_offline). LOCALAPPDATA
// points at a folder of the test's own under %TEMP%, so the real one is neither read nor written.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

uint8_t g_table[154];
const uint8_t g_start[6] = {6, 0, 191, 0, 0x49, 1};

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: zap_apply_test PATH\\zexion_ap_hooktest.dll\n");
        return EXIT_FAILURE;
    }
    // A folder of this run's own (another worktree's run may be going on).
    const std::filesystem::path base =
        std::filesystem::temp_directory_path() /
        (L"zap_put_test_" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    // Read by the helper's offline_folder, in this process. Not loaded unless it reads back as
    // set: the put would go into the real folder.
    wchar_t now[MAX_PATH] = {};
    if (SetEnvironmentVariableW(L"LOCALAPPDATA", base.wstring().c_str()) == 0 ||
        GetEnvironmentVariableW(L"LOCALAPPDATA", now, MAX_PATH) == 0 || base.wstring() != now) {
        std::printf("FAIL: LOCALAPPDATA not set to the test's folder\n");
        std::filesystem::remove_all(base);
        return EXIT_FAILURE;
    }
    stand_in::reset_game();
    // Seed B's AP save loaded, in play.
    for (int i = 0; i < 154; ++i) g_table[i] = static_cast<uint8_t>(i % 40);
    std::memset(stand_in::g_section, 0, sizeof(stand_in::g_section));
    std::memcpy(stand_in::g_section + 0x26C, g_table, sizeof(g_table));
    std::memcpy(stand_in::g_section + 0x310, g_start, sizeof(g_start));
    stand_in::g_rando_marker = 1;
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

    // The client of another seed puts seed B's file (a seed it started earlier in its run).
    reply = zap_test::ask(pipe, "other_save");
    check(reply == "unknown", "no file for seed B: unknown", reply);
    const std::string file = "zexion-ap offline 1\ntable " + hex(g_table, sizeof(g_table)) +
                             "\nstart " + hex(g_start, sizeof(g_start)) +
                             "\nintel 1\nassists lock\ndiscs -\nunfound -\n";
    reply = zap_test::ask(pipe, "offline_put seed 12345-0-1\n" + file);
    check(reply == "stored", "seed B put", reply);
    check(std::filesystem::is_regular_file(base / L"ZexionAP" / L"seeds" / L"12345-0-1.txt"),
          "written into the test's folder");
    for (int i = 0; i < 200 && reply != "told"; ++i) {  // asked again until a frame has taken it
        reply = zap_test::ask(pipe, "other_save");
        if (reply != "told") Sleep(5);
    }
    check(reply == "told", "known to the look from the next frame: told", reply);
    hello = zap_test::ask(pipe, "hello");
    check(hello.find(" offline=1") != std::string::npos, "hello: one seed", hello);

    check(zap_test::ask(pipe, "unload") == "bye", "unload");
    CloseHandle(pipe);
    check(zap_test::unloaded(L"zexion_ap_hooktest.dll"), "unloaded");
    g_stop = true;
    c.join();
    std::error_code ignored;
    std::filesystem::remove_all(base, ignored);
    return report("apply_in_frames test");
}
