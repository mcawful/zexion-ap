#include "pipe.h"

#include <windows.h>

#include <sddl.h>

namespace zap {

namespace {

constexpr DWORD kBufferSize = 64 * 1024;

// Reads one whole message, or returns false when the client has gone or sent too much.
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

// A security descriptor granting the current user, and nobody else, access.
PSECURITY_DESCRIPTOR current_user_only() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
    BYTE buffer[256];
    DWORD size = 0;
    BOOL ok = GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &size);
    CloseHandle(token);
    if (!ok) return nullptr;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid, &sid)) {
        return nullptr;
    }
    std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              &descriptor, nullptr)) {
        return nullptr;
    }
    return descriptor;
}

}  // namespace

std::wstring pipe_name(unsigned long pid) {
    return L"\\\\.\\pipe\\zexion-ap-" + std::to_wstring(pid);
}

bool serve_pipe(const std::wstring& name, const Handler& handle) {
    PSECURITY_DESCRIPTOR descriptor = current_user_only();
    if (descriptor == nullptr) return false;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    HANDLE pipe = CreateNamedPipeW(
        name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
        kBufferSize, kBufferSize, 0, &attributes);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE) return false;
    bool stop = false;
    while (!stop) {
        if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            // A client that came and went before we saw it (ERROR_NO_DATA): reset and wait again.
            DisconnectNamedPipe(pipe);
            Sleep(10);
            continue;
        }
        std::string request;
        while (!stop && read_message(pipe, request)) {
            std::string reply = handle(request, stop);
            DWORD written = 0;
            if (!WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()), &written,
                           nullptr)) {
                break;
            }
        }
        if (stop) FlushFileBuffers(pipe);  // let the last reply reach the client
        DisconnectNamedPipe(pipe);
    }
    CloseHandle(pipe);
    return true;
}

}  // namespace zap
