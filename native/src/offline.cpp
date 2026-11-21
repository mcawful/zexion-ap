#include "offline.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>

#include "kept.h"  // Kept
#include "offline_fs.h"
#include "requests.h"  // parse_discs, parse_number_list
#include "trap.h"      // trap::is_trap

namespace zap {

using namespace fs;  // the file system under this file (offline_fs_win.cpp, offline_fs_posix.cpp)

namespace {

constexpr size_t kMaxSeeds = 256;  // the newest kept
// One thread at a time writes or reads the seed files: the pipe thread (puts, reads) and the copy
// thread (a pass when the game's slots change) share their temporary files.
std::mutex g_files_mutex;

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

// The local seed file of `base` (NAME), written whole in the helper's own folder, made if need
// be: "" or what failed.
std::string write_local_seed(const std::wstring& folder, const std::string& base,
                             const std::string& text) {
    if (folder.empty()) return kNoFolder;
    make_folder(folder);
    const std::wstring seeds = join(folder, "seeds");
    make_folder(seeds);
    bool changed = false;
    return write_whole(join(seeds, base + ".txt"), text, changed);
}

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

// Whether a save of `seed` is among `saved` (Game::saved_seeds, published through SavedSeeds).
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
    // A copy that is there and cannot be read (opened by Steam or another program with no sharing,
    // locked, not ours to open, past the size read) is not seen, so it is not replaced: one that
    // read and did not parse is, as a half-synced file.
    std::string held;
    const std::wstring present = join(save, synced_file_name(base));
    if (exists(present) && !read_file(present, held)) return "its copy cannot be read, left as it is";
    bool changed = false;
    return write_whole(present, text, changed);
}

// A put seed's copy: written only while a save of the seed is among `saved` (the game's slots, as
// its memory holds them: nothing of the game's save files is opened); else it waits for the copy
// thread's pass (join_saves), when the game has saved it. "" or what failed.
std::string put_synced_copy(const std::wstring& save, const std::string& base,
                            const OfflineSeed& seed, const std::vector<OfflineSeed>* saved) {
    if (save.empty() || saved == nullptr || !in_use(*saved, seed)) return "";
    return write_synced_copy(save, base, seed);
}

// Seeds put by a client whose own file could not be kept (no folder, a failed write, a full
// disk) and whose copy waited for a slot, by NAME: the copy thread's pass reaches no file for
// them, so they are checked against the slots here. An entry goes once its copy is written or a
// local file for it exists (the next put or read finds it). Guarded by g_files_mutex.
//
// Never destroyed, and built on its first use (thread-safely), not as a global: on Linux the helper's
// constructor starts the serving thread, which reads the seed files and runs join_saves at once,
// and a global built by this file's own initializer comes after that constructor in the library's
// .init_array: the thread would walk a map not built yet, and at exit() one already destroyed
// (native/README.md, "Seed files in the save folder").
using PendingSeeds = std::map<std::string, OfflineSeed>;
PendingSeeds& pending_seeds() {
    static Kept<PendingSeeds> pending;
    return *pending;
}

void pend(const std::string& name, const OfflineSeed& seed) {
    PendingSeeds& pending = pending_seeds();
    if (pending.size() >= kMaxSeeds && pending.count(name) == 0) return;
    pending[name] = seed;
}

// `seed` (the newer: a put one) made one with its copy in the save folder, named `base`, if that
// is the same seed; else as it is.
OfflineSeed with_copy(const std::wstring& save, const std::string& base, const OfflineSeed& seed) {
    OfflineSeed theirs;
    if (save.empty() || !read_synced_copy(save, base, theirs).empty() ||
        !offline_matches(theirs, seed)) {
        return seed;
    }
    OfflineSeed merged = merge_offline_seeds(theirs, seed);
    merged.name = seed.name;
    return merged;
}

// A seed put by a client, made one with the copy already in the save folder if that is the same
// seed (the put one the newer): `seed` and `body`, its file's text, become the result's when that
// differs from what was put.
void join_synced(const std::wstring& save, OfflineSeed& seed, std::string& body) {
    OfflineSeed merged = with_copy(save, seed.name, seed);
    const std::string text = format_offline_seed(merged);
    if (text == format_offline_seed(seed)) return;
    body = text;
    seed = std::move(merged);
}

// The seeds read from the helper's own folder (`files.seeds`, each named NAME.txt), made one with
// those in the save folder: the newer copy's fields, with merge_offline_seeds' exceptions; each
// copy that then differs from the result is written again, and a seed with no copy in one of the
// folders gets it. Copies in the save folder are written only for a seed a save uses (`saved`,
// the game's slots in memory; none known: none written): a seed none uses keeps the copy it has, not refreshed, and gets none. A copy
// that does not read is not trusted, and is replaced by a good local one (a half-synced file), else
// left alone. With `write` false nothing is written anywhere: the seeds are only made one.
void join_saves(OfflineFiles& files, const std::wstring& folder, const std::wstring& save,
                bool write, const std::vector<OfflineSeed>* saved) {
    if (save.empty() || !folder_exists(save)) return;
    auto used = [&](const OfflineSeed& seed) { return saved != nullptr && in_use(*saved, seed); };
    // The local file, written whole, but never over one that is there and was not read (a file
    // past the 256 read, or one that does not parse: a copy must not replace what it has not seen).
    auto write_local = [&](const std::string& base, const std::string& text, bool was_read) {
        if (!write || folder.empty()) return std::string();
        if (!was_read && exists(join(join(folder, "seeds"), base + ".txt"))) return std::string();
        return write_local_seed(folder, base, text);
    };
    const auto names = list_files(save, kSyncedPrefix, kSyncedSuffix);
    std::map<std::string, size_t> local;  // NAME -> its index in files.seeds
    for (size_t i = 0; i < files.seeds.size(); ++i) local[base_name(files.seeds[i].name)] = i;
    const size_t local_count = files.seeds.size();
    // NAMEs whose copy in the save folder is as it should be, or is there and not read (left alone).
    std::set<std::string> there;
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
        if (!read_file(join(save, file), text)) {
            // Not opened (locked, in use with no sharing, access denied) or not read: a file not
            // seen is not replaced. Left as it is, and the next pass tries it again.
            there.insert(base);
            note(file, "unreadable");
            continue;
        }
        const std::string why = parse_synced_file(text, copy);
        if (!why.empty()) {  // read, and no good: a half-synced file, replaced by a good local one
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
    // Put seeds with no local file: the copy once a slot uses the seed, else they wait.
    PendingSeeds& pending = pending_seeds();
    for (auto it = pending.begin(); it != pending.end();) {
        const std::string base = it->first;
        const bool file_there = local.count(base) != 0 ||
                                (!folder.empty() && exists(join(join(folder, "seeds"), base + ".txt")));
        if (file_there) {
            it = pending.erase(it);
        } else if (write && used(it->second)) {
            const OfflineSeed result = with_copy(save, base, it->second);
            const std::string why = write_synced_copy(save, base, result);
            note(synced_file_name(base), why);
            it = why.empty() ? pending.erase(it) : std::next(it);
        } else {
            ++it;
        }
    }
    for (OfflineSeed& seed : added) {
        if (files.seeds.size() >= kMaxSeeds) break;
        files.seeds.push_back(std::move(seed));
    }
}


// The helper's own folder alone (read_offline_files adds the save folder's copies).
OfflineFiles read_local_files(const std::wstring& folder, const char* build) {
    OfflineFiles files;
    if (folder.empty()) {
        files.error = kNoFolder;
        return files;
    }
    read_cached_config(folder, build, files);
    const std::wstring seeds = join(folder, "seeds");
    std::string listing_error;
    const auto names = list_files(seeds, "", ".txt", &listing_error);
    if (!listing_error.empty()) {
        files.error = "seeds: " + listing_error;
        return files;
    }
    std::string text;
    for (const auto& [written, file] : names) {
        if (files.seeds.size() >= kMaxSeeds) break;
        OfflineSeed seed;
        std::string why =
            read_file(join(seeds, file), text) ? parse_offline_seed(text, seed) : "unreadable";
        if (!why.empty()) {
            files.error = file + ": " + why;
            continue;
        }
        seed.written = written;
        seed.name = file;
        files.seeds.push_back(std::move(seed));
    }
    return files;
}

}  // namespace

std::string put_offline_file(const std::wstring& folder, const char* build,
                             const std::string& request, OfflineSeed* seed_out,
                             const std::wstring& save, std::string* save_error,
                             const std::vector<OfflineSeed>* saved) {
    std::lock_guard<std::mutex> files_lock(g_files_mutex);
    if (save_error != nullptr) save_error->clear();
    const size_t eol = request.find('\n');
    if (eol == std::string::npos) return "error no file";
    std::string body = request.substr(eol + 1);
    std::istringstream words(request.substr(0, eol));
    std::string op, kind, name, extra;
    words >> op >> kind;
    if (op != "offline_put") return "error not an offline_put request";
    std::optional<OfflineSeed> parsed;  // a seed's file, once checked
    std::wstring path;
    if (kind == "seed") {
        if (!(words >> name) || (words >> extra) || !good_seed_name(name)) return "error bad name";
        OfflineSeed seed;
        std::string why = parse_offline_seed(body, seed);
        if (!why.empty()) return "error " + why;
        seed.name = name;
        join_synced(save, seed, body);  // the copy in the save folder, if any, made one with it
        parsed = std::move(seed);
        path = join(join(folder, "seeds"), name + ".txt");
    } else {
        const std::string refused = put_other_kind(kind, folder, build, body, path);
        if (!refused.empty()) return refused;
    }
    // The save folder's copy, written whether the local file can be kept or not.
    if (parsed && !save.empty() && body.size() <= kMaxFile) {
        std::string why = put_synced_copy(save, name, *parsed, saved);
        if (save_error != nullptr) *save_error = std::move(why);
    }
    // The seed is the running helper's whether the file can be kept or not (no folder, a
    // file too large to read back, a write that fails): its own traps and its saves' disk
    // counts come from it with a client too (Game::watch_own_traps). So it is handed over
    // before the checks that concern the file alone. The pipe takes no request much over its
    // 64 KB buffer, which bounds what is parsed here.
    // Kept for the pending list if the local file cannot be written (a copy waiting for a slot).
    const std::optional<OfflineSeed> waiting = parsed;
    const bool may_wait = waiting && !save.empty() && body.size() <= kMaxFile;
    if (parsed && seed_out != nullptr) *seed_out = std::move(*parsed);
    if (folder.empty()) {
        if (may_wait) pend(name, *waiting);
        return kNoFolderError;
    }
    if (body.size() > kMaxFile) return "error too large";
    make_folder(folder);
    if (parsed) make_folder(join(folder, "seeds"));
    bool changed = false;
    std::string why = write_whole(path, body, changed);
    if (!why.empty()) {
        if (may_wait) pend(name, *waiting);
        return "error " + why;
    }
    if (waiting) pending_seeds().erase(name);  // its file is there: the passes find it
    return changed ? "stored" : "unchanged";
}

OfflineFiles read_offline_files(const std::wstring& folder, const char* build,
                                const std::wstring& save, bool write,
                                const std::vector<OfflineSeed>* saved) {
    std::lock_guard<std::mutex> files_lock(g_files_mutex);
    OfflineFiles files = read_local_files(folder, build);
    join_saves(files, folder, save, write, saved);
    return files;
}

std::string copy_pass(const std::wstring& folder, const char* build, const std::wstring& save,
                      const SavedSeeds& saved) {
    const std::vector<OfflineSeed> slots = saved.get();
    return read_offline_files(folder, build, save, true, &slots).error;
}

bool SavedSeeds::publish(const std::vector<OfflineSeed>& seeds) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return false;  // a reader has it: the next call publishes
    const bool same = generation_.load() != 0 && seeds.size() == seeds_.size() &&
                      std::equal(seeds.begin(), seeds.end(), seeds_.begin(),
                                 [](const OfflineSeed& a, const OfflineSeed& b) {
                                     return offline_matches(a, b);
                                 });
    if (same) return false;
    seeds_ = seeds;
    generation_.fetch_add(1);
    return true;
}

std::vector<OfflineSeed> SavedSeeds::get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return seeds_;
}

}  // namespace zap
