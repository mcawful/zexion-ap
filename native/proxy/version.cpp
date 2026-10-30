// version.dll proxy, copied by the player beside Zexion.exe so the helper loads with the game
// (native/README.md, "The proxy"). SDL2.dll imports version.dll, not a KnownDLL, so this one
// loads first; every export passes on to the system's. DllMain only starts load_helper, which
// pins this DLL, loads the real one, and loads zexion_ap.dll from its own folder (host
// Zexion.exe, no zexion_ap.disabled beside it) under the client's load mutex.
#include <windows.h>

#include <cstdint>
#include <cwchar>

namespace {

HMODULE g_self = nullptr;
HMODULE g_real = nullptr;
INIT_ONCE g_real_once = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK load_real(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n + 13 >= MAX_PATH) return TRUE;
    wcscat_s(path, L"\\version.dll");
    HMODULE real = LoadLibraryW(path);
    // Defensive: never take our own module for the real one.
    if (real != nullptr && real != g_self) g_real = real;
    return TRUE;
}

FARPROC real_function(const char* name) {
    InitOnceExecuteOnce(&g_real_once, load_real, nullptr, nullptr);
    return g_real != nullptr ? GetProcAddress(g_real, name) : nullptr;
}

// The directory this DLL was loaded from, with a trailing backslash, into `dir`; false if it
// cannot be found.
bool own_folder(wchar_t (&dir)[MAX_PATH]) {
    DWORD n = GetModuleFileNameW(g_self, dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash == nullptr) return false;
    slash[1] = L'\0';
    return true;
}

bool host_is_zexion() {
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    const wchar_t* name = wcsrchr(exe, L'\\');
    name = name != nullptr ? name + 1 : exe;
    return CompareStringOrdinal(name, -1, L"Zexion.exe", -1, TRUE) == CSTR_EQUAL;
}

DWORD WINAPI load_helper(void*) {
    // Stay loaded: the game holds pointers into this DLL's exports.
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&load_helper), &pinned);
    // The real version.dll loaded here first, outside any other DLL's DllMain, so a first call
    // made while another thread holds the loader lock finds it loaded already.
    InitOnceExecuteOnce(&g_real_once, load_real, nullptr, nullptr);
    wchar_t dir[MAX_PATH], helper[MAX_PATH], off[MAX_PATH];
    if (!host_is_zexion() || !own_folder(dir)) return 0;
    if (wcscpy_s(helper, dir) != 0 || wcscat_s(helper, L"zexion_ap.dll") != 0) return 0;
    if (wcscpy_s(off, dir) != 0 || wcscat_s(off, L"zexion_ap.disabled") != 0) return 0;
    if (GetFileAttributesW(off) != INVALID_FILE_ATTRIBUTES) return 0;  // turned off
    if (GetFileAttributesW(helper) == INVALID_FILE_ATTRIBUTES) return 0;
    // The AP client's lock (client/native.py, load): it loads the helper only if it is absent.
    wchar_t lock_name[64];
    swprintf_s(lock_name, L"Local\\zexion-ap-load-%lu", GetCurrentProcessId());
    HANDLE lock = CreateMutexW(nullptr, FALSE, lock_name);
    if (lock == nullptr) return 0;
    DWORD waited = WaitForSingleObject(lock, 5000);
    if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) {
        if (GetModuleHandleW(L"zexion_ap.dll") == nullptr) {
            // Never freed here: the helper unloads itself when asked (FreeLibraryAndExitThread),
            // and this is its one reference.
            LoadLibraryExW(helper, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
        ReleaseMutex(lock);
    }
    CloseHandle(lock);
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        // Runs once the process has started (the loader lock is free then); nothing else here.
        HANDLE thread = CreateThread(nullptr, 0, load_helper, nullptr, 0, nullptr);
        if (thread != nullptr) CloseHandle(thread);
    }
    return TRUE;
}

// Each export as the real one's prototype (winver.h), passed on; 0 if the real one is missing.
#define ZAP_PASS(ret, name, params, args)                                         \
    extern "C" ret WINAPI p_##name params {                                       \
        using Fn = ret(WINAPI*) params;                                           \
        auto fn = reinterpret_cast<Fn>(real_function(#name));                     \
        if (fn == nullptr) {                                                      \
            SetLastError(ERROR_PROC_NOT_FOUND);                                   \
            return 0;                                                             \
        }                                                                         \
        return fn args;                                                           \
    }

ZAP_PASS(BOOL, GetFileVersionInfoA, (LPCSTR f, DWORD h, DWORD n, LPVOID d), (f, h, n, d))
// Not in winver.h: each argument passed at full register width, whatever the real types are.
ZAP_PASS(BOOL, GetFileVersionInfoByHandle, (uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d),
         (a, b, c, d))
ZAP_PASS(BOOL, GetFileVersionInfoExA, (DWORD fl, LPCSTR f, DWORD h, DWORD n, LPVOID d),
         (fl, f, h, n, d))
ZAP_PASS(BOOL, GetFileVersionInfoExW, (DWORD fl, LPCWSTR f, DWORD h, DWORD n, LPVOID d),
         (fl, f, h, n, d))
ZAP_PASS(DWORD, GetFileVersionInfoSizeA, (LPCSTR f, LPDWORD h), (f, h))
ZAP_PASS(DWORD, GetFileVersionInfoSizeExA, (DWORD fl, LPCSTR f, LPDWORD h), (fl, f, h))
ZAP_PASS(DWORD, GetFileVersionInfoSizeExW, (DWORD fl, LPCWSTR f, LPDWORD h), (fl, f, h))
ZAP_PASS(DWORD, GetFileVersionInfoSizeW, (LPCWSTR f, LPDWORD h), (f, h))
ZAP_PASS(BOOL, GetFileVersionInfoW, (LPCWSTR f, DWORD h, DWORD n, LPVOID d), (f, h, n, d))
ZAP_PASS(DWORD, VerFindFileA,
         (DWORD fl, LPCSTR f, LPCSTR w, LPCSTR a, LPSTR c, PUINT cn, LPSTR d, PUINT dn),
         (fl, f, w, a, c, cn, d, dn))
ZAP_PASS(DWORD, VerFindFileW,
         (DWORD fl, LPCWSTR f, LPCWSTR w, LPCWSTR a, LPWSTR c, PUINT cn, LPWSTR d, PUINT dn),
         (fl, f, w, a, c, cn, d, dn))
ZAP_PASS(DWORD, VerInstallFileA,
         (DWORD fl, LPCSTR s, LPCSTR d, LPCSTR sd, LPCSTR dd, LPCSTR cd, LPSTR t, PUINT tn),
         (fl, s, d, sd, dd, cd, t, tn))
ZAP_PASS(DWORD, VerInstallFileW,
         (DWORD fl, LPCWSTR s, LPCWSTR d, LPCWSTR sd, LPCWSTR dd, LPCWSTR cd, LPWSTR t, PUINT tn),
         (fl, s, d, sd, dd, cd, t, tn))
ZAP_PASS(BOOL, VerQueryValueA, (LPCVOID b, LPCSTR s, LPVOID* p, PUINT n), (b, s, p, n))
ZAP_PASS(BOOL, VerQueryValueW, (LPCVOID b, LPCWSTR s, LPVOID* p, PUINT n), (b, s, p, n))
