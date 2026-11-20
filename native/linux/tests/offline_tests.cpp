// The Linux build's side of the seed files (native/src/offline.cpp, its POSIX half): the helper's
// folder and the game's save folder, where each seed's copy is kept for Steam Cloud
// (offline.h, "Seed files in the save folder"). The pure parts (the format, the crc, the merge)
// are the Windows tests' too (native/tests/offline_tests.cpp); this runs the file system's:
// the atomic write, the listing by name, the save folder from the environment, and what a read
// does with two folders.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include "offline.h"

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::wstring wide(const fs::path& path) { return path.wstring(); }

std::string read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_all(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::set<std::string> names_in(const fs::path& folder) {
    std::set<std::string> names;
    for (const auto& entry : fs::directory_iterator(folder)) names.insert(entry.path().filename());
    return names;
}

// A seed file as the client writes it (client/offline.py, seed_text), `count` disks.
std::string client_text(int count, int labels) {
    std::string text = "zexion-ap offline 1\ntable ";
    for (int i = 0; i < 154; ++i) {
        const char* digits = "0123456789abcdef";
        text += digits[(i % 47) >> 4];
        text += digits[(i % 47) & 15];
    }
    text += "\nstart 0600bf004901\nintel 1\nassists lock\ndiscs " + std::to_string(count) +
            " 12 434f4c4c454354\nunfound 40,1312\ntraps -\n";
    if (labels > 0) text += "ap_labels\n";
    for (int i = 0; i < labels; ++i) text += "label " + std::to_string(20 + i) + " 14 41\n";
    return text;
}

const std::string kCopy = "ZexionAP-seed-AP_1-0-1.sav";

void test_the_save_folder_is_the_games() {
    // SDL's SDL_GetPrefPath on Linux: $XDG_DATA_HOME, else ~/.local/share, then the game's names.
    setenv("XDG_DATA_HOME", "/data/home", 1);
    setenv("HOME", "/home/someone", 1);
    check(zap::save_folder() == L"/data/home/Gallant Leaf/Zexion", "the save folder: XDG_DATA_HOME's");
    unsetenv("XDG_DATA_HOME");
    check(zap::save_folder() == L"/home/someone/.local/share/Gallant Leaf/Zexion",
          "else under ~/.local/share");
    setenv("XDG_DATA_HOME", "relative", 1);
    check(zap::save_folder() == L"/home/someone/.local/share/Gallant Leaf/Zexion",
          "a relative XDG_DATA_HOME is not used");
    unsetenv("XDG_DATA_HOME");
    setenv("HOME", "relative", 1);
    check(zap::save_folder().empty(), "no absolute base: no save folder");
}

void test_a_put_and_a_read_with_a_save_folder() {
    const fs::path root = fs::temp_directory_path() / ("zap_linux_sync_" + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path local = root / "local", save = root / "save";
    fs::create_directories(save);
    write_all(save / "Save Game 1.sav", "SAVE ONE");
    const std::string put = "offline_put seed AP_1-0-1\n" + client_text(3, 2);
    zap::OfflineSeed got;
    std::string error = "unset";
    check(zap::put_offline_file(wide(local), "", put, &got, wide(save), &error) == "stored" &&
              error.empty(),
          "a seed stored with its copy");
    zap::OfflineSeed copy;
    check(zap::parse_synced_file(read_all(save / kCopy), copy).empty() &&
              zap::format_offline_seed(copy) == client_text(3, 2),
          "the copy is the seed's text with a crc line");
    check(names_in(save) == std::set<std::string>{"Save Game 1.sav", kCopy},
          "the only file added, no temporary one left");
    check(read_all(save / "Save Game 1.sav") == "SAVE ONE", "the game's own file untouched");
    // A new machine: no local folder, the copy alone.
    fs::remove_all(local);
    zap::OfflineFiles files = zap::read_offline_files(wide(local), "", wide(save));
    check(files.seeds.size() == 1 && files.seeds[0].name == "AP_1-0-1.txt" &&
              files.seeds[0].count == 3 &&
              read_all(local / "seeds" / "AP_1-0-1.txt") == client_text(3, 2),
          "the copy read, and the local file made from it");
    // The first run after an upgrade: a local file only, mirrored.
    fs::remove(save / kCopy);
    files = zap::read_offline_files(wide(local), "", wide(save));
    check(files.seeds.size() == 1 && zap::parse_synced_file(read_all(save / kCopy), copy).empty(),
          "a local seed with no copy gets it");
    // Two machines' copies: the larger count wins in both.
    write_all(local / "seeds" / "AP_1-0-1.txt", client_text(9, 1));
    files = zap::read_offline_files(wide(local), "", wide(save));
    check(files.seeds.size() == 1 && files.seeds[0].count == 9 &&
              files.seeds[0].labels.find("label 21") != std::string::npos &&
              zap::parse_synced_file(read_all(save / kCopy), copy).empty() && copy.count == 9,
          "two copies made one, each written as the result");
    // A copy cut short is refused and replaced; other names are not read.
    const std::string good = read_all(save / kCopy);
    write_all(save / kCopy, good.substr(0, good.size() - 30));
    write_all(save / "ZexionAP-seed-AP_2-0-1.sav.tmp", "x\n");
    write_all(save / "other-ZexionAP-seed-AP_2-0-1.sav", good);
    files = zap::read_offline_files(wide(local), "", wide(save));
    check(files.seeds.size() == 1 && files.error.find("crc") != std::string::npos &&
              read_all(save / kCopy) == good,
          "a cut copy refused, said, and replaced by the local one");
    // No save folder: not made; a folder in the copy's place: said, the rest goes on.
    check(zap::read_offline_files(wide(local), "", wide(root / "none")).seeds.size() == 1 &&
              !fs::exists(root / "none"),
          "no save folder: none made");
    fs::remove(save / kCopy);
    fs::create_directories(save / kCopy);
    check(zap::put_offline_file(wide(local), "", "offline_put seed AP_1-0-1\n" + client_text(10, 1),
                                &got, wide(save), &error) == "stored" &&
              !error.empty() && got.count == 10,
          "a copy that cannot be written is said, the local file written");
    // The write leaves no file mode wider than the owner's.
    fs::remove_all(save / kCopy);
    zap::put_offline_file(wide(local), "", "offline_put seed AP_1-0-1\n" + client_text(10, 1), &got,
                          wide(save), &error);
    struct stat st {};
    check(stat((save / kCopy).c_str(), &st) == 0 && (st.st_mode & 0077) == 0,
          "the copy is the owner's alone");
    fs::remove_all(root);
}

}  // namespace

int main() {
    test_the_save_folder_is_the_games();
    test_a_put_and_a_read_with_a_save_folder();
    if (g_failures == 0) std::printf("linux offline tests: all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
