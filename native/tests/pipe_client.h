// A minimal client for the helper's pipe, for the native tests.
#pragma once

#include <windows.h>

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

}  // namespace zap_test
