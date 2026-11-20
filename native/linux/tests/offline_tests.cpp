// The Linux build's side of the seed files (native/src/offline.cpp, its POSIX half): the helper's
// folder and the game's save folder, where each seed's copy is kept for Steam Cloud
// (offline.h, "Seed files in the save folder"). The pure parts (the format, the crc, the merge)
// are the Windows tests' too (native/tests/offline_tests.cpp); this runs the file system's:
// the atomic write, the listing by name, the save folder from the environment, which saves use a
// seed, what a read does with two folders, and the test builds' folder guard.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
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

uint8_t table_of(int variant, int index) {
    return static_cast<uint8_t>(index % 47 + (index == 0 ? variant : 0));
}

// A seed file as the client writes it (client/offline.py, seed_text), `count` disks.
std::string client_text(int count, int labels, int variant = 0) {
    std::string text = "zexion-ap offline 1\ntable ";
    for (int i = 0; i < 154; ++i) {
        const char* digits = "0123456789abcdef";
        text += digits[table_of(variant, i) >> 4];
        text += digits[table_of(variant, i) & 15];
    }
    text += "\nstart 0600bf004901\nintel 1\nassists lock\ndiscs " + std::to_string(count) +
            " 12 434f4c4c454354\nunfound 40,1312\ntraps -\n";
    if (labels > 0) text += "ap_labels\n";
    for (int i = 0; i < labels; ++i) text += "label " + std::to_string(20 + i) + " 14 41\n";
    return text;
}

// A save of slot `n` in `save`, as far as the helper reads one: the randomizer marker, the empty
// mark, the table of seed `variant` and the start.
void write_save(const fs::path& save, int n, int variant = 0, int marker = 1, bool empty = false) {
    std::string data(0x13100, '\0');
    data[0x9402] = static_cast<char>(marker);
    data[0x9360] = empty ? '\xFF' : '\0';
    for (int i = 0; i < 154; ++i) data[0x12A80 + i] = static_cast<char>(table_of(variant, i));
    const unsigned char start[6] = {0x06, 0x00, 0xbf, 0x00, 0x49, 0x01};
    std::memcpy(&data[0x12B24], start, 6);
    write_all(save / ("Save Game " + std::to_string(n) + ".sav"), data);
}

const std::string kCopy = "ZexionAP-seed-AP_1-0-1.sav";

void test_the_save_folder_is_the_games() {
    // SDL's SDL_GetPrefPath on Linux: $XDG_DATA_HOME, else ~/.local/share, then the game's names.
    const char* real_home = std::getenv("HOME");
    const std::string kept_home = real_home != nullptr ? real_home : "";
    const char* real_data = std::getenv("XDG_DATA_HOME");
    const std::string kept_data = real_data != nullptr ? real_data : "";
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
    if (real_home != nullptr) setenv("HOME", kept_home.c_str(), 1);
    if (real_data != nullptr) setenv("XDG_DATA_HOME", kept_data.c_str(), 1);
}

void test_a_put_and_a_read_with_a_save_folder() {
    const fs::path root = fs::temp_directory_path() / ("zap_linux_sync_" + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path local = root / "local", save = root / "save";
    fs::create_directories(save);
    write_save(save, 1);  // a save of the seed
    const std::string slot_one = read_all(save / "Save Game 1.sav");
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
    check(read_all(save / "Save Game 1.sav") == slot_one, "the game's own file untouched");
    // No save uses the seed: no copy from a put; a slot of another seed, a normal game, an empty
    // slot and a short file do not count.
    fs::remove(save / kCopy);
    fs::remove(save / "Save Game 1.sav");
    write_save(save, 2, 1);
    write_save(save, 3, 0, 0);
    write_save(save, 4, 0, 1, true);
    write_all(save / "Save Game 5.sav", "SHORT");
    zap::OfflineSeed unused;
    check(zap::put_offline_file(wide(local), "", put, &unused, wide(save), &error) == "unchanged" &&
              error.empty() && unused.count == 3 && !fs::exists(save / kCopy),
          "no save of the seed: no copy");
    write_save(save, 6);
    check(zap::put_offline_file(wide(local), "", put, &unused, wide(save), &error) == "unchanged" &&
              fs::exists(save / kCopy),
          "a save of it now: the copy is written");
    // A new machine: no local folder, the copy alone.
    fs::remove_all(local);
    zap::OfflineFiles files = zap::read_offline_files(wide(local), "", wide(save), false);
    check(files.seeds.size() == 1 && !fs::exists(local), "the copy read, nothing written");
    files = zap::read_offline_files(wide(local), "", wide(save));
    check(files.seeds.size() == 1 && files.seeds[0].name == "AP_1-0-1.txt" &&
              files.seeds[0].count == 3 &&
              read_all(local / "seeds" / "AP_1-0-1.txt") == client_text(3, 2),
          "and the local file made from it at a read that writes");
    // The first run after an upgrade: a local file only, mirrored while a save uses its seed.
    fs::remove(save / kCopy);
    files = zap::read_offline_files(wide(local), "", wide(save));
    check(files.seeds.size() == 1 && zap::parse_synced_file(read_all(save / kCopy), copy).empty(),
          "a local seed a save uses gets its copy");
    write_all(local / "seeds" / "AP_2-0-1.txt", client_text(2, 0, 3));  // no slot holds variant 3
    files = zap::read_offline_files(wide(local), "", wide(save));
    check(files.seeds.size() == 2 && !fs::exists(save / "ZexionAP-seed-AP_2-0-1.sav"),
          "a seed no save uses gets none");
    fs::remove(local / "seeds" / "AP_2-0-1.txt");
    // Two machines' copies: the larger count wins in both.
    write_all(local / "seeds" / "AP_1-0-1.txt", client_text(9, 1));
    files = zap::read_offline_files(wide(local), "", wide(save));
    check(files.seeds.size() == 1 && files.seeds[0].count == 9 &&
              files.seeds[0].labels.find("label 21") != std::string::npos &&
              zap::parse_synced_file(read_all(save / kCopy), copy).empty() && copy.count == 9,
          "two copies made one, each written as the result");
    // A local file that is there and does not read is not written over from a copy.
    write_all(local / "seeds" / "AP_1-0-1.txt", "not a seed\n");
    files = zap::read_offline_files(wide(local), "", wide(save));
    check(read_all(local / "seeds" / "AP_1-0-1.txt") == "not a seed\n",
          "an unread local file is left alone");
    write_all(local / "seeds" / "AP_1-0-1.txt", client_text(9, 1));
    // A copy cut short is refused and replaced; other names are not read.
    const std::string good = read_all(save / kCopy);
    write_all(save / kCopy, good.substr(0, good.size() - 30));
    write_all(save / "ZexionAP-seed-AP_2-0-1.sav.tmp", "x\n");
    write_all(save / "other-ZexionAP-seed-AP_2-0-1.sav", good);
    files = zap::read_offline_files(wide(local), "", wide(save));
    check(files.seeds.size() == 1 && files.error.find("crc") != std::string::npos &&
              zap::parse_synced_file(read_all(save / kCopy), copy).empty() && copy.count == 9,
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
    check(read_all(save / "Save Game 6.sav").size() == 0x13100, "the saves were never touched");
    fs::remove_all(root);
}

void test_a_test_build_takes_only_a_folder_in_temp() {
    const fs::path temp = fs::temp_directory_path();
    const std::wstring inside = wide(temp / "zap_restrict_probe" / "ZexionAP");
    check(zap::restrict_to_test_folder(inside) == inside, "a folder inside the temporary folder");
    check(zap::restrict_to_test_folder(L"").empty(), "no folder: none");
    check(zap::restrict_to_test_folder(L"relative/folder").empty(), "a relative one: refused");
    check(zap::restrict_to_test_folder(wide(temp)).empty(), "the temporary folder itself: refused");
    check(zap::restrict_to_test_folder(wide(temp / "x" / ".." / "..")).empty(),
          "a path that climbs out of it: refused");
    check(!zap::restrict_to_test_folder(wide(temp / "a" / ".." / "b")).empty(),
          "one that climbs and stays inside: allowed");
    check(zap::restrict_to_test_folder(wide(temp) + L"2/x").empty(),
          "a sibling that begins with its name: refused");
    // A link out of it is followed: a path through it is where the link points.
    const fs::path root = temp / ("zap_restrict_link_" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] == '/') {
        std::error_code error;
        fs::create_directory_symlink(home, root / "out", error);
        if (!error) {
            check(zap::restrict_to_test_folder(wide(root / "out" / ".local" / "share" / "ZexionAP"))
                      .empty() ||
                      !fs::exists(fs::path(home) / ".local" / "share" / "ZexionAP"),
                  "a link out of the temporary folder to the real one: refused");
        }
    }
    fs::remove_all(root);
    // A TMPDIR elsewhere is the temporary folder: what is under /tmp is then not.
    setenv("TMPDIR", "/var/tmp/zap-elsewhere", 1);
    check(zap::restrict_to_test_folder(inside).empty() &&
              !zap::restrict_to_test_folder(L"/var/tmp/zap-elsewhere/x").empty(),
          "$TMPDIR names the temporary folder");
    unsetenv("TMPDIR");
}

}  // namespace

int main() {
    test_the_save_folder_is_the_games();
    test_a_put_and_a_read_with_a_save_folder();
    test_a_test_build_takes_only_a_folder_in_temp();
    if (g_failures == 0) std::printf("linux offline tests: all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
