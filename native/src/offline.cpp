#include "offline.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#include "game_calls.h"  // parse_config
#include "game_mem.h"

namespace zap {

namespace {

// A seed file is some 20 KB at most; the pipe drops a message over 64 KB, so the client sends
// none larger than this (client/offline.py, MAX_FILE).
constexpr size_t kMaxFile = 48 * 1024;
constexpr size_t kMaxSeeds = 256;  // the newest kept
constexpr size_t kCheckBytes = 16;

bool hex_bytes(const std::string& hex, uint8_t* out, size_t size) {
    if (hex.size() != size * 2) return false;
    for (size_t i = 0; i < size; ++i) {
        unsigned v = 0;
        for (int k = 0; k < 2; ++k) {
            char c = hex[i * 2 + k];
            unsigned d = c >= '0' && c <= '9'   ? static_cast<unsigned>(c - '0')
                         : c >= 'a' && c <= 'f' ? static_cast<unsigned>(c - 'a' + 10)
                         : c >= 'A' && c <= 'F' ? static_cast<unsigned>(c - 'A' + 10)
                                                : 16u;
            if (d > 15) return false;
            v = v * 16 + d;
        }
        out[i] = static_cast<uint8_t>(v);
    }
    return true;
}

bool hex_text(const std::string& hex, std::string& out) {
    if (hex.size() % 2 != 0 || hex.size() > 62) return false;  // one line of the game's text
    std::vector<uint8_t> raw(hex.size() / 2);
    if (!hex_bytes(hex, raw.data(), raw.size())) return false;
    out.assign(raw.begin(), raw.end());
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

// The cached config: the `config` request, then "check KEY HEX" lines, at least
// kOfflineMinChecks, each on a key of the config. "" or what is wrong.
std::string parse_offline_config(const std::string& text, std::map<std::string, uint64_t>& rvas,
                                 std::map<std::string, std::string>& checks) {
    rvas.clear();
    checks.clear();
    std::istringstream lines(text);
    std::string first, line;
    std::getline(lines, first);
    if (!first.empty() && first.back() == '\r') first.pop_back();
    std::string why = parse_config(first, rvas);
    if (!why.empty()) return why;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
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
    auto next = [&]() {
        if (!std::getline(lines, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    };
    if (!next() || line != "zexion-ap offline 1") return "not a seed file";
    bool table = false, start = false;
    while (next()) {
        if (line == "ap_labels") {
            std::string labels = "ap_labels";
            while (next()) {
                if (line.empty()) continue;
                labels += "\n" + line;
            }
            out.labels = labels;
            break;
        }
        std::istringstream words(line);
        std::string key, a, b, c;
        words >> key;
        if (key.empty()) continue;
        if (key == "table") {
            if (!(words >> a) || !hex_bytes(a, out.table, sizeof(out.table))) return "bad table";
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
            if (!(words >> a)) return "bad discs";
            if (a == "-") continue;
            if (!(words >> b >> c)) return "bad discs";
            try {
                out.count = std::stoi(a);
                out.required = std::stoi(b);
            } catch (...) {
                return "bad discs";
            }
            if (out.count < 0 || out.count > 255 || out.required < 1 || out.required > 255 ||
                !hex_text(c, out.scan_line) || out.scan_line.empty()) {
                return "bad discs";
            }
            out.discs = true;
        } else if (key == "unfound") {
            if (!(words >> a)) return "bad unfound";
            if (a == "-") continue;
            std::istringstream items(a);
            std::string item;
            while (std::getline(items, item, ',')) {
                if (item.empty() || item.size() > 4 ||
                    item.find_first_not_of("0123456789") != std::string::npos) {
                    return "bad unfound";
                }
                int cell = std::stoi(item);
                if (cell >= kMapCells) return "bad unfound";
                out.unfound.push_back(cell);
            }
        }
        // Other keys: from a later client, ignored.
    }
    if (!table || !start) return "no table or start";
    return "";
}

bool offline_matches(const OfflineSeed& seed, const uint8_t* table, const uint8_t* start) {
    return std::memcmp(seed.table, table, sizeof(seed.table)) == 0 &&
           std::memcmp(seed.start, start, sizeof(seed.start)) == 0;
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
                             const std::string& request) {
    if (folder.empty()) return "error no LOCALAPPDATA";
    size_t eol = request.find('\n');
    if (eol == std::string::npos) return "error no file";
    std::string head = request.substr(0, eol), body = request.substr(eol + 1);
    if (body.size() > kMaxFile) return "error too large";
    std::istringstream words(head);
    std::string op, kind, name, extra;
    words >> op >> kind;
    std::wstring path;
    if (op != "offline_put") return "error not an offline_put request";
    if (kind == "config") {
        if (build == nullptr) return "error an unknown build";
        // Checked as the reader will: the config, then its check lines.
        std::map<std::string, uint64_t> rvas;
        std::map<std::string, std::string> checks;
        std::string why = parse_offline_config(body, rvas, checks);
        if (!why.empty()) return "error " + why;
        path = folder + L"\\config-";
        for (const char* c = build; *c != '\0'; ++c) path += static_cast<wchar_t>(*c);
        path += L".txt";
    } else if (kind == "seed") {
        if (!(words >> name) || (words >> extra) || !good_seed_name(name)) {
            return "error bad name";
        }
        OfflineSeed seed;
        std::string why = parse_offline_seed(body, seed);
        if (!why.empty()) return "error " + why;
        CreateDirectoryW(folder.c_str(), nullptr);
        CreateDirectoryW((folder + L"\\seeds").c_str(), nullptr);
        path = folder + L"\\seeds\\";
        for (char c : name) path += static_cast<wchar_t>(c);
        path += L".txt";
    } else {
        return "error not config or seed";
    }
    CreateDirectoryW(folder.c_str(), nullptr);
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
        std::wstring name = folder + L"\\config-";
        for (const char* c = build; *c != '\0'; ++c) name += static_cast<wchar_t>(*c);
        name += L".txt";
        if (read_file(name, text)) {
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
