#include "offline.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <set>
#include <sstream>

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
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

// The first `size` bytes of the file at `path` (a save is larger than kMaxFile): false if it is
// shorter or cannot be read.
bool read_prefix(const std::wstring& path, size_t size, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                                                           FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    out.resize(size);
    DWORD got = 0;
    bool ok = ReadFile(f, out.data(), static_cast<DWORD>(size), &got, nullptr) && got == size;
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

// The first `size` bytes of the file at `path` (a save is larger than kMaxFile): false if it is
// shorter or cannot be read. Not through a link, as read_file.
bool read_prefix(const std::wstring& path, size_t size, std::string& out) {
    int fd = open(path_bytes(path).c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st {};
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && static_cast<uint64_t>(st.st_size) >= size;
    if (ok) {
        out.resize(size);
        size_t got = 0;
        while (ok && got < size) {
            ssize_t n = ::read(fd, out.data() + got, size - got);
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

std::string lower_hex(const uint8_t* data, size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}

// The CRC-32 (the zlib one: reflected, polynomial 0xEDB88320) of `data`.
uint32_t crc32_of(const std::string& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : data) {
        crc ^= c;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

std::string crc_line(const std::string& body) {
    char text[16];
    std::snprintf(text, sizeof text, "crc %08X\n", static_cast<unsigned>(crc32_of(body)));
    return text;
}

// How many "label X Y HEX" lines the labels block holds.
size_t label_lines(const std::string& labels) {
    size_t count = 0;
    for (size_t at = labels.find("\nlabel "); at != std::string::npos;
         at = labels.find("\nlabel ", at + 1)) {
        ++count;
    }
    return count;
}

}  // namespace

std::string format_offline_seed(const OfflineSeed& seed) {
    std::string text = "zexion-ap offline 1\n";
    text += "table " + lower_hex(seed.table, static_cast<size_t>(seed.locations)) + "\n";
    text += "start " + lower_hex(seed.start, sizeof(seed.start)) + "\n";
    text += std::string("intel ") + (seed.intel ? "1" : "0") + "\n";
    text += std::string("assists ") + (seed.lock_assists ? "lock" : "free") + "\n";
    if (seed.discs) {
        text += "discs " + std::to_string(seed.count) + " " + std::to_string(seed.required) + " " +
                lower_hex(reinterpret_cast<const uint8_t*>(seed.scan_line.data()),
                          seed.scan_line.size()) +
                "\n";
    } else {
        text += "discs -\n";
    }
    text += "unfound ";
    if (seed.unfound.empty()) text += "-";
    for (size_t i = 0; i < seed.unfound.size(); ++i) {
        text += (i == 0 ? "" : ",") + std::to_string(seed.unfound[i]);
    }
    text += "\ntraps ";
    if (seed.traps.empty()) text += "-";
    for (size_t i = 0; i < seed.traps.size(); ++i) {
        text += (i == 0 ? "" : ",") + std::to_string(seed.traps[i].cell) + ":" +
                std::to_string(seed.traps[i].item);
    }
    text += "\n";
    if (!seed.labels.empty()) text += seed.labels + "\n";
    return text;
}

std::string synced_file_text(const OfflineSeed& seed) {
    const std::string body = format_offline_seed(seed);
    return body + crc_line(body);
}

std::string parse_synced_file(const std::string& text, OfflineSeed& out) {
    out = OfflineSeed{};
    if (text.size() < 2 || text.back() != '\n') return "no crc line";
    const size_t cut = text.rfind('\n', text.size() - 2);
    if (cut == std::string::npos) return "no crc line";
    const std::string body = text.substr(0, cut + 1);
    if (text.substr(cut + 1) != crc_line(body)) return "crc line missing or wrong";
    return parse_offline_seed(body, out);
}

OfflineSeed merge_offline_seeds(const OfflineSeed& older, const OfflineSeed& newer) {
    OfflineSeed merged = newer;
    merged.written = std::max(older.written, newer.written);
    // Assist Unlock is never taken back: the lock stands only if both copies still lock.
    merged.lock_assists = older.lock_assists && newer.lock_assists;
    // The disk count only grows: the larger, with the cells and the line it came with.
    if (older.discs && (!newer.discs || older.count > newer.count)) {
        merged.discs = true;
        merged.count = older.count;
        merged.required = older.required;
        merged.scan_line = older.scan_line;
        merged.unfound = older.unfound;
    }
    if (label_lines(older.labels) > label_lines(newer.labels)) merged.labels = older.labels;
    if (newer.traps.empty()) merged.traps = older.traps;
    return merged;
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

bool folder_exists(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// Whether anything is at `path`, whatever it holds.
bool exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// The files in `folder` that begin with kSyncedPrefix and end with kSyncedSuffix, as (last
// written, name), unordered.
std::vector<std::pair<uint64_t, std::string>> list_synced(const std::wstring& folder) {
    std::vector<std::pair<uint64_t, std::string>> names;
    WIN32_FIND_DATAW found{};
    HANDLE find = FindFirstFileW(
        (folder + L"\\" + widen(kSyncedPrefix) + L"*" + widen(kSyncedSuffix)).c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) return names;
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        uint64_t written = (static_cast<uint64_t>(found.ftLastWriteTime.dwHighDateTime) << 32) |
                           found.ftLastWriteTime.dwLowDateTime;
        names.emplace_back(written, narrow(found.cFileName));
    } while (FindNextFileW(find, &found));
    FindClose(find);
    return names;
}

// The local seed file of `base` (NAME), written whole in the helper's own folder, made if need
// be: "" or what failed.
std::string write_local_seed(const std::wstring& folder, const std::string& base,
                             const std::string& text) {
    if (folder.empty()) return "no LOCALAPPDATA";
    CreateDirectoryW(folder.c_str(), nullptr);
    CreateDirectoryW((folder + L"\\seeds").c_str(), nullptr);
    bool changed = false;
    return write_whole(folder + L"\\seeds\\" + widen(base) + L".txt", text, changed);
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

std::wstring join(const std::wstring& folder, const std::string& name) {
    return folder + L"/" + widen(name);
}

bool folder_exists(const std::wstring& path) {
    struct stat st {};
    return stat(path_bytes(path).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Whether anything is at `path` (a link itself too), whatever it holds.
bool exists(const std::wstring& path) {
    struct stat st {};
    return lstat(path_bytes(path).c_str(), &st) == 0;
}

// The files in `folder` that begin with kSyncedPrefix and end with kSyncedSuffix, as (last
// written, name), unordered.
std::vector<std::pair<uint64_t, std::string>> list_synced(const std::wstring& folder) {
    std::vector<std::pair<uint64_t, std::string>> names;
    DIR* dir = opendir(path_bytes(folder).c_str());
    if (dir == nullptr) return names;
    const std::string prefix = kSyncedPrefix, suffix = kSyncedSuffix;
    while (const dirent* entry = readdir(dir)) {
        const std::string file = entry->d_name;
        if (file.size() <= prefix.size() + suffix.size() || file.compare(0, prefix.size(), prefix) != 0 ||
            file.compare(file.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        struct stat st {};
        if (stat((path_bytes(folder) + "/" + file).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        names.emplace_back(static_cast<uint64_t>(st.st_mtim.tv_sec) * 1000000000ull +
                               static_cast<uint64_t>(st.st_mtim.tv_nsec),
                           file);
    }
    closedir(dir);
    return names;
}

// The local seed file of `base` (NAME), written whole in the helper's own folder, made if need
// be: "" or what failed.
std::string write_local_seed(const std::wstring& folder, const std::string& base,
                             const std::string& text) {
    if (folder.empty()) return "no folder";
    make_folder(folder);
    make_folder(folder + L"/seeds");
    bool changed = false;
    return write_whole(folder + L"/seeds/" + widen(base) + L".txt", text, changed);
}
#endif
}  // namespace

namespace {

// A seed's NAME from its local file's name ("NAME.txt"), or as it is.
std::string base_name(const std::string& file) {
    return file.size() > 4 && file.compare(file.size() - 4, 4, ".txt") == 0
               ? file.substr(0, file.size() - 4)
               : file;
}

std::string synced_file_name(const std::string& base) {
    return std::string(kSyncedPrefix) + base + kSyncedSuffix;
}

// "" if seed `base`'s copy in the save folder `save` read right (into `out`), else why not.
std::string read_synced_copy(const std::wstring& save, const std::string& base,
                             OfflineSeed& out) {
    std::string text;
    if (!read_file(join(save, synced_file_name(base)), text)) return "unreadable";
    return parse_synced_file(text, out);
}

// The save file's offsets (docs/re/save-format.md; client/save_patch.py): the randomizer section
// starts at 0x12814 (so rando::kItemTable at 0x12A80, rando::kStart at 0x12B24), and the slot
// block, which the file holds from its offset 2, has the empty mark and the randomizer marker
// (slot::kEmpty, slot::kRandoMarker).
constexpr size_t kSaveSection = 0x12814;
constexpr size_t kSavePrefix = kSaveSection + rando::kSettings + rando::kSecretShuffle + 1;

// The seeds of the randomizer saves among the 30 slots in the save folder `save`, read only (the
// first bytes of each `Save Game N.sav`, and none of an empty slot, a normal game or a file that
// is not there): the match the file select makes (offline_seed_of, offline_matches).
std::vector<OfflineSeed> saved_seeds(const std::wstring& save) {
    std::vector<OfflineSeed> seeds;
    std::string data;
    for (int n = 1; n <= slot::kCount; ++n) {
        if (!read_prefix(join(save, "Save Game " + std::to_string(n) + ".sav"), kSavePrefix,
                         data)) {
            continue;
        }
        const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());
        const bool marked = bytes[slot::kRandoMarker + 2] == 1 && bytes[slot::kRandoMarker + 3] == 0;
        if (!marked || bytes[slot::kEmpty + 2] == slot::kEmptyMark) continue;
        seeds.push_back(offline_seed_of(bytes + kSaveSection));
    }
    return seeds;
}

// Whether a save of `seed` is among `saved` (saved_seeds).
bool in_use(const std::vector<OfflineSeed>& saved, const OfflineSeed& seed) {
    return std::any_of(saved.begin(), saved.end(),
                       [&](const OfflineSeed& s) { return offline_matches(s, seed); });
}

// `seed`'s copy written into the save folder, under its own name only, whole or not at all: ""
// or what failed. Nothing, and no error, where there is no save folder (a game with no saves yet:
// the helper never makes the game's folder). Only the caller decides whether a save uses it.
std::string write_synced_copy(const std::wstring& save, const std::string& base,
                              const OfflineSeed& seed) {
    if (save.empty() || !folder_exists(save)) return "";
    const std::string text = synced_file_text(seed);
    if (text.size() > kMaxFile) return "too large";
    bool changed = false;
    return write_whole(join(save, synced_file_name(base)), text, changed);
}

// A put seed's copy: written only while a save of the seed is in the save folder; else it waits
// for a read (join_saves), once the game has saved it. "" or what failed.
std::string put_synced_copy(const std::wstring& save, const std::string& base,
                            const OfflineSeed& seed) {
    if (save.empty() || !folder_exists(save) || !in_use(saved_seeds(save), seed)) return "";
    return write_synced_copy(save, base, seed);
}

// A seed put by a client, made one with the copy already in the save folder if that is the
// same seed (the put one the newer): `seed` and `body`, its file's text, become the result's
// when that differs from what was put.
void join_synced(const std::wstring& save, OfflineSeed& seed, std::string& body) {
    if (save.empty()) return;
    OfflineSeed theirs;
    if (!read_synced_copy(save, seed.name, theirs).empty() || !offline_matches(theirs, seed)) {
        return;
    }
    OfflineSeed merged = merge_offline_seeds(theirs, seed);
    merged.name = seed.name;
    const std::string text = format_offline_seed(merged);
    if (text == format_offline_seed(seed)) return;
    body = text;
    seed = std::move(merged);
}

// The seeds read from the helper's own folder (`files.seeds`, each named NAME.txt), made one with
// those in the save folder: the newer copy's fields, with merge_offline_seeds' exceptions; each
// copy that then differs from the result is written again, and a seed with no copy in one of the
// folders gets it. Copies in the save folder are written only for a seed a save in that folder
// uses (saved_seeds): a seed none uses keeps the copy it has, not refreshed, and gets none. A copy
// that does not read is not trusted, and is replaced by a good local one (a half-synced file), else
// left alone. With `write` false nothing is written anywhere: the seeds are only made one.
void join_saves(OfflineFiles& files, const std::wstring& folder, const std::wstring& save,
                bool write) {
    if (save.empty() || !folder_exists(save)) return;
    std::vector<OfflineSeed> saved;
    bool saved_read = false;
    auto used = [&](const OfflineSeed& seed) {
        if (!saved_read) saved = saved_seeds(save);
        saved_read = true;
        return in_use(saved, seed);
    };
    // The local file, written whole, but never over one that is there and was not read (a file
    // past the 256 read, or one that does not parse: a copy must not replace what it has not seen).
    auto write_local = [&](const std::string& base, const std::string& text, bool was_read) {
        if (!write || folder.empty()) return std::string();
        if (!was_read && exists(join(join(folder, "seeds"), base + ".txt"))) return std::string();
        return write_local_seed(folder, base, text);
    };
    std::vector<std::pair<uint64_t, std::string>> names = list_synced(save);
    std::sort(names.begin(), names.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    std::map<std::string, size_t> local;  // NAME -> its index in files.seeds
    for (size_t i = 0; i < files.seeds.size(); ++i) local[base_name(files.seeds[i].name)] = i;
    const size_t local_count = files.seeds.size();
    std::set<std::string> there;  // NAMEs whose copy in the save folder is as it should be
    std::vector<OfflineSeed> added;
    std::string text;
    auto note = [&](const std::string& file, const std::string& why) {
        if (!why.empty()) files.error = file + ": " + why;
    };
    size_t read = 0;
    for (const auto& [written, file] : names) {
        const size_t head = std::strlen(kSyncedPrefix), tail = std::strlen(kSyncedSuffix);
        // The listing's pattern may be loose (Windows matches a short name too): the name exactly.
        if (file.size() <= head + tail || file.compare(0, head, kSyncedPrefix) != 0 ||
            file.compare(file.size() - tail, tail, kSyncedSuffix) != 0) {
            continue;
        }
        const std::string base = file.substr(head, file.size() - head - tail);
        if (!good_seed_name(base)) continue;
        if (++read > kMaxSeeds) {
            there.insert(base);  // not read, so not written over
            continue;
        }
        OfflineSeed copy;
        std::string why = read_file(join(save, file), text) ? parse_synced_file(text, copy)
                                                              : "unreadable";
        if (!why.empty()) {
            note(file, why);
            continue;
        }
        copy.written = written;
        copy.name = base + ".txt";
        there.insert(base);
        const auto at = local.find(base);
        if (at == local.end()) {
            note(file, write_local(base, format_offline_seed(copy), false));
            added.push_back(std::move(copy));
            continue;
        }
        OfflineSeed& mine = files.seeds[at->second];
        if (!offline_matches(mine, copy)) {
            note(file, "not the seed of the local file of that name");
            continue;
        }
        OfflineSeed merged = copy.written > mine.written ? merge_offline_seeds(mine, copy)
                                                         : merge_offline_seeds(copy, mine);
        merged.name = mine.name;
        const std::string result = format_offline_seed(merged);
        if (result != format_offline_seed(mine)) note(file, write_local(base, result, true));
        if (write && result != format_offline_seed(copy) && used(merged)) {
            note(file, write_synced_copy(save, base, merged));
        }
        mine = std::move(merged);
    }
    for (size_t i = 0; i < local_count; ++i) {
        const std::string base = base_name(files.seeds[i].name);
        if (write && there.count(base) == 0 && good_seed_name(base) && used(files.seeds[i])) {
            note(synced_file_name(base), write_synced_copy(save, base, files.seeds[i]));
        }
    }
    for (OfflineSeed& seed : added) {
        if (files.seeds.size() >= kMaxSeeds) break;
        files.seeds.push_back(std::move(seed));
    }
}

}  // namespace

#ifdef _WIN32
std::string put_offline_file(const std::wstring& folder, const char* build,
                             const std::string& request, OfflineSeed* seed_out,
                             const std::wstring& save, std::string* save_error) {
    if (save_error != nullptr) save_error->clear();
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
        join_synced(save, seed, body);  // the copy in the save folder, if any, made one with it
        parsed = std::move(seed);
        path = folder + L"\\seeds\\" + widen(name) + L".txt";
    } else {
        return "error not config or seed";
    }
    // The save folder's copy, written whether the local file can be kept or not.
    if (parsed && !save.empty() && body.size() <= kMaxFile) {
        std::string why = put_synced_copy(save, name, *parsed);
        if (save_error != nullptr) *save_error = std::move(why);
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

// `path` made absolute, with its . and .. folded and its short (8.3) names long, as far as the
// path exists, and no trailing separator.
std::wstring canonical_path(const std::wstring& path) {
    wchar_t full[32768];
    DWORD n = GetFullPathNameW(path.c_str(), 32768, full, nullptr);
    if (n == 0 || n >= 32768) return L"";
    std::wstring rest(full, n), tail;
    trim_separators(rest);
    // The longest part that exists, in its long form; what does not exist yet, as it is.
    for (;;) {
        wchar_t lng[32768];
        DWORD m = GetLongPathNameW(rest.c_str(), lng, 32768);
        if (m > 0 && m < 32768) {
            rest.assign(lng, m);
            break;
        }
        size_t cut = rest.find_last_of(L'\\');
        if (cut == std::wstring::npos || cut < 3) break;
        tail = rest.substr(cut) + tail;
        rest.resize(cut);
    }
    trim_separators(rest);
    return rest + tail;
}

bool equal_nocase(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() &&
           CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

// Whether `path` is `base` or inside it (both canonical_path's), case apart.
bool at_or_under(const std::wstring& path, const std::wstring& base) {
    if (base.empty()) return false;
    if (equal_nocase(path, base)) return true;
    return path.size() > base.size() && path[base.size()] == L'\\' &&
           equal_nocase(path.substr(0, base.size()), base);
}

}  // namespace

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

std::wstring os_save_folder() { return save_folder_of(known_folder(kRoamingAppData)); }

std::wstring os_offline_folder() {
    const std::wstring local = known_folder(kLocalAppData);
    return absolute_path(local) ? local + L"\\ZexionAP" : L"";
}

std::wstring restrict_to_test_folder(const std::wstring& folder) {
    if (!absolute_path(folder)) return L"";
    wchar_t temp[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, temp);
    if (n == 0 || n > MAX_PATH) return L"";
    const std::wstring base = canonical_path(std::wstring(temp, n)), path = canonical_path(folder);
    if (base.empty() || path.empty() || !at_or_under(path, base) || equal_nocase(path, base)) {
        return L"";
    }
    // Never the real ones: the folders Windows itself names for this user, not an environment
    // variable (a test sets LOCALAPPDATA to its own folder, which is what it is allowed to use).
    const std::wstring reals[] = {canonical_path(os_offline_folder()),
                                  canonical_path(os_save_folder())};
    for (const std::wstring& real : reals) {
        if (at_or_under(path, real)) return L"";
    }
    return folder;
}

// The helper's own folder alone (read_offline_files adds the save folder's copies).
static OfflineFiles read_local_files(const std::wstring& folder, const char* build) {
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
        seed.written = written;
        seed.name = narrow(file);
        files.seeds.push_back(std::move(seed));
    }
    return files;
}

#else
// The Linux build keeps the seeds' files as Windows does, in its own per-user folder
// (offline_folder), and no config: the Linux helper configures itself from the game's symbols.
std::string put_offline_file(const std::wstring& folder, const char*, const std::string& request,
                             OfflineSeed* seed_out, const std::wstring& save,
                             std::string* save_error) {
    if (save_error != nullptr) save_error->clear();
    size_t eol = request.find('\n');
    if (eol == std::string::npos) return "error no file";
    std::istringstream words(request.substr(0, eol));
    std::string op, kind, name, extra;
    words >> op >> kind;
    if (op != "offline_put") return "error not an offline_put request";
    if (kind != "seed") return "error not on the Linux build";
    if (!(words >> name) || (words >> extra) || !good_seed_name(name)) return "error bad name";
    std::string body = request.substr(eol + 1);
    OfflineSeed seed;
    std::string why = parse_offline_seed(body, seed);
    if (!why.empty()) return "error " + why;
    seed.name = name;
    join_synced(save, seed, body);  // the copy in the save folder, if any, made one with it
    // The save folder's copy, written whether the local file can be kept or not.
    if (!save.empty() && body.size() <= kMaxFile) {
        std::string synced = put_synced_copy(save, name, seed);
        if (save_error != nullptr) *save_error = std::move(synced);
    }
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

// The helper's own folder alone (read_offline_files adds the save folder's copies).
static OfflineFiles read_local_files(const std::wstring& folder, const char*) {
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
        seed.written = static_cast<uint64_t>(written);
        seed.name = file;
        files.seeds.push_back(std::move(seed));
    }
    return files;
}

std::wstring save_folder() {
    // As SDL_GetPrefPath does: $XDG_DATA_HOME if it is set, else ~/.local/share, then the
    // game's two names. An absolute path only. Under Flatpak's Steam, the sandbox's.
    const char* data = std::getenv("XDG_DATA_HOME");
    std::wstring base;
    if (data != nullptr && data[0] == '/') {
        base = bytes_path(data);
    } else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] == '/') {
        base = bytes_path(home) + L"/.local/share";
    } else {
        return L"";
    }
    return base + L"/Gallant Leaf/Zexion";
}

namespace {

// `path` (absolute) with its . and .. folded and its links resolved as far as the path exists,
// no trailing slash; "" for a path that is not absolute.
std::string canonical_path(const std::string& path) {
    if (path.empty() || path[0] != '/') return "";
    std::vector<std::string> parts;
    std::istringstream in(path);
    for (std::string part; std::getline(in, part, '/');) {
        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
            continue;
        }
        parts.push_back(part);
    }
    std::string tail;
    while (true) {
        std::string head;
        for (const std::string& part : parts) head += "/" + part;
        if (head.empty()) head = "/";
        char resolved[PATH_MAX];
        if (realpath(head.c_str(), resolved) != nullptr) {
            std::string out = resolved;
            if (out == "/") out.clear();
            return out + tail;
        }
        if (parts.empty()) return tail;
        tail = "/" + parts.back() + tail;
        parts.pop_back();
    }
}

bool at_or_under(const std::string& path, const std::string& base) {
    return !base.empty() && path.compare(0, base.size(), base) == 0 &&
           (path.size() == base.size() || path[base.size()] == '/');
}

// The home the passwd entry names for this user (not $HOME), or "".
std::string passwd_home() {
    const passwd* user = getpwuid(getuid());
    return user != nullptr && user->pw_dir != nullptr && user->pw_dir[0] == '/' ? user->pw_dir : "";
}

}  // namespace

std::wstring os_save_folder() {
    const std::string home = passwd_home();
    return home.empty() ? L"" : bytes_path((home + "/.local/share/Gallant Leaf/Zexion").c_str());
}

std::wstring os_offline_folder() {
    const std::string home = passwd_home();
    return home.empty() ? L"" : bytes_path((home + "/.local/share/ZexionAP").c_str());
}

std::wstring restrict_to_test_folder(const std::wstring& folder) {
    const std::string path = canonical_path(path_bytes(folder));
    const char* tmp = std::getenv("TMPDIR");
    const std::string base = canonical_path(tmp != nullptr && tmp[0] == '/' ? tmp : "/tmp");
    if (path.empty() || base.empty() || !at_or_under(path, base) || path == base) return L"";
    // Never the real ones: under the home the system names for this user, not $HOME or
    // $XDG_DATA_HOME, which a test sets to its own.
    const std::string home = passwd_home();
    if (!home.empty()) {
        for (const char* real : {"/.local/share/ZexionAP", "/.local/share/Gallant Leaf",
                                 "/.var/app/com.valvesoftware.Steam/.local/share/ZexionAP",
                                 "/.var/app/com.valvesoftware.Steam/.local/share/Gallant Leaf"}) {
            if (at_or_under(path, canonical_path(home + real))) return L"";
        }
    }
    return folder;
}
#endif

OfflineFiles read_offline_files(const std::wstring& folder, const char* build,
                                const std::wstring& save, bool write) {
    OfflineFiles files = read_local_files(folder, build);
    join_saves(files, folder, save, write);
    return files;
}

}  // namespace zap
