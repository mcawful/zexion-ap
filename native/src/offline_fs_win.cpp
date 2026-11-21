// The seed files' Windows half: the file system under offline.cpp (offline_fs.h), the per-user
// folders, and the cache of the game's addresses that only the Windows helper keeps.

#include <map>
#include <sstream>

#include "game_mem.h"  // safe_read
#include "offline_fs.h"
#include "requests.h"  // parse_config

namespace zap {

namespace fs {

constexpr size_t kCheckBytes = 16;

bool read_file(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                                                           FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart >= 0 &&
              static_cast<uint64_t>(size.QuadPart) <= kMaxFile;
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        ok = out.empty() || (ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &got,
                                      nullptr) &&
                             got == out.size());
    }
    CloseHandle(f);
    return ok;
}

std::string write_whole(const std::wstring& path, const std::string& text, bool& changed) {
    std::string old;
    changed = !(read_file(path, old) && old == text);
    if (!changed) return "";
    if (!write_allowed(path)) return "refused: not a test folder";
    std::wstring temp = path + L".tmp";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return "cannot create, error " + std::to_string(GetLastError());
    DWORD written = 0;
    bool ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
              written == text.size() && FlushFileBuffers(f);  // on disk before it replaces the old
    CloseHandle(f);
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD e = GetLastError();
        DeleteFileW(temp.c_str());
        return "cannot write, error " + std::to_string(e);
    }
    return "";
}

std::wstring join(const std::wstring& folder, const std::string& name) {
    return folder + L"\\" + widen(name);
}

void make_folder(const std::wstring& folder) {
    if (folder_allowed(folder)) CreateDirectoryW(folder.c_str(), nullptr);
}

bool folder_exists(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// Whether anything is at `path`, whatever it holds.
bool exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::vector<std::pair<uint64_t, std::string>> list_files(const std::wstring& folder,
                                                         const std::string& prefix,
                                                         const std::string& suffix,
                                                         std::string* error) {
    std::vector<std::pair<uint64_t, std::string>> names;
    WIN32_FIND_DATAW found{};
    HANDLE find = FindFirstFileW(
        (folder + L"\\" + widen(prefix) + L"*" + widen(suffix)).c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && error != nullptr) {
            *error = "error " + std::to_string(e) + " in " + narrow(folder);
        }
        return names;
    }
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        const std::wstring file = found.cFileName;
        // The pattern may be loose (a short name matches too): the name exactly, and only ASCII
        // ones, which the helper's own files are.
        if (file.size() <= prefix.size() + suffix.size() ||
            std::any_of(file.begin(), file.end(), [](wchar_t c) { return c >= 128; })) {
            continue;
        }
        const std::string name = narrow(file);
        if (name.compare(0, prefix.size(), prefix) != 0 ||
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        const uint64_t written =
            (static_cast<uint64_t>(found.ftLastWriteTime.dwHighDateTime) << 32) |
            found.ftLastWriteTime.dwLowDateTime;
        names.emplace_back(written, name);
    } while (FindNextFileW(find, &found));
    FindClose(find);
    sort_newest_first(names);
    return names;
}

// The cached config's file for `build` in `folder`.
std::wstring config_path(const std::wstring& folder, const char* build) {
    return folder + L"\\config-" + widen(build) + L".txt";
}

// The cached config: the `config` request, then "check KEY HEX" lines, at least
// kOfflineMinChecks, each on a key of the config. "" or what is wrong.
std::string parse_offline_config(const std::string& text, std::map<std::string, uint64_t>& rvas,
                                 std::map<std::string, std::string>& checks) {
    rvas.clear();
    checks.clear();
    std::istringstream lines(text);
    std::string first, line;
    next_line(lines, first);
    std::string why = parse_config(first, rvas);
    if (!why.empty()) return why;
    while (next_line(lines, line)) {
        std::istringstream words(line);
        std::string op, key, hex, extra;
        if (!(words >> op)) continue;
        if (op != "check" || !(words >> key >> hex) || (words >> extra) ||
            hex.size() != kCheckBytes * 2 || rvas.count(key) == 0) {
            return "bad line " + line;
        }
        checks[key] = hex;
    }
    if (checks.size() < kOfflineMinChecks) return "too few functions checked";
    return "";
}

void read_cached_config(const std::wstring& folder, const char* build, OfflineFiles& files) {
    if (build == nullptr) return;
    std::string text;
    if (read_file(config_path(folder, build), text)) {
        std::map<std::string, uint64_t> rvas;
        std::string why = parse_offline_config(text, rvas, files.checks);
        if (why.empty()) {
            files.config = std::move(rvas);
        } else {
            files.checks.clear();
            files.error = "config: " + why;
        }
    } else if (DWORD e = GetLastError(); e != ERROR_FILE_NOT_FOUND) {
        files.error = "config: error " + std::to_string(e);
    }
}

std::string put_other_kind(const std::string& kind, const std::wstring& folder, const char* build,
                           const std::string& body, std::wstring& path) {
    if (kind != "config") return "error not config or seed";
    if (build == nullptr) return "error an unknown build";
    std::map<std::string, uint64_t> rvas;
    std::map<std::string, std::string> checks;
    std::string why = parse_offline_config(body, rvas, checks);
    if (!why.empty()) return "error " + why;
    path = config_path(folder, build);
    return "";
}

}  // namespace fs

std::string check_offline_cache(HMODULE image, const OfflineFiles& files) {
    if (files.config.empty()) return "no cached addresses";
    if (files.checks.size() < kOfflineMinChecks) return "too few functions checked";
    for (const auto& [key, hex] : files.checks) {
        auto it = files.config.find(key);
        if (it == files.config.end()) return key + " is checked but not configured";
        uint8_t want[fs::kCheckBytes], have[fs::kCheckBytes];
        if (!fs::hex_bytes(hex, want, sizeof(want))) return key + "'s check is not hex";
        const BYTE* at = reinterpret_cast<const BYTE*>(image) + it->second;
        if (!safe_read(at, have, sizeof(have))) return key + " cannot be read";
        if (std::memcmp(want, have, sizeof(want)) != 0) return key + " differs from the cache";
    }
    return "";
}

namespace {

// An absolute path: a drive's root or a share (a relative one would resolve against the game's
// own folder).
bool absolute_path(const std::wstring& path) {
    bool drive = path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
    bool share = path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\';
    return drive || share;
}

void trim_separators(std::wstring& path) {
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
}

// A known folder's path (Windows' own answer, not an environment variable a program may have
// changed), or "". Asked of shell32 at run time: the helper imports no more than it did.
std::wstring known_folder(const GUID& id) {
    using Get = HRESULT(WINAPI*)(const GUID&, DWORD, HANDLE, PWSTR*);
    using Free = void(WINAPI*)(void*);
    std::wstring path;
    HMODULE shell = LoadLibraryW(L"shell32.dll"), ole = LoadLibraryW(L"ole32.dll");
    if (shell != nullptr && ole != nullptr) {
        auto get = reinterpret_cast<Get>(GetProcAddress(shell, "SHGetKnownFolderPath"));
        auto release = reinterpret_cast<Free>(GetProcAddress(ole, "CoTaskMemFree"));
        PWSTR text = nullptr;
        if (get != nullptr && release != nullptr && SUCCEEDED(get(id, 0, nullptr, &text)) &&
            text != nullptr) {
            path = text;
        }
        if (release != nullptr && text != nullptr) release(text);
    }
    if (shell != nullptr) FreeLibrary(shell);
    if (ole != nullptr) FreeLibrary(ole);
    return path;
}

// {3EB685DB-65F9-4CF6-A03A-E3EF65729F3D}, {F1B32785-6FBA-4FCF-9D55-7B8E7F157091}
constexpr GUID kRoamingAppData = {0x3EB685DB, 0x65F9, 0x4CF6, {0xA0, 0x3A, 0xE3, 0xEF, 0x65, 0x72, 0x9F, 0x3D}};
constexpr GUID kLocalAppData = {0xF1B32785, 0x6FBA, 0x4FCF, {0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91}};

// The save folder under the Roaming AppData folder `roaming`, or "" for a path that is not
// absolute.
std::wstring save_folder_of(std::wstring roaming) {
    trim_separators(roaming);
    return absolute_path(roaming) ? roaming + L"\\Gallant Leaf\\Zexion" : L"";
}

// `path` with every link in it followed (a junction, a symbolic link, a `subst` drive: the final
// path of the longest part that exists, long names, no `\\?\` prefix), then what does not exist
// yet as it is, no trailing separator; "" for a path that is not absolute, has a `..` in it, or
// cannot be resolved. (A part made a link between this and the use is not caught: a test's
// folders are made by the test.)
std::wstring resolved_path(const std::wstring& path) {
    if (!absolute_path(path)) return L"";
    for (size_t at = 0; at < path.size();) {
        size_t end = path.find_first_of(L"\\/", at);
        if (end == std::wstring::npos) end = path.size();
        if (path.compare(at, end - at, L"..") == 0) return L"";
        at = end + 1;
    }
    wchar_t full[32768];
    DWORD n = GetFullPathNameW(path.c_str(), 32768, full, nullptr);
    if (n == 0 || n >= 32768) return L"";
    std::wstring rest(full, n), tail;
    trim_separators(rest);
    for (;;) {
        HANDLE h = CreateFileW(rest.c_str(), 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            wchar_t final_path[32768];
            DWORD m = GetFinalPathNameByHandleW(h, final_path, 32768, VOLUME_NAME_DOS);
            CloseHandle(h);
            if (m == 0 || m >= 32768) return L"";
            std::wstring out(final_path, m);
            if (out.rfind(L"\\\\?\\UNC\\", 0) == 0) {
                out = L"\\\\" + out.substr(8);
            } else if (out.rfind(L"\\\\?\\", 0) == 0) {
                out = out.substr(4);
            }
            trim_separators(out);
            return out + tail;
        }
        const size_t cut = rest.find_last_of(L'\\');
        if (cut == std::wstring::npos || cut < 3) return L"";
        tail = rest.substr(cut) + tail;
        rest.resize(cut);
    }
}

bool equal_nocase(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() &&
           CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

// Whether `path` is `base` or inside it (both resolved_path's), case apart.
bool at_or_under(const std::wstring& path, const std::wstring& base) {
    if (base.empty()) return false;
    if (equal_nocase(path, base)) return true;
    return path.size() > base.size() && path[base.size()] == L'\\' &&
           equal_nocase(path.substr(0, base.size()), base);
}

}  // namespace

std::wstring offline_folder() {
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    // An absolute path only (a drive's root or a share): a relative one would resolve against
    // the game's own folder.
    const std::wstring local(base, n);
    return absolute_path(local) ? local + L"\\ZexionAP" : L"";
}

std::wstring save_folder_under(const std::wstring& roaming) { return save_folder_of(roaming); }

std::wstring save_folder() {
    // Found once, at start (the helper asks then): SDL's own answer for the game (it makes the
    // folder, as the game's call does at start; here nothing is made) is the Roaming AppData
    // known folder, else %APPDATA%.
    static const std::wstring folder = [] {
        std::wstring roaming = known_folder(kRoamingAppData);
        if (roaming.empty()) {
            wchar_t base[MAX_PATH];
            DWORD n = GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH);
            if (n != 0 && n < MAX_PATH) roaming.assign(base, n);
        }
        return save_folder_of(roaming);
    }();
    return folder;
}

std::vector<std::wstring> os_save_folders() {
    return {save_folder_of(known_folder(kRoamingAppData))};
}

std::vector<std::wstring> os_offline_folders() {
    const std::wstring local = known_folder(kLocalAppData);
    return {absolute_path(local) ? local + L"\\ZexionAP" : L""};
}

std::wstring restrict_to_test_folder(const std::wstring& folder) {
    wchar_t temp[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, temp);
    if (n == 0 || n > MAX_PATH) return L"";
    const std::wstring base = resolved_path(std::wstring(temp, n)), path = resolved_path(folder);
    if (base.empty() || path.empty() || !at_or_under(path, base) || equal_nocase(path, base)) {
        return L"";
    }
    // Never the real ones: the folders Windows itself names for this user, not an environment
    // variable (a test sets LOCALAPPDATA to its own folder, which is what it is allowed to use).
    // One that cannot be named or resolved refuses everything.
    std::vector<std::wstring> reals = os_save_folders();
    for (std::wstring& real : os_offline_folders()) reals.push_back(std::move(real));
    for (const std::wstring& real : reals) {
        const std::wstring resolved = resolved_path(real);
        if (resolved.empty() || at_or_under(path, resolved)) return L"";
    }
    return path;
}
}  // namespace zap
