// Game's offline look (offline.h): an AP save dressed with no client (or with a client of
// another seed), the file select's counts, and the notices.
#include "game_calls.h"

#include <algorithm>
#include <iterator>

namespace zap {

namespace {

constexpr int kOfflineFrames = 15;  // frames between looks
// What the player is told on loading an AP save with no client.
constexpr char kOfflineWarning[] =
    "ARCHIPELAGO CLIENT NOT CONNECTED. CHECKS ARE SENT WHEN IT CONNECTS.";
// And once a client connects while a save was dressed offline.
constexpr char kOnlineNotice[] = "ARCHIPELAGO CLIENT CONNECTED.";
// And once a load, from a client whose seed the loaded randomizer save is not.
constexpr char kOtherSeedNotice[] =
    "THIS SAVE IS FROM ANOTHER SEED. NOTHING IS SENT FROM IT, AND YOUR ITEMS WAIT FOR YOUR "
    "SEED'S SAVE.";

}  // namespace

void Game::set_offline(std::vector<OfflineSeed> seeds) {
    add_known_seeds(seeds);
    leave_offline();
    offline_seeds_ = std::move(seeds);
}

void Game::merge_offline(std::vector<OfflineSeed> seeds) {
    add_known_seeds(seeds);
    // Each seed in place of its old record, or after the others: the indices stay, so the
    // dressed seed (offline_index_) is still the one dressed, and its record is kept as it is.
    for (OfflineSeed& seed : seeds) {
        auto same = [&](const OfflineSeed& k) { return offline_matches(k, seed); };
        auto it = std::find_if(offline_seeds_.begin(), offline_seeds_.end(), same);
        if (it == offline_seeds_.end()) {
            offline_seeds_.push_back(std::move(seed));
        } else if (it - offline_seeds_.begin() != offline_index_) {
            *it = std::move(seed);
        }
    }
}

void Game::drop_notice(const char* text) {
    // The helper's notices are the only silent popups (a client's never are: parse_batch).
    for (auto it = popups_.begin(); it != popups_.end();) {
        it = it->silent && it->suffix == text ? popups_.erase(it) : std::next(it);
    }
}

void Game::leave_offline_for_client() {
    leave_offline();
    // The new client says other_save again, and is told again: a notice not shown yet is the
    // last client's.
    drop_notice(kOtherSeedNotice);
    other_save_ = false;
    other_warned_ = false;
}

void Game::drop_client_notices() {
    // Not shown yet when its client went away: the other-seed notice is told again to the next
    // client that says so, and CLIENT CONNECTED. is no longer so.
    drop_notice(kOtherSeedNotice);
    drop_notice(kOnlineNotice);
    other_save_ = false;
    other_warned_ = false;
}

void Game::leave_offline_seed() {
    drop_notice(kOfflineWarning);
    if (offline_index_ < 0) return;
    offline_index_ = -1;
    offline_count_ = -1;
    offline_told_ = -1;
    drop_save_look();
}

void Game::leave_offline() {
    leave_offline_seed();
    if (!offline_file_counts_.empty()) {
        offline_file_counts_.clear();
        apply_file_counts();
    }
}

void Game::apply_file_counts() {
    // The client's counts for its seed's saves, the offline ones for the other known seeds'.
    std::map<int, int> merged = offline_file_counts_;
    for (const auto& [slot, count] : client_file_counts_) merged[slot] = count;
    if (merged != files_.counts()) files_.set_counts(std::move(merged));
}

bool Game::section_is(const OfflineSeed& seed, const BYTE* section) const {
    return offline_matches(seed, offline_seed_of(section));
}

int Game::loaded_seed() const {
    // The marker first: after a normal save the section is still the last randomizer game's.
    if (!rando_save_loaded()) return -1;
    for (size_t i = 0; i < offline_seeds_.size(); ++i) {
        if (section_is(offline_seeds_[i], at_.rando_blob)) return static_cast<int>(i);
    }
    return -1;
}

std::string Game::known_disk_row() const {
    // A save of a known seed with the disks, loaded in play or not: its count from the seed's
    // file, for the pause screen's row until the client's (or the offline look's) is set. During
    // a load it may read the last save's count for a frame: only the branch's code byte follows
    // it, put right the next frame, and no pause screen opens mid-load.
    const int seed = loaded_seed();
    if (seed < 0) return "";
    const OfflineSeed& sd = offline_seeds_[static_cast<size_t>(seed)];
    if (!sd.discs) return "";
    const bool told = at_.flags[flag::kScannerTold] != 0;
    return pause_row_text(offline_disk_count(sd, at_.map_grid), told ? sd.required : 0);
}

void Game::watch_offline() {
    if (!configured_) return;
    if (*at_.save_loaded == kNoSaveLoaded) {
        // No save: each notice comes again with the next load, and one not shown yet is
        // withdrawn (it was for the save just left; pump_popups would show it in the next).
        for (const char* notice : {kOfflineWarning, kOnlineNotice, kOtherSeedNotice}) {
            drop_notice(notice);
        }
        offline_warned_ = false;
        other_warned_ = false;
        offline_played_ = -1;
        other_save_ = false;
    }
    if (offline_seeds_.empty()) {
        leave_offline();
        return;
    }
    if (offline_frames_-- > 0) return;
    offline_frames_ = kOfflineFrames;
    // A save is dressed with no client, or with a client that said the loaded save is not its
    // seed's (other_save); undressed when that stops.
    const bool other = active_ && other_save_;
    const bool dress = !active_ || other;
    // Which known seed's save is loaded.
    const int seed = dress ? loaded_seed() : -1;
    // A save is dressed once it is in play (not while it loads, when the marker and section may
    // still be another save's), and undressed at once.
    if (seed != offline_index_ && (seed < 0 || in_play())) {
        leave_offline_seed();
        offline_index_ = seed;
        if (seed >= 0) {  // the look itself: below
            // Once a load: the player is told why received items and hints wait (with a
            // client of another seed, its own notice said so: other_save).
            if (!offline_warned_ && !other) {
                popups_.push_back(Popup{kAreaMapItem, kOfflineWarning, true, true});
                offline_warned_ = true;
            }
            if (!other) offline_played_ = seed;
        }
    } else if (seed != offline_index_ && offline_index_ >= 0) {
        leave_offline_seed();  // another seed's save, not in play yet: dressed once it is
    }
    if (offline_index_ >= 0) {
        const OfflineSeed& sd = offline_seeds_[static_cast<size_t>(offline_index_)];
        // The save's look, set when it is dressed and kept while it is: a client of another
        // seed, attaching while this save is in play, takes its own look back (no labels, ASSIST
        // free, AREA MAP, no disks), which is not this save's.
        if (!ap_items_.active() && !sd.labels.empty()) {
            std::map<int, Label> labels;
            if (parse_labels(sd.labels, labels).empty()) set_labels(std::move(labels));
        }
        if (intel_on() != sd.intel) set_intel(sd.intel);
        if (assists_locked_ != sd.lock_assists) set_assists_locked(sd.lock_assists);
        if (sd.discs) {
            int count = offline_disk_count(sd, at_.map_grid);
            int told = at_.flags[flag::kScannerTold] != 0 ? 1 : 0;
            if (count != offline_count_ || told != offline_told_ || scan_line_ != sd.scan_line) {
                offline_count_ = count;
                offline_told_ = told;
                set_discs(count, sd.required, sd.scan_line, told != 0);
            }
        }
    }
    // The file select: each save of a known seed with the disks, by its own copy of the section
    // (valid for a randomizer save in use: client/discs.py, seed_slots), with or without a
    // client (whose own counts win for its seed's saves: apply_file_counts).
    std::map<int, int> counts;
    for (int n = 1; n <= slot::kCount; ++n) {
        const BYTE* copy = slot_section(n);
        if (copy == nullptr) continue;
        const BYTE* block = at_.slot_array + static_cast<ptrdiff_t>(n - 1) * slot::kStride;
        for (const OfflineSeed& sd : offline_seeds_) {
            if (sd.discs && section_is(sd, copy)) {
                // This save's own count, from its map grid (docs/re/save-format.md, "The map
                // grid marks collected locations").
                counts[n] = offline_disk_count(sd, block + slot::kMap);
                break;
            }
        }
    }
    if (counts != offline_file_counts_) {
        offline_file_counts_ = std::move(counts);
        apply_file_counts();
    }
}

std::string Game::other_save() {
    // Only an AP save of another seed: one whose seed the helper has a file for (every new game
    // from ARCHIPELAGO is made with a client, which leaves one). A plain randomizer game is left
    // alone.
    if (!configured_ || loaded_seed() < 0) return "unknown";
    other_save_ = true;
    if (other_warned_) return "already told";
    drop_notice(kOfflineWarning);
    popups_.push_back(Popup{kAreaMapItem, kOtherSeedNotice, true, true});
    other_warned_ = true;
    return "told";
}

bool Game::client_back() {
    bool shown = configured_ && offline_played_ >= 0 && rando_save_loaded() &&
                 static_cast<size_t>(offline_played_) < offline_seeds_.size() &&
                 section_is(offline_seeds_[static_cast<size_t>(offline_played_)], at_.rando_blob);
    offline_played_ = -1;
    if (shown) popups_.push_back(Popup{kAreaMapItem, kOnlineNotice, true, true});
    return shown;
}

std::string Game::offline_state() const {
    if (offline_seeds_.empty()) return "-";
    std::string out = std::to_string(offline_seeds_.size());
    if (offline_index_ >= 0) out += ":" + offline_seeds_[static_cast<size_t>(offline_index_)].name;
    return out;
}

}  // namespace zap
