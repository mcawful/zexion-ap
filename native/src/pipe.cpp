#include "pipe.h"

#include <windows.h>

namespace zap {

namespace {

constexpr DWORD kBufferSize = 64 * 1024;

// Reads one whole message, or returns false when the client has gone.
bool read_message(HANDLE pipe, std::string& out) {
    out.clear();
    char buffer[4096];
    for (;;) {
        DWORD got = 0;
        BOOL ok = ReadFile(pipe, buffer, sizeof(buffer), &got, nullptr);
        if (got > 0) out.append(buffer, got);
        if (ok) return true;
        if (GetLastError() != ERROR_MORE_DATA) return false;
        if (out.size() > kBufferSize) return false;  // no request is this long
    }
}

}  // namespace

std::wstring pipe_name(unsigned long pid) {
    return L"\\\\.\\pipe\\zexion-ap-" + std::to_wstring(pid);
}

void serve_pipe(const std::wstring& name, const Handler& handle) {
    for (;;) {
        HANDLE pipe = CreateNamedPipeW(
            name.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, kBufferSize, kBufferSize, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) return;
        BOOL connected =
            ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        bool stop = false;
        std::string request;
        while (connected && !stop && read_message(pipe, request)) {
            std::string reply = handle(request, stop);
            DWORD written = 0;
            if (!WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()), &written,
                           nullptr)) {
                break;
            }
            FlushFileBuffers(pipe);
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        if (stop) return;
    }
}

}  // namespace zap
