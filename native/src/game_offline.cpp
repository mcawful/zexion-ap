// Game's part for AP saves without the client (offline.h): the seeds' files taken, a save of
// one of them dressed while no client is there (or one of another seed is), the file select's
// counts, and the notices that tell the player.
#include "game_calls.h"

#include <cstring>

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
    "THE ARCHIPELAGO CLIENT IS FOR ANOTHER SEED. LOAD THAT SEED'S SAVE OR START A NEW GAME.";

}  // namespace

void Game::set_offline(std::vector<OfflineSeed> seeds) {
    leave_offline();
    offline_seeds_ = std::move(seeds);
}

void Game::drop_offline_warning() {
    for (auto it = popups_.begin(); it != popups_.end();) {
        it = it->silent && it->suffix == kOfflineWarning ? popups_.erase(it) : std::next(it);
    }
}

void Game::leave_offline_seed() {
    drop_offline_warning();
    if (offline_index_ < 0) return;
    offline_index_ = -1;
    offline_count_ = -1;
    offline_told_ = -1;
    restore_ap_items();
    set_intel(false);
    restore_intel();
    set_assists_locked(false);
    restore_assists();
    set_discs(0, 0, "");
    restore_discs();
    offline_skip_ = false;
    restore_skip();
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
    return offline_matches(seed, section + kOfflineTableAt, section + kOfflineStartAt);
}

void Game::watch_offline() {
    if (!configured_) return;
    if (*save_loaded_ == kNoSaveLoaded) {
        // No save: each notice comes again with the next load.
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
    const bool other = other_mode();
    const bool dress = !active_ || other;
    // This seed's save loaded: the randomizer section as the game loaded it (it is the
    // previous one after a normal save, hence the marker).
    int seed = -1;
    if (dress && *save_loaded_ != kNoSaveLoaded && at<uint16_t>(rando_marker_, 0) == 1) {
        for (size_t i = 0; i < offline_seeds_.size() && seed < 0; ++i) {
            if (section_is(offline_seeds_[i], rando_blob_)) seed = static_cast<int>(i);
        }
    }
    // A save is dressed once it is in play (not while it loads, when the marker and section may
    // still be another save's), and undressed at once.
    if (seed != offline_index_ && (seed < 0 || in_play())) {
        leave_offline_seed();
        offline_index_ = seed;
        if (seed >= 0) {
            const OfflineSeed& sd = offline_seeds_[static_cast<size_t>(seed)];
            std::map<int, Label> labels;
            if (!sd.labels.empty() && parse_labels(sd.labels, labels).empty()) {
                set_labels(std::move(labels));
            }
            set_intel(sd.intel);
            set_assists_locked(sd.lock_assists);
            offline_skip_ = true;
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
        if (sd.discs) {
            int count = offline_disk_count(sd, map_grid_);
            int told = flags_[flag::kScannerTold] != 0 ? 1 : 0;
            if (count != offline_count_ || told != offline_told_) {
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
        const BYTE* block = slot_array_ + static_cast<ptrdiff_t>(n - 1) * slot::kStride;
        if (block[slot::kEmpty] == slot::kEmptyMark ||
            field<uint16_t>(block, slot::kRandoMarker) != 1) {
            continue;
        }
        const BYTE* copy = rando_slots_ + static_cast<ptrdiff_t>(n - 1) * kSectionSize;
        for (const OfflineSeed& sd : offline_seeds_) {
            if (sd.discs && section_is(sd, copy)) {
                // This save's own count: its map, in its block (docs/re/save-format.md).
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

bool Game::other_save() {
    // A client whose session sees a randomizer save in play that is not its seed's: the player
    // is told once a load, and the save gets its offline look if it is a known seed's.
    if (!configured_ || *save_loaded_ == kNoSaveLoaded) return false;
    bool told = other_warned_;
    other_save_ = true;
    if (!other_warned_) {
        drop_offline_warning();
        popups_.push_back(Popup{kAreaMapItem, kOtherSeedNotice, true, true});
        other_warned_ = true;
    }
    return !told;
}

bool Game::client_back() {
    // The save played offline still loaded (the same seed's section), and the client that asks
    // has seen it is its seed's: the player is told the client is back, once.
    bool shown = configured_ && offline_played_ >= 0 && *save_loaded_ != kNoSaveLoaded &&
                 static_cast<size_t>(offline_played_) < offline_seeds_.size() &&
                 at<uint16_t>(rando_marker_, 0) == 1 &&
                 section_is(offline_seeds_[static_cast<size_t>(offline_played_)], rando_blob_);
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
