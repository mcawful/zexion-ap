// AP saves without the client (native/README.md, "AP saves without the client";
// docs/re/memory.md, "AP saves without the client"). The client sends each seed's file and its
// resolved addresses (`offline_put`); the helper writes them under %LOCALAPPDATA%\ZexionAP (an
// MSIX-packaged client's AppData writes are redirected, inferred; the Linux helper the seeds
// alone, under $XDG_DATA_HOME, else ~/.local/share), and a copy of the file of each seed a save uses into the
// game's save folder under its own name (kSyncedPrefix, below), where Steam Cloud syncs it with
// the saves, and nothing else there: never a file the game uses. It
// reads them at start and each time a client goes away. A seed file matches a save by the
// section's location count, item table and start record (client/save_patch.py, section_matches).
//
// A seed file, one line each, then the labels:
//   zexion-ap offline 1
//   table HEX          the item table, 154 bytes, or 155 with Secret Shuffle (the section's
//                      +0x26C)
//   start HEX          the start record, 6 bytes (+0x310)
//   intel 0|1          the Area Map named INTEL CHIP
//   assists lock|free  the ASSIST lock
//   discs COUNT REQUIRED HEX   (or "discs -") the disk count every save of the seed shares
//                      (the disks the slot starts with and those received, when the client
//                      last wrote it), the number needed, and the scanner's line in hex: read as
//                      the `discs` request is (a TOLD after it is ignored)
//   unfound I,J,...    (or "-") map grid indices (y * 54 + x) of this player's own disks, none
//                      of them in COUNT: each one a save's map shows collected counts one more
//                      in that save
//   traps I:T,J:T,...  (or "-"; none in a file from before it) map grid indices of this
//                      player's own traps, each with its trap's item (trap.h): set off when
//                      its cell turns collected in play (Game::watch_own_traps)
//   ap_labels          then "label X Y HEX" lines to the end, as the client's request
#pragma once

#include "platform.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "game_layout.h"  // kMapCells, rando

namespace zap {

constexpr uint8_t kCollectedCell = 0x1B;

struct OfflineSeed {
    std::string name;  // the file's name, for `hello` and logs
    int locations = rando::kLocations;  // the table's length: 154, or 155 with Secret Shuffle
    uint8_t table[rando::kMaxLocations] = {};  // past `locations`, 0
    uint8_t start[rando::kStartSize] = {};
    bool intel = false;
    bool lock_assists = false;
    bool discs = false;
    int count = 0, required = 0;
    std::string scan_line;
    std::vector<int> unfound;
    // This player's own traps: the map grid index of each one's cell, and its item (trap.h).
    struct OwnTrap {
        int cell = 0;
        uint8_t item = 0;
        bool operator==(const OwnTrap&) const = default;
    };
    std::vector<OwnTrap> traps;
    std::string labels;  // the "ap_labels" request, or empty
    // When the file it was read from was last written, in the file system's own units: only to
    // tell which of a seed's two copies (the local one, the save folder's) is the newer, so
    // compared with another read in the same run. 0 for a seed that was put or made.
    uint64_t written = 0;
};

// "" or what is wrong.
std::string parse_offline_seed(const std::string& text, OfflineSeed& out);

// The seed file's text for `seed`: what parse_offline_seed reads back as the same seed, and, for
// a seed read from a client's file, the text the client made (client/offline.py, seed_text).
std::string format_offline_seed(const OfflineSeed& seed);

// "Playing on more than one machine" ("Seed files in the save folder", native/README.md): a copy
// of the file of each seed a save uses, kept in the game's save folder, where Steam Cloud syncs it
// with the saves.
// The only files a helper writes there: named kSyncedPrefix + the seed's NAME + kSyncedSuffix (the
// suffix is the one Steam's Auto-Cloud rule syncs), never a name the game uses.
constexpr const char* kSyncedPrefix = "ZexionAP-seed-";
constexpr const char* kSyncedSuffix = ".sav";

// The synced copy's text: the seed's text, then one last line "crc XXXXXXXX", the CRC-32 of
// every byte before it (upper-case hex), so a copy cut short by a sync is refused, never read as
// another seed.
std::string synced_file_text(const OfflineSeed& seed);
// "" or what is wrong with a synced copy's text (a missing, wrong or misplaced crc line, or a
// seed file that does not parse); `out` as parse_offline_seed leaves it.
std::string parse_synced_file(const std::string& text, OfflineSeed& out);

// One seed's two copies made one, `older` and `newer` (offline_matches, so the same table and
// start): `newer`'s fields, except that Assist Unlock is never taken back (free wins), the larger
// disk count stands with its own-disk cells and the scanner's line (a tie: `newer`'s), the
// labels with more "label" lines stand, and the traps are `older`'s when `newer` has none.
OfflineSeed merge_offline_seeds(const OfflineSeed& older, const OfflineSeed& newer);

// Whether `other` (offline_seed_of a section, or another seed) is `seed`: the same location
// count, item table and start.
bool offline_matches(const OfflineSeed& seed, const OfflineSeed& other);
// A seed made of a randomizer section's location count, item table and start (nothing else
// set), to match another section against.
OfflineSeed offline_seed_of(const uint8_t* section);

// The disk count with the map `grid` (54 * 54 cells) as it is.
int offline_disk_count(const OfflineSeed& seed, const uint8_t* grid);

// What the client left for the helper: the addresses for this build (the fallback should the
// helper's own scan, findaddr.h, find none), and the seeds.
struct OfflineFiles {
    std::map<std::string, uint64_t> config;  // empty: none for this build
    // The first bytes of each game function the client found, as it read them ("check KEY HEX"
    // lines after the config): the cache is used only if the game in memory holds the same.
    std::map<std::string, std::string> checks;
    std::vector<OfflineSeed> seeds;
    std::string error;  // the last file that could not be read, and why
};

// The per-user folder (%LOCALAPPDATA%\ZexionAP; on Linux $XDG_DATA_HOME/ZexionAP, else
// ~/.local/share/ZexionAP), or "" if that base is not an absolute path. The functions below
// take the folder, so tests give their own.
std::wstring offline_folder();
// The game's own save folder, where the saves are and Steam Cloud syncs: what the game's
// SDL_GetPrefPath("Gallant Leaf", "Zexion") returns, found without calling SDL (which makes the
// folder): on Windows the Roaming AppData known folder (SDL's: CSIDL_APPDATA) and those two names,
// else %APPDATA%; on Linux $XDG_DATA_HOME, else ~/.local/share, then the same two names (Flatpak
// Steam's home is the sandbox's). Without a trailing separator, or "" if that is not an absolute
// path. On Windows found once, the first time asked (the helper asks at start); on Linux read
// from the environment at each call, which the Linux helper makes once, in its constructor
// before the game's threads run. Never made by the helper: a game that has no folder has no saves.
std::wstring save_folder();
#ifdef _WIN32
// The save folder under the Roaming AppData folder `roaming` (a test gives its own), or "" if
// `roaming` is not an absolute path.
std::wstring save_folder_under(const std::wstring& roaming);
#endif
// The real save folders and the helper's real per-user folders, as the operating system itself
// names them for this user, whatever the environment holds ($HOME, $XDG_DATA_HOME,
// %LOCALAPPDATA% and %APPDATA% are what a test sets to its own). Windows: the known folders
// (one of each). Linux: under the home the passwd entry names, the default `.local/share`,
// Flatpak Steam's `.var/app/com.valvesoftware.Steam/.local/share` and its default
// `XDG_DATA_HOME`, `.var/app/com.valvesoftware.Steam/data`. Empty where the OS names none.
// restrict_to_test_folder refuses them and what is inside, and the tests' canary watches them
// (native/tests/canary.cpp).
std::vector<std::wstring> os_save_folders();
std::vector<std::wstring> os_offline_folders();
// A test build's folder, resolved: `folder` after every link in it (a junction, a symbolic link, a
// `subst` drive) is followed, if it is an absolute path with no `..` in it and strictly inside the
// system's temporary folder (GetTempPathW; on Linux $TMPDIR, else /tmp), also resolved, and neither
// one of the real folders above nor inside one, else "". The resolved path is what the caller
// must use. Refuses everything when a real folder cannot be resolved. Test builds take every
// folder they write through this (ZAP_HOOKTEST), and the test-only core build checks each write
// primitive with it (ZAP_TEST_BUILD), so a test run cannot write the real folders.
std::wstring restrict_to_test_folder(const std::wstring& folder);

// The seeds of the randomizer saves among the game's 30 slots, as the game's memory holds them
// (Game::saved_seeds: the match the file select makes), which the game thread publishes from its
// frame call and the other threads read: the helpers never open the game's save files, since
// the game opens one to write with no share mode and a read open would fail its save. A copy of a
// seed goes to the save folder only while a slot here uses the seed.
class SavedSeeds {
public:
    // Game thread: never waits. Publishes `seeds` if they are not the list published last (or
    // none was yet) and the lock is free; true if it did.
    bool publish(const std::vector<OfflineSeed>& seeds);
    // The list published last (empty before the first publish).
    std::vector<OfflineSeed> get() const;
    // How many lists were published: 0 before the first. The copy thread acts when it changes.
    uint64_t generation() const { return generation_.load(); }

private:
    mutable std::mutex mutex_;
    std::vector<OfflineSeed> seeds_;
    std::atomic<uint64_t> generation_{0};
};

// Pipe thread (file I/O; never the frame call): reads the folder's config for `build` (the
// client's `config` request, parse_config, then its check lines) and its seeds, the newest
// first; a file that does not parse is skipped. With `save` (save_folder(), or a test's), the
// seeds' copies in the save folder are read too and made one with the local ones
// (merge_offline_seeds); each copy that then differs from the result is rewritten, whole or not at
// all, the save folder's only under a kSyncedPrefix name: a seed with no copy there (or a
// poorer one) gets it, a seed only there gets its local file. A copy is written only for a seed
// a slot in `saved` uses (the match the file select makes; null: none known, none written), so a
// seed no save has keeps the copy it has, not refreshed, and gets none. `write` false writes
// nothing, anywhere: the seeds are only made one (a client going away that set nothing, such as
// `helper.py status`, reads so). `error` is then also the last copy that could not be written.
// One thread at a time reads or writes (a mutex inside).
OfflineFiles read_offline_files(const std::wstring& folder, const char* build,
                                const std::wstring& save = L"", bool write = true,
                                const std::vector<OfflineSeed>* saved = nullptr);

// The copy thread's pass, the same on both helpers (a thread of its own, started by the host): the
// seed files read as read_offline_files does, with the copy of each seed a slot in `saved` now
// uses written into the save folder. The last copy that could not be written, or "".
std::string copy_pass(const std::wstring& folder, const char* build, const std::wstring& save,
                      const SavedSeeds& saved);

// Pipe thread: an `offline_put` request, "offline_put config" or "offline_put seed NAME" then
// the file's text, checked as the reader checks it and written in place of the old one, whole
// or not at all: "stored", "unchanged" or "error <why>". A seed that reads right is also
// given in `seed`, named NAME, if not null (Game::merge_offline), whether or not its file could
// be written: the running helper works from it with a client too. With `save`, the seed is first
// made one with the copy already in the save folder (merge_offline_seeds, the put one the
// newer), that is what is handed over and written to the local file, and the copy there is
// rewritten as well if a slot in `saved` uses the seed (else the copy thread writes it when the
// game's slots do); `save_error` (if not null) then says why that last write failed, else is
// empty. The reply is about the local file alone. No save file of the game is opened.
std::string put_offline_file(const std::wstring& folder, const char* build,
                             const std::string& request, OfflineSeed* seed = nullptr,
                             const std::wstring& save = L"", std::string* save_error = nullptr,
                             const std::vector<OfflineSeed>* saved = nullptr);

// "" if the cached addresses fit the game in `image`: at least kOfflineMinChecks functions
// checked, each one's bytes as the client read them, each at a key of the config.
constexpr size_t kOfflineMinChecks = 8;
std::string check_offline_cache(HMODULE image, const OfflineFiles& files);

}  // namespace zap
