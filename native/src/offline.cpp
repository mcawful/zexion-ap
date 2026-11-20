#include "offline.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <sstream>

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#endif

#include "game_mem.h"
#include "requests.h"  // parse_config, parse_discs, parse_hex
#include "trap.h"      // trap::is_trap

namespace zap {

namespace {

// Under the pipe's 64 KB message limit; the client's MAX_FILE (client/offline.py).
constexpr size_t kMaxFile = 48 * 1024;
constexpr size_t kMaxSeeds = 256;  // the newest kept
constexpr size_t kCheckBytes = 16;

// Exactly `size` bytes, as hex (parse_hex).
bool hex_bytes(const std::string& hex, uint8_t* out, size_t size) {
    std::vector<uint8_t> raw;
    if (hex.size() != size * 2 || !parse_hex(hex, raw)) return false;
    std::memcpy(out, raw.data(), size);
    return true;
}

#ifdef _WIN32
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
#else
// A path is held as its bytes, one a character (getenv's as they are, and the ASCII names the
// helper adds), so it goes back to the same bytes whatever the user's folder is named.
std::string path_bytes(const std::wstring& path) {
    std::string out;
    for (wchar_t c : path) out += static_cast<char>(static_cast<unsigned char>(c));
    return out;
}

std::wstring bytes_path(const char* bytes) {
    std::wstring out;
    for (; *bytes != '\0'; ++bytes) out += static_cast<wchar_t>(static_cast<unsigned char>(*bytes));
    return out;
}

bool read_file(const std::wstring& path, std::string& out) {
    // Not through a link, and never blocking on what is not a file (a FIFO swapped in).
    int fd = open(path_bytes(path).c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st {};
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 &&
              static_cast<uint64_t>(st.st_size) <= kMaxFile;
    if (ok) {
        out.resize(static_cast<size_t>(st.st_size));
        size_t got = 0;
        while (ok && got < out.size()) {
            ssize_t n = ::read(fd, out.data() + got, out.size() - got);
            if (n < 0 && errno == EINTR) continue;
            ok = n > 0;
            if (ok) got += static_cast<size_t>(n);
        }
    }
    close(fd);
    return ok;
}
#endif

[[maybe_unused]] std::string narrow(const std::wstring& w) {
    std::string out;
    for (wchar_t c : w) out += c < 128 ? static_cast<char>(c) : '?';
    return out;
}

// ASCII text (a build's name, a seed file's: good_seed_name) as a path's characters.
std::wstring widen(const std::string& text) { return std::wstring(text.begin(), text.end()); }

// The next line of `lines`, its \r dropped; false at the end.
bool next_line(std::istream& lines, std::string& line) {
    if (!std::getline(lines, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

// The cached config's file for `build` in `folder`.
[[maybe_unused]] std::wstring config_path(const std::wstring& folder, const char* build) {
    return folder + L"\\config-" + widen(build) + L".txt";
}

// The cached config: the `config` request, then "check KEY HEX" lines, at least
// kOfflineMinChecks, each on a key of the config. "" or what is wrong.
[[maybe_unused]] std::string parse_offline_config(const std::string& text, std::map<std::string, uint64_t>& rvas,
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

// A seed file's name as the client makes it (client/offline.py): letters, digits, dashes and
// underscores, ending in -TEAM-SLOT (which also keeps out the device names CON, COM1 and such).
bool good_seed_name(const std::string& name) {
    if (name.empty() || name.size() > 80 ||
        name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                               "0123456789-_") != std::string::npos) {
        return false;
    }
    size_t slot = name.rfind('-');
    if (slot == std::string::npos || slot == 0 || slot + 1 == name.size()) return false;
    size_t team = name.rfind('-', slot - 1);
    if (team == std::string::npos || team == 0 || team + 1 == slot) return false;
    auto digits = [&](size_t from, size_t to) {
        for (size_t i = from; i < to; ++i) {
            if (name[i] < '0' || name[i] > '9') return false;
        }
        return true;
    };
    return digits(team + 1, slot) && digits(slot + 1, name.size());
}

}  // namespace

namespace {

// "I:T,J:T,...": each a map grid index and a trap's item (trap.h), no cell twice.
bool parse_own_traps(const std::string& text, std::vector<OfflineSeed::OwnTrap>& out) {
    std::istringstream entries(text);
    std::string entry;
    while (std::getline(entries, entry, ',')) {
        const size_t colon = entry.find(':');
        if (colon == std::string::npos) return false;
        std::vector<int> cell, item;
        if (!parse_number_list(entry.substr(0, colon), 4, 0, kMapCells - 1, cell) ||
            !parse_number_list(entry.substr(colon + 1), 3, 0, 255, item) || cell.size() != 1 ||
            item.size() != 1 || !trap::is_trap(item[0])) {
            return false;
        }
        for (const OfflineSeed::OwnTrap& t : out) {
            if (t.cell == cell[0]) return false;
        }
        out.push_back({cell[0], static_cast<uint8_t>(item[0])});
    }
    return !out.empty() && text.back() != ',';
}

}  // namespace

std::string parse_offline_seed(const std::string& text, OfflineSeed& out) {
    out = OfflineSeed{};
    std::istringstream lines(text);
    std::string line;
    if (!next_line(lines, line) || line != "zexion-ap offline 1") return "not a seed file";
    bool table = false, start = false;
    while (next_line(lines, line)) {
        if (line == "ap_labels") {
            out.labels = line;
            while (next_line(lines, line)) {
                if (!line.empty()) out.labels += "\n" + line;
            }
            break;
        }
        std::istringstream words(line);
        std::string key, a;
        words >> key;
        if (key.empty()) continue;
        if (key == "table") {
            // Only one: a second, shorter one would leave the first's 155th entry past
            // `locations`, which must be 0 (offline_matches compares the whole table).
            if (table || !(words >> a)) return "bad table";
            out.locations = static_cast<int>(a.size() / 2);
            if (out.locations != rando::kLocations && out.locations != rando::kMaxLocations) {
                return "bad table";
            }
            if (!hex_bytes(a, out.table, static_cast<size_t>(out.locations))) return "bad table";
            table = true;
        } else if (key == "start") {
            if (!(words >> a) || !hex_bytes(a, out.start, sizeof(out.start))) return "bad start";
            start = true;
        } else if (key == "intel") {
            if (!(words >> a) || (a != "0" && a != "1")) return "bad intel";
            out.intel = a == "1";
        } else if (key == "assists") {
            if (!(words >> a) || (a != "lock" && a != "free")) return "bad assists";
            out.lock_assists = a == "lock";
        } else if (key == "discs") {
            if (words >> a && a == "-") continue;
            bool told = false;
            if (!parse_discs(line, out.count, out.required, out.scan_line, told).empty() ||
                out.scan_line.empty()) {
                return "bad discs";
            }
            out.discs = true;
        } else if (key == "unfound") {
            if (!(words >> a)) return "bad unfound";
            if (a != "-" && !parse_number_list(a, 4, 0, kMapCells - 1, out.unfound)) {
                return "bad unfound";
            }
        } else if (key == "traps") {
            if (!(words >> a)) return "bad traps";
            if (a != "-" && !parse_own_traps(a, out.traps)) return "bad traps";
        }
        // Unknown keys are ignored (a newer client).
    }
    if (!table || !start) return "no table or start";
    return "";
}

bool offline_matches(const OfflineSeed& seed, const OfflineSeed& other) {
    return seed.locations == other.locations &&
           std::memcmp(seed.table, other.table, sizeof(seed.table)) == 0 &&
           std::memcmp(seed.start, other.start, sizeof(seed.start)) == 0;
}

OfflineSeed offline_seed_of(const uint8_t* section) {
    OfflineSeed seed;
    seed.locations = rando::locations(section);
    std::memcpy(seed.table, section + rando::kItemTable, static_cast<size_t>(seed.locations));
    std::memcpy(seed.start, section + rando::kStart, sizeof(seed.start));
    return seed;
}

int offline_disk_count(const OfflineSeed& seed, const uint8_t* grid) {
    int count = seed.count;
    for (int cell : seed.unfound) {
        if (cell >= 0 && cell < kMapCells && grid[cell] == kCollectedCell) ++count;
    }
    return count;
}

namespace {

#ifdef _WIN32
// The file's text written in place of `path`'s, through a temporary file: "" or what failed.
std::string write_whole(const std::wstring& path, const std::string& text, bool& changed) {
    std::string old;
    changed = !(read_file(path, old) && old == text);
    if (!changed) return "";
    std::wstring temp = path + L".tmp";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return "cannot create, error " + std::to_string(GetLastError());
    DWORD written = 0;
    bool ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
              written == text.size();
    CloseHandle(f);
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DWORD e = GetLastError();
        DeleteFileW(temp.c_str());
        return "cannot write, error " + std::to_string(e);
    }
    return "";
}

#else
// The file's text written in place of `path`'s, through a temporary file renamed over it (whole
// or not at all, as on Windows): "" or what failed.
std::string write_whole(const std::wstring& path, const std::string& text, bool& changed) {
    std::string old;
    changed = !(read_file(path, old) && old == text);
    if (!changed) return "";
    const std::string target = path_bytes(path), temp = target + ".tmp";
    unlink(temp.c_str());  // a stale one (or a link planted there) gone, then made afresh
    int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return "cannot create, error " + std::to_string(errno);
    size_t done = 0;
    bool ok = true;
    while (ok && done < text.size()) {
        ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n < 0 && errno == EINTR) continue;
        ok = n > 0;
        if (ok) done += static_cast<size_t>(n);
    }
    int e = ok ? 0 : errno;
    if (ok && fsync(fd) != 0) {  // on disk before it replaces the old one
        ok = false;
        e = errno;
    }
    if (close(fd) != 0 && ok) {
        ok = false;
        e = errno;
    }
    if (!ok || rename(temp.c_str(), target.c_str()) != 0) {
        if (ok) e = errno;
        unlink(temp.c_str());
        return "cannot write, error " + std::to_string(e);
    }
    return "";
}

// A folder made if it is not there yet, with any of its parents missing (a new user's
// ~/.local/share), each only for the user (mode 0700, as the XDG spec has them made).
void make_folder(const std::wstring& folder) {
    const std::string path = path_bytes(folder);
    for (size_t slash = path.find('/', 1); slash != std::string::npos;
         slash = path.find('/', slash + 1)) {
        mkdir(path.substr(0, slash).c_str(), 0700);  // there already: EEXIST, left as it is
    }
    mkdir(path.c_str(), 0700);
}
#endif
}  // namespace

#ifdef _WIN32
std::string put_offline_file(const std::wstring& folder, const char* build,
                             const std::string& request, OfflineSeed* seed_out) {
    size_t eol = request.find('\n');
    if (eol == std::string::npos) return "error no file";
    std::string head = request.substr(0, eol), body = request.substr(eol + 1);
    std::istringstream words(head);
    std::string op, kind, name, extra;
    std::optional<OfflineSeed> parsed;  // a seed's file, once checked
    words >> op >> kind;
    std::wstring path;
    if (op != "offline_put") return "error not an offline_put request";
    if (kind == "config") {
        if (build == nullptr) return "error an unknown build";
        std::map<std::string, uint64_t> rvas;
        std::map<std::string, std::string> checks;
        std::string why = parse_offline_config(body, rvas, checks);
        if (!why.empty()) return "error " + why;
        path = config_path(folder, build);
    } else if (kind == "seed") {
        if (!(words >> name) || (words >> extra) || !good_seed_name(name)) {
            return "error bad name";
        }
        OfflineSeed seed;
        std::string why = parse_offline_seed(body, seed);
        if (!why.empty()) return "error " + why;
        seed.name = name;
        parsed = std::move(seed);
        path = folder + L"\\seeds\\" + widen(name) + L".txt";
    } else {
        return "error not config or seed";
    }
    // The seed is the running helper's whether the file can be kept or not (no folder, a
    // file too large to read back, a write that fails): its own traps and its saves' disk
    // counts come from it with a client too (Game::watch_own_traps). So it is handed over
    // before the checks that concern the file alone. The pipe takes no request much over its
    // 64 KB buffer, which bounds what is parsed here.
    if (parsed && seed_out != nullptr) *seed_out = std::move(*parsed);
    if (folder.empty()) return "error no LOCALAPPDATA";
    if (body.size() > kMaxFile) return "error too large";
    CreateDirectoryW(folder.c_str(), nullptr);
    if (parsed) CreateDirectoryW((folder + L"\\seeds").c_str(), nullptr);
    bool changed = false;
    std::string why = write_whole(path, body, changed);
    if (!why.empty()) return "error " + why;
    return changed ? "stored" : "unchanged";
}

std::string check_offline_cache(HMODULE image, const OfflineFiles& files) {
    if (files.config.empty()) return "no cached addresses";
    if (files.checks.size() < kOfflineMinChecks) return "too few functions checked";
    for (const auto& [key, hex] : files.checks) {
        auto it = files.config.find(key);
        if (it == files.config.end()) return key + " is checked but not configured";
        uint8_t want[kCheckBytes], have[kCheckBytes];
        if (!hex_bytes(hex, want, sizeof(want))) return key + "'s check is not hex";
        const BYTE* at = reinterpret_cast<const BYTE*>(image) + it->second;
        if (!safe_read(at, have, sizeof(have))) return key + " cannot be read";
        if (std::memcmp(want, have, sizeof(want)) != 0) return key + " differs from the cache";
    }
    return "";
}

std::wstring offline_folder() {
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    // An absolute path only (a drive's root or a share): a relative one would resolve against
    // the game's own folder.
    bool drive = n >= 3 && base[1] == L':' && (base[2] == L'\\' || base[2] == L'/');
    bool share = n >= 2 && base[0] == L'\\' && base[1] == L'\\';
    if (!drive && !share) return L"";
    return std::wstring(base, n) + L"\\ZexionAP";
}

OfflineFiles read_offline_files(const std::wstring& folder, const char* build) {
    OfflineFiles files;
    if (folder.empty()) {
        files.error = "no LOCALAPPDATA";
        return files;
    }
    std::string text;
    if (build != nullptr) {
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
    WIN32_FIND_DATAW found{};
    HANDLE find = FindFirstFileW((folder + L"\\seeds\\*.txt").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND) {
            files.error = "seeds: error " + std::to_string(e) + " in " + narrow(folder);
        }
        return files;
    }
    std::vector<std::pair<uint64_t, std::wstring>> names;  // last written, name
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        uint64_t written = (static_cast<uint64_t>(found.ftLastWriteTime.dwHighDateTime) << 32) |
                           found.ftLastWriteTime.dwLowDateTime;
        names.emplace_back(written, found.cFileName);
    } while (FindNextFileW(find, &found));
    FindClose(find);
    std::sort(names.begin(), names.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    for (const auto& [written, file] : names) {
        if (files.seeds.size() >= kMaxSeeds) break;
        OfflineSeed seed;
        std::string why =
            read_file(folder + L"\\seeds\\" + file, text) ? parse_offline_seed(text, seed)
                                                           : "unreadable";
        if (!why.empty()) {
            files.error = narrow(file) + ": " + why;
            continue;
        }
        seed.name = narrow(file);
        files.seeds.push_back(std::move(seed));
    }
    return files;
}

#else
// The Linux build keeps the seeds' files as Windows does, in its own per-user folder
// (offline_folder), and no config: the Linux helper configures itself from the game's symbols.
std::string put_offline_file(const std::wstring& folder, const char*, const std::string& request,
                             OfflineSeed* seed_out) {
    size_t eol = request.find('\n');
    if (eol == std::string::npos) return "error no file";
    std::istringstream words(request.substr(0, eol));
    std::string op, kind, name, extra;
    words >> op >> kind;
    if (op != "offline_put") return "error not an offline_put request";
    if (kind != "seed") return "error not on the Linux build";
    if (!(words >> name) || (words >> extra) || !good_seed_name(name)) return "error bad name";
    const std::string body = request.substr(eol + 1);
    OfflineSeed seed;
    std::string why = parse_offline_seed(body, seed);
    if (!why.empty()) return "error " + why;
    seed.name = name;
    // The running helper's whether the file can be kept or not, as on Windows.
    if (seed_out != nullptr) *seed_out = std::move(seed);
    if (folder.empty()) return "error no folder";
    if (body.size() > kMaxFile) return "error too large";
    make_folder(folder);
    make_folder(folder + L"/seeds");
    bool changed = false;
    why = write_whole(folder + L"/seeds/" + widen(name) + L".txt", body, changed);
    if (!why.empty()) return "error " + why;
    return changed ? "stored" : "unchanged";
}

std::string check_offline_cache(HMODULE, const OfflineFiles&) { return "not on the Linux build"; }

std::wstring offline_folder() {
    // As the game's own saves: $XDG_DATA_HOME, else ~/.local/share, an absolute path only (a
    // relative one would resolve against the game's folder). Under Flatpak's Steam, the
    // sandbox's.
    const char* data = std::getenv("XDG_DATA_HOME");
    if (data != nullptr && data[0] == '/') return bytes_path(data) + L"/ZexionAP";
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] == '/') return bytes_path(home) + L"/.local/share/ZexionAP";
    return L"";
}

OfflineFiles read_offline_files(const std::wstring& folder, const char*) {
    OfflineFiles files;
    if (folder.empty()) {
        files.error = "no folder";
        return files;
    }
    const std::wstring seeds = folder + L"/seeds/";
    DIR* dir = opendir(path_bytes(seeds).c_str());
    if (dir == nullptr) {
        if (errno != ENOENT) files.error = "seeds: error " + std::to_string(errno);
        return files;
    }
    std::vector<std::pair<int64_t, std::string>> names;  // last written (ns), name
    while (const dirent* entry = readdir(dir)) {
        std::string file = entry->d_name;
        if (file.size() <= 4 || file.compare(file.size() - 4, 4, ".txt") != 0) continue;
        struct stat st {};
        if (stat((path_bytes(seeds) + file).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        names.emplace_back(static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000 +
                               st.st_mtim.tv_nsec,
                           file);
    }
    closedir(dir);
    std::sort(names.begin(), names.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    std::string text;
    for (const auto& [written, file] : names) {
        if (files.seeds.size() >= kMaxSeeds) break;
        OfflineSeed seed;
        std::string why =
            read_file(seeds + bytes_path(file.c_str()), text) ? parse_offline_seed(text, seed)
                                                               : "unreadable";
        if (!why.empty()) {
            files.error = file + ": " + why;
            continue;
        }
        seed.name = file;
        files.seeds.push_back(std::move(seed));
    }
    return files;
}
#endif

}  // namespace zap
