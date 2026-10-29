// A seed file put through the real pipe while a client is connected reaches the offline look
// from the next frame call, not only the AP-save test (dllmain.cpp, take_offline): the
// hook-test helper (argv[1]) hooks this program's GetTickCount, which a busy thread calls as the
// game's frames, and is configured with the stand-in game (stand_in.h). LOCALAPPDATA points at a
// folder of the test's own under %TEMP%, so nothing is written into the real one.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include "check.h"
#include "pipe.h"
#include "pipe_client.h"
#include "stand_in.h"

namespace {

std::atomic<bool> g_stop{false};

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

// Asks `request` until it answers `want` (frame requests wait for a frame call), up to a second.
std::string ask_until(HANDLE pipe, const std::string& request, const std::string& want) {
    std::string reply;
    for (int i = 0; i < 200; ++i) {
        reply = zap_test::ask(pipe, request);
        if (reply == want) break;
        Sleep(5);
    }
    return reply;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: zap_offline_put_test PATH\\zexion_ap_hooktest.dll\n");
        return EXIT_FAILURE;
    }
    const std::filesystem::path base = std::filesystem::temp_directory_path() / L"zap_put_test";
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    // Read by the helper's offline_folder, in this process.
    check(SetEnvironmentVariableW(L"LOCALAPPDATA", base.wstring().c_str()) != 0,
          "LOCALAPPDATA set");

    stand_in::reset_game();
    // Seed B's AP save loaded, in play.
    for (int i = 0; i < 154; ++i) g_table[i] = static_cast<uint8_t>(i % 40);
    std::memset(stand_in::g_section, 0, sizeof(stand_in::g_section));
    std::memcpy(stand_in::g_section + 0x26C, g_table, sizeof(g_table));
    std::memcpy(stand_in::g_section + 0x310, g_start, sizeof(g_start));
    stand_in::g_rando_marker = 1;
    stand_in::g_save_loaded = 3;
    stand_in::g_menu = 0;

    std::thread a(busy);
    if (LoadLibraryW(argv[1]) == nullptr) {
        std::printf("FAIL: LoadLibrary\n");
        g_stop = true;
        a.join();
        return EXIT_FAILURE;
    }
    HANDLE pipe = zap_test::connect(zap::pipe_name(GetCurrentProcessId()));
    check(pipe != INVALID_HANDLE_VALUE, "connects");
    std::string reply = zap_test::ask(pipe, config_request());
    check(reply == "ok", "config", reply);
    reply = zap_test::ask(pipe, "other_save");
    check(reply == "unknown", "no file for seed B: unknown", reply);

    // The client of another seed puts seed B's file (a seed it started earlier in its run).
    const std::string file = "zexion-ap offline 1\ntable " + hex(g_table, sizeof(g_table)) +
                             "\nstart " + hex(g_start, sizeof(g_start)) +
                             "\nintel 1\nassists lock\ndiscs -\nunfound -\n";
    reply = zap_test::ask(pipe, "offline_put seed 12345-0-1\n" + file);
    check(reply == "stored", "seed B put", reply);
    reply = ask_until(pipe, "other_save", "told");
    check(reply == "told", "known to the look from the next frame: told", reply);
    std::string hello = zap_test::ask(pipe, "hello");
    check(hello.find(" offline=1") != std::string::npos, "hello: one seed", hello);

    check(zap_test::ask(pipe, "unload") == "bye", "unload");
    CloseHandle(pipe);
    check(zap_test::unloaded(L"zexion_ap_hooktest.dll"), "unloaded");
    g_stop = true;
    a.join();
    std::error_code ignored;
    std::filesystem::remove_all(base, ignored);
    stand_in::g_save_loaded = 0xFF;
    stand_in::g_rando_marker = 0;
    return report("offline_put test");
}
