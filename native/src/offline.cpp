#include "offline.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <sstream>

#include "game_mem.h"
#include "requests.h"  // parse_config, parse_discs, parse_hex

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

std::string narrow(const std::wstring& w) {
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

std::string parse_offline_seed(const std::string& text, OfflineSeed& out) {
    out = OfflineSeed{};
    std::istringstream lines(text);
    std::string line;
    if (!next_line(lines, line) || line != "zexion-ap offline 1") return "not a seed file";
    bool table = false, start = false;
    while (next_line(lines, line)) {
        if (line == "ap_labels") {
            std::string labels = "ap_labels";
            while (next_line(lines, line)) {
                if (line.empty()) continue;
                labels += "\n" + line;
            }
            out.labels = labels;
            break;
        }
        std::istringstream words(line);
        std::string key, a;
        words >> key;
        if (key.empty()) continue;
        if (key == "table") {
            if (!(words >> a)) return "bad table";
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
            // "discs -", or as the `discs` request (its TOLD left out).
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
    std::memcpy(seed.table, section + kOfflineTableAt, static_cast<size_t>(seed.locations));
    std::memcpy(seed.start, section + kOfflineStartAt, sizeof(seed.start));
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

}  // namespace

std::string put_offline_file(const std::wstring& folder, const char* build,
                             const std::string& request, OfflineSeed* seed_out) {
    if (folder.empty()) return "error no LOCALAPPDATA";
    size_t eol = request.find('\n');
    if (eol == std::string::npos) return "error no file";
    std::string head = request.substr(0, eol), body = request.substr(eol + 1);
    if (body.size() > kMaxFile) return "error too large";
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
    CreateDirectoryW(folder.c_str(), nullptr);
    if (parsed) CreateDirectoryW((folder + L"\\seeds").c_str(), nullptr);
    bool changed = false;
    std::string why = write_whole(path, body, changed);
    if (!why.empty()) return "error " + why;
    if (parsed && seed_out != nullptr) *seed_out = std::move(*parsed);
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

}  // namespace zap
