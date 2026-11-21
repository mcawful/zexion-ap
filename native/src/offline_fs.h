// What offline.cpp takes from the file system, one implementation a platform
// (offline_fs_win.cpp, offline_fs_posix.cpp): reading and writing a file whole, folders, listing,
// and the per-user folders. Paths are held as std::wstring on both (offline.h); a POSIX path is
// its bytes, one a character. Only offline.cpp and those two include this.
#pragma once

#include <algorithm>
#include <cstdint>
#include <istream>
#include <string>
#include <utility>
#include <vector>

#include "offline.h"
#include "requests.h"  // parse_hex

namespace zap::fs {

// Under the pipe's 64 KB message limit; the client's MAX_FILE (client/offline.py).
inline constexpr size_t kMaxFile = 48 * 1024;

// The reply for a missing folder, a platform's own (the client reads either: tests/test_cross_checks.py);
// kNoFolder is its reason alone.
#ifdef _WIN32
inline constexpr char kNoFolderError[] = "error no LOCALAPPDATA";
#else
inline constexpr char kNoFolderError[] = "error no folder";
#endif
inline constexpr const char* kNoFolder = kNoFolderError + 6;

// ASCII text (a build's name, a seed file's name: good_seed_name) as a path's characters, and a
// path's characters as ASCII text (anything else a `?`).
inline std::wstring widen(const std::string& text) {
    std::wstring out;
    for (char c : text) out += static_cast<wchar_t>(static_cast<unsigned char>(c));
    return out;
}
inline std::string narrow(const std::wstring& w) {
    std::string out;
    for (wchar_t c : w) out += c < 128 ? static_cast<char>(c) : '?';
    return out;
}

// Exactly `size` bytes, as hex (parse_hex).
inline bool hex_bytes(const std::string& hex, uint8_t* out, size_t size) {
    std::vector<uint8_t> raw;
    if (hex.size() != size * 2 || !parse_hex(hex, raw)) return false;
    std::copy(raw.begin(), raw.end(), out);
    return true;
}

// The next line of `lines`, its \r dropped; false at the end.
inline bool next_line(std::istream& lines, std::string& line) {
    if (!std::getline(lines, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

// A listing's order: newest first, and by name within a time.
inline void sort_newest_first(std::vector<std::pair<uint64_t, std::string>>& names) {
    std::sort(names.begin(), names.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
}

// The write primitives (write_whole, make_folder) ask this first. In the release build it is
// always so. In the test-only build of the core (ZAP_TEST_BUILD, set by CMake for the test targets
// alone: every test program and the test builds of the helpers link it, the release never does) a
// write is allowed only inside a test's own folder: strictly inside the temporary folder and never
// a real one (restrict_to_test_folder), so even a unit test that is handed the real save folder
// writes nothing there, instead of the canary noticing afterwards.
#ifdef ZAP_TEST_BUILD
inline bool folder_allowed(const std::wstring& folder) {
    return !restrict_to_test_folder(folder).empty();
}
inline bool write_allowed(const std::wstring& path) {
    const size_t cut = path.find_last_of(L"\\/");
    return cut != std::wstring::npos && folder_allowed(path.substr(0, cut));
}
#else
constexpr bool folder_allowed(const std::wstring&) { return true; }
constexpr bool write_allowed(const std::wstring&) { return true; }
#endif

// The file `path` read whole into `out`: false if it is missing, not a plain file, not readable
// (opened with no sharing by another program) or past kMaxFile.
bool read_file(const std::wstring& path, std::string& out);

// The file's text written in place of `path`'s, through a temporary file (whole or not at all, on
// disk before it replaces the old): "" or what failed. `changed` says whether the text differs
// from what was there (nothing is written when it does not).
std::string write_whole(const std::wstring& path, const std::string& text, bool& changed);

// A folder made if it is not there yet (on POSIX with any of its parents missing, each only for
// the user, mode 0700: a new user's ~/.local/share), if the write guard allows it.
void make_folder(const std::wstring& folder);

// `folder`, the platform's separator, `name`.
std::wstring join(const std::wstring& folder, const std::string& name);

bool folder_exists(const std::wstring& path);
// Whether anything is at `path` (on POSIX a link itself too), whatever it holds.
bool exists(const std::wstring& path);

// The plain files in `folder` whose names (ASCII only) begin with `prefix` and end with `suffix`,
// longer than the two, as (last written, name), newest first and by name within a time. A folder
// that is not there lists none; one that cannot be listed says why in `error` (when given).
std::vector<std::pair<uint64_t, std::string>> list_files(const std::wstring& folder,
                                                         const std::string& prefix,
                                                         const std::string& suffix,
                                                         std::string* error = nullptr);

// The address cache's file and checks for `build` in `folder`, which only the Windows helper
// keeps (the Linux helper configures itself from the game's symbols): read into `files` (the
// config and its checks, or `files.error`); nothing on Linux.
void read_cached_config(const std::wstring& folder, const char* build, OfflineFiles& files);

// An `offline_put` of kind `kind` that is not a seed's: what to answer ("error ..."), or "" and
// `path` the file to write for a config (Windows, a known `build`, `body` a cache that parses).
std::string put_other_kind(const std::string& kind, const std::wstring& folder, const char* build,
                           const std::string& body, std::wstring& path);

}  // namespace zap::fs
