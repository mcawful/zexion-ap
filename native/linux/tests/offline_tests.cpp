// The Linux build's side of the seed files (native/src/offline.cpp, its POSIX half): the helper's
// folder and the game's save folder, where each seed's copy is kept for Steam Cloud
// (offline.h, "Seed files in the save folder"). The pure parts (the format, the crc, the merge)
// are the Windows tests' too (native/tests/offline_tests.cpp); this runs the file system's:
// the atomic write, the listing by name, the save folder from the environment, which seeds the
// game's slots use (a list, never the game's save files), what a read does with two folders, and
// the guard of the test-only build (restrict_to_test_folder, the write primitives).
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
#include <vector>

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

// The seeds of the game's slots (Game::saved_seeds) as a list, for the seed files `variants` make.
using Saved = std::vector<zap::OfflineSeed>;

Saved saved_of(std::initializer_list<int> variants) {
    Saved saved;
    for (int variant : variants) {
        zap::OfflineSeed seed;
        zap::parse_offline_seed(client_text(0, 0, variant), seed);
        saved.push_back(seed);
    }
    return saved;
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
    write_all(save / "Save Game 1.sav", "SAVE ONE");  // the game's own: never opened
    const Saved used = saved_of({0}), other = saved_of({1});
    const std::string put = "offline_put seed AP_1-0-1\n" + client_text(3, 2);
    zap::OfflineSeed got;
    std::string error = "unset";
    check(zap::put_offline_file(wide(local), "", put, &got, wide(save), &error, &used) == "stored" &&
              error.empty(),
          "a seed stored with its copy");
    zap::OfflineSeed copy;
    check(zap::parse_synced_file(read_all(save / kCopy), copy).empty() &&
              zap::format_offline_seed(copy) == client_text(3, 2),
          "the copy is the seed's text with a crc line");
    check(names_in(save) == std::set<std::string>{"Save Game 1.sav", kCopy},
          "the only file added, no temporary one left");
    check(read_all(save / "Save Game 1.sav") == "SAVE ONE", "the game's own file untouched");
    // No slot uses the seed, or none is known: no copy from a put.
    fs::remove(save / kCopy);
    zap::OfflineSeed unused;
    check(zap::put_offline_file(wide(local), "", put, &unused, wide(save), &error, &other) ==
                  "unchanged" &&
              error.empty() && unused.count == 3 && !fs::exists(save / kCopy),
          "no slot of the seed: no copy");
    check(zap::put_offline_file(wide(local), "", put, &unused, wide(save), &error, nullptr) ==
                  "unchanged" &&
              !fs::exists(save / kCopy),
          "no list of the slots yet: no copy");
    check(zap::put_offline_file(wide(local), "", put, &unused, wide(save), &error, &used) ==
                  "unchanged" &&
              fs::exists(save / kCopy),
          "a slot of it now: the copy is written");
    // A new machine: no local folder, the copy alone.
    fs::remove_all(local);
    zap::OfflineFiles files = zap::read_offline_files(wide(local), "", wide(save), false, &used);
    check(files.seeds.size() == 1 && !fs::exists(local), "the copy read, nothing written");
    files = zap::read_offline_files(wide(local), "", wide(save), true, &used);
    check(files.seeds.size() == 1 && files.seeds[0].name == "AP_1-0-1.txt" &&
              files.seeds[0].count == 3 &&
              read_all(local / "seeds" / "AP_1-0-1.txt") == client_text(3, 2),
          "and the local file made from it at a read that writes");
    // The first run after an upgrade: a local file only, mirrored while a slot uses its seed.
    fs::remove(save / kCopy);
    files = zap::read_offline_files(wide(local), "", wide(save), true, &used);
    check(files.seeds.size() == 1 && zap::parse_synced_file(read_all(save / kCopy), copy).empty(),
          "a local seed a slot uses gets its copy");
    write_all(local / "seeds" / "AP_2-0-1.txt", client_text(2, 0, 3));
    files = zap::read_offline_files(wide(local), "", wide(save), true, &used);
    check(files.seeds.size() == 2 && !fs::exists(save / "ZexionAP-seed-AP_2-0-1.sav"),
          "a seed no slot uses gets none");
    fs::remove(local / "seeds" / "AP_2-0-1.txt");
    // Two machines' copies: the larger count wins in both.
    write_all(local / "seeds" / "AP_1-0-1.txt", client_text(9, 1));
    files = zap::read_offline_files(wide(local), "", wide(save), true, &used);
    check(files.seeds.size() == 1 && files.seeds[0].count == 9 &&
              files.seeds[0].labels.find("label 21") != std::string::npos &&
              zap::parse_synced_file(read_all(save / kCopy), copy).empty() && copy.count == 9,
          "two copies made one, each written as the result");
    // A local file that is there and does not read is not written over from a copy.
    write_all(local / "seeds" / "AP_1-0-1.txt", "not a seed\n");
    files = zap::read_offline_files(wide(local), "", wide(save), true, &used);
    check(read_all(local / "seeds" / "AP_1-0-1.txt") == "not a seed\n",
          "an unread local file is left alone");
    write_all(local / "seeds" / "AP_1-0-1.txt", client_text(9, 1));
    // A copy cut short is refused and replaced; other names are not read.
    const std::string good = read_all(save / kCopy);
    write_all(save / kCopy, good.substr(0, good.size() - 30));
    write_all(save / "ZexionAP-seed-AP_2-0-1.sav.tmp", "x\n");
    write_all(save / "other-ZexionAP-seed-AP_2-0-1.sav", good);
    files = zap::read_offline_files(wide(local), "", wide(save), true, &used);
    check(files.seeds.size() == 1 && files.error.find("crc") != std::string::npos &&
              zap::parse_synced_file(read_all(save / kCopy), copy).empty() && copy.count == 9,
          "a cut copy refused, said, and replaced by the local one");
    // No save folder: not made; a folder in the copy's place: said, the rest goes on.
    check(zap::read_offline_files(wide(local), "", wide(root / "none"), true, &used).seeds.size() == 1 &&
              !fs::exists(root / "none"),
          "no save folder: none made");
    fs::remove(save / kCopy);
    fs::create_directories(save / kCopy);
    check(zap::put_offline_file(wide(local), "", "offline_put seed AP_1-0-1\n" + client_text(10, 1),
                                &got, wide(save), &error, &used) == "stored" &&
              !error.empty() && got.count == 10,
          "a copy that cannot be written is said, the local file written");
    // The write leaves no file mode wider than the owner's.
    fs::remove_all(save / kCopy);
    zap::put_offline_file(wide(local), "", "offline_put seed AP_1-0-1\n" + client_text(10, 1), &got,
                          wide(save), &error, &used);
    struct stat st {};
    check(stat((save / kCopy).c_str(), &st) == 0 && (st.st_mode & 0077) == 0,
          "the copy is the owner's alone");
    check(read_all(save / "Save Game 1.sav") == "SAVE ONE", "the saves were never touched");
    fs::remove_all(root);
}

void test_a_test_build_takes_only_a_folder_in_temp() {
    const fs::path temp = fs::temp_directory_path();
    const std::wstring inside = wide(temp / "zap_restrict_probe" / "ZexionAP");
    const std::wstring resolved = zap::restrict_to_test_folder(inside);
    check(!resolved.empty() &&
              resolved.size() >= 27 &&
              resolved.compare(resolved.size() - 27, 27, L"zap_restrict_probe/ZexionAP") == 0,
          "a folder inside the temporary folder, returned resolved");
    check(zap::restrict_to_test_folder(L"").empty(), "no folder: none");
    check(zap::restrict_to_test_folder(L"relative/folder").empty(), "a relative one: refused");
    check(zap::restrict_to_test_folder(wide(temp)).empty(), "the temporary folder itself: refused");
    check(zap::restrict_to_test_folder(wide(temp / "x" / ".." / "..")).empty(),
          "a path that climbs out of it: refused");
    check(zap::restrict_to_test_folder(wide(temp / "a" / ".." / "b")).empty(),
          "a path with .. in it at all: refused (never folded as text)");
    check(zap::restrict_to_test_folder(wide(temp) + L"2/x").empty(),
          "a sibling that begins with its name: refused");
    // Never the real ones: this user's, as the passwd entry names them: the default, Flatpak
    // Steam's and its default data folder.
    const std::vector<std::wstring> saves = zap::os_save_folders(), locals = zap::os_offline_folders();
    check(saves.size() == 3 && locals.size() == 3 && !saves[0].empty() && !locals[0].empty(),
          "the OS names three of each (default, Flatpak's, Flatpak's data)");
    for (const std::wstring& real : saves) {
        check(zap::restrict_to_test_folder(real).empty() &&
                  zap::restrict_to_test_folder(real + L"/Mods").empty(),
              "a real save folder, and what is inside it: refused");
    }
    for (const std::wstring& real : locals) {
        check(zap::restrict_to_test_folder(real).empty(), "a real per-user folder: refused");
    }
    // Links are followed, and `..` is never folded as text. A link in temp points only at a
    // stand-in folder of this test's own outside temp, never at a real folder or the root: should
    // this process die with it in place, nothing real is reachable through it. The real folders
    // are tested above by their paths alone.
    const fs::path root = temp / ("zap_restrict_link_" + std::to_string(getpid()));
    const fs::path stand_in = fs::path("/var/tmp") / ("zap_standin_" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::remove_all(stand_in);
    fs::create_directories(root / "dir");
    std::error_code error;
    fs::create_directories(stand_in, error);
    if (!error) {
        fs::create_directory_symlink(stand_in, root / "out", error);  // a link out of temp
        check(!error, "a symbolic link could be made");
        check(zap::restrict_to_test_folder(wide(root / "out" / "sub" / "probe")).empty(),
              "a link in temp to a folder outside it: refused, and what is under it");
        check(zap::restrict_to_test_folder(wide(root / "out" / "..")).empty(),
              "/tmp/x/l/.. (a link, then ..): refused");
        // A link in temp to a folder in temp is fine, and resolved.
        fs::create_directory_symlink(root / "dir", root / "inside", error);
        const std::wstring through = zap::restrict_to_test_folder(wide(root / "inside" / "x"));
        check(!through.empty() && through.find(L"/dir/x") != std::wstring::npos,
              "a link to another place in temp: allowed, and the resolved path returned");
        // The write primitives, handed a folder outside the temporary folder, write nothing: the
        // test-only core refuses (a put whose save folder is a link out of temp).
        const Saved used = saved_of({0});
        std::string refused;
        zap::OfflineSeed got;
        zap::put_offline_file(wide(root / "local"), "",
                              "offline_put seed AP_1-0-1\n" + client_text(3, 0), &got,
                              wide(root / "out"), &refused, &used);
        check(refused == "refused: not a test folder",
              "a put with a save folder through a link out of temp: the write is refused");
        check(fs::is_empty(stand_in), "and nothing was written there");
    } else {
        check(true, "no /var/tmp to make a stand-in folder: the link checks skipped");
    }
    fs::remove_all(root);
    fs::remove_all(stand_in);
    // A TMPDIR elsewhere is the temporary folder: what is under /tmp is then not.
    setenv("TMPDIR", "/var/tmp/zap-elsewhere", 1);
    check(zap::restrict_to_test_folder(inside).empty() &&
              !zap::restrict_to_test_folder(L"/var/tmp/zap-elsewhere/x").empty(),
          "$TMPDIR names the temporary folder");
    unsetenv("TMPDIR");
    fs::remove_all(root);
}

}  // namespace

int main() {
    test_the_save_folder_is_the_games();
    test_a_put_and_a_read_with_a_save_folder();
    test_a_test_build_takes_only_a_folder_in_temp();
    if (g_failures == 0) std::printf("linux offline tests: all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
