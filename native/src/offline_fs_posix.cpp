// The seed files' POSIX half, the Linux helper's: the file system under offline.cpp
// (offline_fs.h) and the per-user folders. The Linux helper keeps no address cache.

#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <sstream>

#include "offline_fs.h"

namespace zap {

namespace fs {

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

std::string write_whole(const std::wstring& path, const std::string& text, bool& changed) {
    std::string old;
    changed = !(read_file(path, old) && old == text);
    if (!changed) return "";
    if (!write_allowed(path)) return "refused: not a test folder";
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

void make_folder(const std::wstring& folder) {
    if (!folder_allowed(folder)) return;
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

std::vector<std::pair<uint64_t, std::string>> list_files(const std::wstring& folder,
                                                         const std::string& prefix,
                                                         const std::string& suffix,
                                                         std::string* error) {
    std::vector<std::pair<uint64_t, std::string>> names;
    const std::string where = path_bytes(folder);
    DIR* dir = opendir(where.c_str());
    if (dir == nullptr) {
        if (errno != ENOENT && error != nullptr) *error = "error " + std::to_string(errno);
        return names;
    }
    while (const dirent* entry = readdir(dir)) {
        const std::string file = entry->d_name;
        if (file.size() <= prefix.size() + suffix.size() ||
            file.compare(0, prefix.size(), prefix) != 0 ||
            file.compare(file.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        struct stat st {};
        if (stat((where + "/" + file).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        names.emplace_back(static_cast<uint64_t>(st.st_mtim.tv_sec) * 1000000000ull +
                               static_cast<uint64_t>(st.st_mtim.tv_nsec),
                           file);
    }
    closedir(dir);
    sort_newest_first(names);
    return names;
}

void read_cached_config(const std::wstring&, const char*, OfflineFiles&) {}

std::string put_other_kind(const std::string&, const std::wstring&, const char*,
                           const std::string&, std::wstring&) {
    return "error not on the Linux build";
}

}  // namespace fs

std::string check_offline_cache(HMODULE, const OfflineFiles&) { return "not on the Linux build"; }

std::wstring offline_folder() {
    // As the game's own saves: $XDG_DATA_HOME, else ~/.local/share, an absolute path only (a
    // relative one would resolve against the game's folder). Under Flatpak's Steam, the
    // sandbox's.
    const char* data = std::getenv("XDG_DATA_HOME");
    if (data != nullptr && data[0] == '/') return fs::bytes_path(data) + L"/ZexionAP";
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] == '/') return fs::bytes_path(home) + L"/.local/share/ZexionAP";
    return L"";
}
std::wstring save_folder() {
    // As SDL_GetPrefPath does: $XDG_DATA_HOME if it is set, else ~/.local/share, then the
    // game's two names. An absolute path only. Under Flatpak's Steam, the sandbox's.
    const char* data = std::getenv("XDG_DATA_HOME");
    std::wstring base;
    if (data != nullptr && data[0] == '/') {
        base = fs::bytes_path(data);
    } else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] == '/') {
        base = fs::bytes_path(home) + L"/.local/share";
    } else {
        return L"";
    }
    return base + L"/Gallant Leaf/Zexion";
}

namespace {

// `path` with every link in it followed (realpath of the longest part that exists), then what
// does not exist yet as it is, no trailing slash; "" for a path that is not absolute, has a `..`
// in it (never folded as text: `/tmp/x/l/..` is not where it reads), or cannot be resolved.
std::string resolved_path(const std::string& path) {
    if (path.empty() || path[0] != '/') return "";
    std::vector<std::string> parts;
    std::istringstream in(path);
    for (std::string part; std::getline(in, part, '/');) {
        if (part.empty() || part == ".") continue;
        if (part == "..") return "";
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
        if (parts.empty()) return "";
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

// Under that home: the default data folder, Flatpak Steam's and its default $XDG_DATA_HOME.
constexpr const char* kDataFolders[] = {"/.local/share", "/.var/app/com.valvesoftware.Steam/.local/share",
                                        "/.var/app/com.valvesoftware.Steam/data"};

}  // namespace

std::vector<std::wstring> os_save_folders() {
    const std::string home = passwd_home();
    if (home.empty()) return {L""};
    std::vector<std::wstring> folders;
    for (const char* data : kDataFolders) {
        folders.push_back(fs::bytes_path((home + data + "/Gallant Leaf/Zexion").c_str()));
    }
    return folders;
}

std::vector<std::wstring> os_offline_folders() {
    const std::string home = passwd_home();
    if (home.empty()) return {L""};
    std::vector<std::wstring> folders;
    for (const char* data : kDataFolders) {
        folders.push_back(fs::bytes_path((home + data + "/ZexionAP").c_str()));
    }
    return folders;
}

std::wstring restrict_to_test_folder(const std::wstring& folder) {
    const std::string path = resolved_path(fs::path_bytes(folder));
    const char* tmp = std::getenv("TMPDIR");
    const std::string base = resolved_path(tmp != nullptr && tmp[0] == '/' ? tmp : "/tmp");
    if (path.empty() || base.empty() || !at_or_under(path, base) || path == base) return L"";
    // Never the real ones: under the home the system names for this user, not $HOME or
    // $XDG_DATA_HOME, which a test sets to its own. One that cannot be named or resolved (and a
    // Flatpak Steam's or a non-default home's own folders do not exist) refuses nothing more,
    // but a home that cannot be named refuses everything.
    std::vector<std::wstring> reals = os_save_folders();
    for (std::wstring& real : os_offline_folders()) reals.push_back(std::move(real));
    for (const std::wstring& real : reals) {
        if (real.empty()) return L"";
        if (at_or_under(path, resolved_path(fs::path_bytes(real)))) return L"";
    }
    return fs::bytes_path(path.c_str());
}
}  // namespace zap
