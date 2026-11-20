// A canary for the tests' folders: `zap_canary snapshot FILE` records what the real save folder
// and the helper's real per-user folder hold (how many seed copies and seed files, and the newest
// write time of each), `zap_canary verify FILE` fails if either changed. CTest runs the first
// before every other test and the second after them all (a fixture in CMakeLists.txt), so a test
// that writes a real folder is caught, not just fixed after the fact. Reads only; the folders are
// found as the operating system names them for this user (zap::os_save_folder,
// zap::os_offline_folder), not through an environment variable a test run may have pointed at
// scratch folders. A folder that is not there is recorded as absent, and a test that makes it
// fails the check too.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

// "absent", or "COUNT NEWEST" of the files in `folder` whose names start with `prefix` and end
// with `suffix`.
std::string describe(const std::wstring& folder, const char* prefix, const char* suffix) {
    std::error_code error;
    if (folder.empty() || !fs::is_directory(as_path(folder), error)) return "absent";
    long long count = 0, newest = 0;
    for (const auto& entry : fs::directory_iterator(as_path(folder), error)) {
        const std::string name = entry.path().filename().string();
        const size_t head = std::strlen(prefix), tail = std::strlen(suffix);
        if (name.size() < head + tail || name.compare(0, head, prefix) != 0 ||
            name.compare(name.size() - tail, tail, suffix) != 0) {
            continue;
        }
        ++count;
        const auto written = fs::last_write_time(entry.path(), error).time_since_epoch().count();
        if (written > newest) newest = written;
    }
    return std::to_string(count) + " " + std::to_string(newest);
}

std::wstring seeds_folder(const std::wstring& local) {
    if (local.empty()) return local;
#ifdef _WIN32
    return local + L"\\seeds";
#else
    return local + L"/seeds";
#endif
}

std::string snapshot() {
    return "save " + describe(zap::os_save_folder(), zap::kSyncedPrefix, zap::kSyncedSuffix) +
           "\nseeds " + describe(seeds_folder(zap::os_offline_folder()), "", ".txt") + "\n";
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
        std::printf("canary: %s", now.c_str());
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
    std::printf("canary: the real folders are as they were\n%s", now.c_str());
    return 0;
}
