// A canary for the tests' folders: `zap_canary snapshot FILE` records what the real save folders
// and the helper's real per-user folders hold (every `ZexionAP-seed-*` file there with its size and
// write time; in the per-user folder every file at all: the seeds, the config files, the Linux
// token and any temporary file), `zap_canary verify FILE` fails if any of it changed. CTest runs
// the first before every other test and the second after them all (a fixture in CMakeLists.txt),
// so a test that writes a real folder is caught, not just fixed after the fact. Reads only. The
// folders are found as the operating system names them for this user (zap::os_save_folders,
// zap::os_offline_folders), not through an environment variable a test run may have pointed at
// scratch folders; on Linux the $XDG_DATA_HOME this run started with is watched too, when it is
// absolute and not a temporary folder. A folder that is not there is recorded as absent, and a test
// that makes it fails the check too. A created and deleted file shows as it must: by the listing,
// not by a count.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "offline.h"

namespace fs = std::filesystem;

namespace {

// The folder as a path: a wstring holds a Windows path as it is, a Linux path as its bytes.
fs::path as_path(const std::wstring& folder) {
#ifdef _WIN32
    return fs::path(folder);
#else
    std::string bytes;
    for (wchar_t c : folder) bytes += static_cast<char>(static_cast<unsigned char>(c));
    return fs::path(bytes);
#endif
}

// The entries of `folder` (recursively, or its files that begin with `prefix` only), one line each:
// the path inside it, the size and the write time; sorted. "absent" if it is not there.
std::string listing(const fs::path& folder, const std::string& prefix, bool recursive) {
    std::error_code error;
    if (!fs::is_directory(folder, error)) return "  absent\n";
    std::vector<std::string> lines;
    auto note = [&](const fs::directory_entry& entry) {
        std::error_code e;
        const std::string name = fs::relative(entry.path(), folder, e).generic_string();
        const std::string leaf = entry.path().filename().string();
        if (!prefix.empty() && leaf.compare(0, prefix.size(), prefix) != 0) return;
        const bool is_file = entry.is_regular_file(e);
        lines.push_back("  " + name + (is_file ? " " + std::to_string(entry.file_size(e)) + " " +
                                                      std::to_string(entry.last_write_time(e)
                                                                         .time_since_epoch()
                                                                         .count())
                                                : " dir"));
    };
    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(folder, error)) note(entry);
    } else {
        for (const auto& entry : fs::directory_iterator(folder, error)) note(entry);
    }
    std::sort(lines.begin(), lines.end());
    std::string out;
    for (const std::string& line : lines) out += line + "\n";
    return out.empty() ? "  empty\n" : out;
}

bool contains(const std::vector<std::wstring>& list, const std::wstring& folder) {
    return std::find(list.begin(), list.end(), folder) != list.end();
}

std::string snapshot() {
    std::vector<std::wstring> saves = zap::os_save_folders(), locals = zap::os_offline_folders();
#ifndef _WIN32
    // A non-default $XDG_DATA_HOME of the user's own (this run started with it), unless it is a
    // temporary folder a test run was pointed at.
    const char* data = std::getenv("XDG_DATA_HOME");
    const char* tmp = std::getenv("TMPDIR");
    const std::string temporary = tmp != nullptr && tmp[0] == '/' ? tmp : "/tmp";
    if (data != nullptr && data[0] == '/' && std::string(data).compare(0, temporary.size(), temporary) != 0) {
        std::wstring base;
        for (const char* c = data; *c != '\0'; ++c) base += static_cast<wchar_t>(static_cast<unsigned char>(*c));
        if (!contains(saves, base + L"/Gallant Leaf/Zexion")) saves.push_back(base + L"/Gallant Leaf/Zexion");
        if (!contains(locals, base + L"/ZexionAP")) locals.push_back(base + L"/ZexionAP");
    }
#endif
    std::ostringstream out;
    for (const std::wstring& folder : saves) {
        out << "save " << as_path(folder).string() << "\n"
            << (folder.empty() ? "  unnamed\n" : listing(as_path(folder), zap::kSyncedPrefix, false));
    }
    for (const std::wstring& folder : locals) {
        out << "offline " << as_path(folder).string() << "\n"
            << (folder.empty() ? "  unnamed\n" : listing(as_path(folder), "", true));
    }
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || (std::strcmp(argv[1], "snapshot") != 0 && std::strcmp(argv[1], "verify") != 0)) {
        std::printf("usage: zap_canary snapshot|verify FILE\n");
        return 2;
    }
    const std::string now = snapshot();
    if (std::strcmp(argv[1], "snapshot") == 0) {
        std::ofstream(argv[2], std::ios::binary) << now;
        std::printf("canary: recorded\n");
        return 0;
    }
    std::ifstream in(argv[2], std::ios::binary);
    std::stringstream before;
    before << in.rdbuf();
    if (before.str() != now) {
        std::printf("FAIL: the real folders changed while the tests ran\nbefore:\n%safter:\n%s",
                    before.str().c_str(), now.c_str());
        return 1;
    }
    std::printf("canary: the real folders are as they were\n");
    return 0;
}
