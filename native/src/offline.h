// AP saves without the client (native/README.md, "AP saves without the client";
// docs/re/memory.md, "AP saves without the client"). The client sends each seed's file and its
// resolved addresses (`offline_put`); the helper writes them under %LOCALAPPDATA%\ZexionAP (an
// MSIX-packaged client's AppData writes are redirected, inferred; the Linux helper the seeds
// alone, under $XDG_DATA_HOME, else ~/.local/share), and a copy of each seed's file into the
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

#include <cstdint>
#include <map>
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
// of each seed's file kept in the game's save folder, where Steam Cloud syncs it with the saves.
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
// SDL_GetPrefPath("Gallant Leaf", "Zexion") returns (on Windows asked of the game's SDL2.dll, else
// %APPDATA%\Gallant Leaf\Zexion; on Linux $XDG_DATA_HOME, else ~/.local/share, then the same
// two names: Flatpak Steam's home is the sandbox's), without a trailing separator, or "" if that
// is not an absolute path. Never made by the helper itself (SDL's own call makes it, as the
// game's does at start): a game that has no folder has no saves.
std::wstring save_folder();
// Pipe thread (file I/O; never the frame call): reads the folder's config for `build` (the
// client's `config` request, parse_config, then its check lines) and its seeds, the newest
// first; a file that does not parse is skipped. With `save` (save_folder(), or a test's), the
// seeds' copies in the save folder are read too and made one with the local ones
// (merge_offline_seeds); each copy that then differs from the result is rewritten, whole or not at
// all, the save folder's only under a kSyncedPrefix name: a seed with no copy there (or a
// poorer one) gets it, a seed only there gets its local file. `error` is then also the last
// copy that could not be written.
OfflineFiles read_offline_files(const std::wstring& folder, const char* build,
                                const std::wstring& save = L"");

// Pipe thread: an `offline_put` request, "offline_put config" or "offline_put seed NAME" then
// the file's text, checked as the reader checks it and written in place of the old one, whole
// or not at all: "stored", "unchanged" or "error <why>". A seed that reads right is also
// given in `seed`, named NAME, if not null (Game::merge_offline), whether or not its file could
// be written: the running helper works from it with a client too. With `save`, the seed is first
// made one with the copy already in the save folder (merge_offline_seeds, the put one the
// newer), that is what is handed over and written to the local file, and the copy there is
// rewritten as well; `save_error` (if not null) then says why that last write failed, else is empty. The
// reply is about the local file alone.
std::string put_offline_file(const std::wstring& folder, const char* build,
                             const std::string& request, OfflineSeed* seed = nullptr,
                             const std::wstring& save = L"", std::string* save_error = nullptr);

// "" if the cached addresses fit the game in `image`: at least kOfflineMinChecks functions
// checked, each one's bytes as the client read them, each at a key of the config.
constexpr size_t kOfflineMinChecks = 8;
std::string check_offline_cache(HMODULE image, const OfflineFiles& files);

}  // namespace zap
