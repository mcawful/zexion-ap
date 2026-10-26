// A minimal client for the helper's pipe, and what the hook-test hosts share: the busy "frame"
// threads and the wait for an unload.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace zap_test {

// Connects to `name`, retrying for up to two seconds. INVALID_HANDLE_VALUE if nobody answers.
inline HANDLE connect(const std::wstring& name) {
    for (int i = 0; i < 200; ++i) {
        HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
            return pipe;
        }
        Sleep(10);
    }
    return INVALID_HANDLE_VALUE;
}

inline std::string ask(HANDLE pipe, const std::string& request) {
    DWORD n = 0;
    if (!WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()), &n, nullptr)) {
        return "(write failed)";
    }
    char buffer[1024];
    if (!ReadFile(pipe, buffer, sizeof(buffer), &n, nullptr)) return "(read failed)";
    return std::string(buffer, n);
}

// Calls GetTickCount, the hook-test helper's target, through this program's import table
// nonstop, as the game calls its frame function, until `stop`; counted in `calls` if given.
inline void busy(const std::atomic<bool>& stop, std::atomic<uint64_t>* calls = nullptr) {
    while (!stop.load()) {
        volatile DWORD t = GetTickCount();
        (void)t;
        if (calls != nullptr) calls->fetch_add(1);
    }
}

// Whether `dll` is unloaded within two seconds.
inline bool unloaded(const wchar_t* dll) {
    for (int i = 0; i < 2000 && GetModuleHandleW(dll) != nullptr; ++i) Sleep(1);
    return GetModuleHandleW(dll) == nullptr;
}

}  // namespace zap_test
