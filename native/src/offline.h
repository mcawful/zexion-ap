// AP saves without the client (offline): the look the client gives them, kept from a file.
//
// The client sends the helper, over its pipe (`offline_put`), one small file per seed it
// connects to (client/offline.py) and the addresses it resolved by pattern for the game's
// build, and the helper writes them into a per-user folder (%LOCALAPPDATA%\ZexionAP\seeds\
// and \config-<build>.txt), never the game's. The helper writes them itself, from inside the
// game: run from a packaged app (an MSIX package, the Claude desktop app in this repo's tests),
// the client's own writes there were not found by the game (2026-10-25: error 3, path not
// found), inferred to be the package's AppData redirect. The helper reads them when it starts and each time a client goes away. With no client, it
// configures itself from the cache (only for the build it was made on), and while a save of
// one of those seeds is loaded it gives the save the same look: the item labels (AP ITEM,
// PROGRESSION ITEM, this player's own disks), the Intel Chip's name, the ASSIST lock, the
// OS Install Disks goal's screens and the escape skip; and the file select shows those
// saves' disk counts. What needs the server (received items, hints, the goal) waits for the
// client. A seed file is matched to a save by the randomizer section's item table and start
// record, as the client matches it (client/save_patch.py, section_matches).
//
// A seed file, one line each, then the labels:
//   zexion-ap offline 1
//   table HEX          the item table, 154 bytes (the section's +0x26C)
//   start HEX          the start record, 6 bytes (+0x310)
//   intel 0|1          the Area Map named INTEL CHIP
//   assists lock|free  the ASSIST lock
//   discs COUNT REQUIRED HEX   (or "discs -") the disk count when the client last saw it,
//                      the number needed, and the scanner's line in hex
//   unfound I,J,...    (or "-") map grid indices (y * 54 + x) of this player's own disks
//                      not counted in COUNT: each one the map shows collected counts one more
//   ap_labels          then "label X Y HEX" lines to the end, as the client's request
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zap {

constexpr int kOfflineLocations = 154;
constexpr int kOfflineStartSize = 6;
constexpr ptrdiff_t kOfflineTableAt = 0x26C, kOfflineStartAt = 0x310;  // in the section
constexpr int kMapCells = 54 * 54;
constexpr uint8_t kCollectedCell = 0x1B;

struct OfflineSeed {
    std::string name;  // the file's name, for `hello` and logs
    uint8_t table[kOfflineLocations] = {};
    uint8_t start[kOfflineStartSize] = {};
    bool intel = false;
    bool lock_assists = false;
    bool discs = false;
    int count = 0, required = 0;
    std::string scan_line;
    std::vector<int> unfound;
    std::string labels;  // the "ap_labels" request, or empty
};

// "" or what is wrong.
std::string parse_offline_seed(const std::string& text, OfflineSeed& out);

// Whether `table` (154 bytes) and `start` (6 bytes) are this seed's.
bool offline_matches(const OfflineSeed& seed, const uint8_t* table, const uint8_t* start);

// The disk count with the map `grid` (54 * 54 cells) as it is.
int offline_disk_count(const OfflineSeed& seed, const uint8_t* grid);

// What the client left for the helper: the addresses for this build, and the seeds.
struct OfflineFiles {
    std::map<std::string, uint64_t> config;  // empty: none for this build
    // The first bytes of each game function the client found, as it read them ("check KEY HEX"
    // lines after the config): the cache is used only if the game in memory holds the same.
    std::map<std::string, std::string> checks;
    std::vector<OfflineSeed> seeds;
    std::string error;  // the last file that could not be read, and why
};

// The per-user folder (%LOCALAPPDATA%\ZexionAP), or `folder` if given (tests).
std::wstring offline_folder();
// Reads the folder's config for `build` (the client's `config` request, parse_config, then
// its check lines) and its seeds, the newest first; a file that does not parse is skipped.
OfflineFiles read_offline_files(const std::wstring& folder, const char* build);

// Pipe thread: an `offline_put` request, "offline_put config" or "offline_put seed NAME" then
// the file's text, checked as the reader checks it and written in place of the old one, whole
// or not at all: "stored", "unchanged" or "error <why>". NAME as the client makes it: letters,
// digits, dashes and underscores, ending in -TEAM-SLOT.
std::string put_offline_file(const std::wstring& folder, const char* build,
                             const std::string& request);

// "" if the cached addresses fit the game in `image`: at least kOfflineMinChecks functions
// checked, each one's bytes as the client read them, each at a key of the config.
constexpr size_t kOfflineMinChecks = 8;
std::string check_offline_cache(HMODULE image, const OfflineFiles& files);

}  // namespace zap
