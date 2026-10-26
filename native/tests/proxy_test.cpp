// The proxy version.dll (native/proxy/): loaded into this test program, its exports give what
// Windows' own version.dll gives, and, the host not being Zexion.exe, it loads no helper.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"

namespace {

std::wstring widen(const char* s) {
    std::wstring out;
    for (; *s != '\0'; ++s) out += static_cast<wchar_t>(*s == '/' ? '\\' : *s);
    return out;
}

}  // namespace

int main() {
    std::wstring proxy_path = widen(ZAP_PROXY_PATH);
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring real_path = std::wstring(sys) + L"\\version.dll";
    std::wstring kernel = std::wstring(sys) + L"\\kernel32.dll";

    HMODULE proxy = LoadLibraryW(proxy_path.c_str());
    check(proxy != nullptr, "the proxy loads");
    HMODULE real = LoadLibraryW(real_path.c_str());
    check(real != nullptr && real != proxy, "Windows' own loads beside it");
    if (proxy == nullptr || real == nullptr) return 1;

    using SizeW = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    using InfoW = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    using Query = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    auto p_size = reinterpret_cast<SizeW>(GetProcAddress(proxy, "GetFileVersionInfoSizeW"));
    auto r_size = reinterpret_cast<SizeW>(GetProcAddress(real, "GetFileVersionInfoSizeW"));
    auto p_info = reinterpret_cast<InfoW>(GetProcAddress(proxy, "GetFileVersionInfoW"));
    auto p_query = reinterpret_cast<Query>(GetProcAddress(proxy, "VerQueryValueW"));
    check(p_size != nullptr && r_size != nullptr && p_info != nullptr && p_query != nullptr,
          "the exports are there");
    if (p_size == nullptr || r_size == nullptr || p_info == nullptr || p_query == nullptr) return 1;
    check(GetProcAddress(proxy, "GetFileVersionInfoSizeW") != GetProcAddress(real,
                                                                             "GetFileVersionInfoSizeW"),
          "the proxy's own export, not Windows'");

    DWORD handle = 0;
    DWORD size = p_size(kernel.c_str(), &handle);
    check(size != 0 && size == r_size(kernel.c_str(), &handle), "the same size as the real one");
    std::vector<BYTE> data(size);
    check(p_info(kernel.c_str(), 0, size, data.data()) != FALSE, "the version info read");
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT n = 0;
    check(p_query(data.data(), L"\\", reinterpret_cast<LPVOID*>(&fixed), &n) != FALSE &&
              fixed != nullptr && fixed->dwSignature == 0xFEEF04BD,
          "and queried");

    // The forwarders of the real one, kept as forwarders.
    using LangW = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
    auto lang = reinterpret_cast<LangW>(GetProcAddress(proxy, "VerLanguageNameW"));
    wchar_t name[64] = {};
    check(lang != nullptr && lang(0x409, name, 64) != 0, "VerLanguageNameW forwarded");

    // Ordinals as Windows' own: 5 is GetFileVersionInfoSizeA in both.
    check(GetProcAddress(proxy, MAKEINTRESOURCEA(5)) == GetProcAddress(proxy, "GetFileVersionInfoSizeA"),
          "the ordinals kept");

    // Not Zexion.exe: no helper, whatever is beside it.
    Sleep(200);
    check(GetModuleHandleW(L"zexion_ap.dll") == nullptr, "no helper outside the game");

    if (g_failures == 0) std::printf("proxy test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
