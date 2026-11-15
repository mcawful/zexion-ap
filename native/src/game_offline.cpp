// Game's offline look (offline.h): an AP save dressed with no client (or with a client of
// another seed), the file select's counts, and the notices.
#include "game_calls.h"

#include <algorithm>

namespace zap {

namespace {

constexpr int kOfflineFrames = 15;  // frames between looks
// Loading an AP save with no client (in capitals where drawn so: text.h, in_capitals).
constexpr char kOfflineWarning[] =
    "Archipelago client not connected. Checks are sent when it connects.";
// And once a client connects while a save was dressed offline.
constexpr char kOnlineNotice[] = "Archipelago client connected.";
// And once a load, from a client whose seed the loaded randomizer save is not.
constexpr char kOtherSeedNotice[] =
    "This save is from another seed. Nothing is sent from it, and your items wait for your "
    "seed's save.";

// One of the helper's own notices: silent, for any save.
Popup notice(const char* text) { return {kAreaMapItem, text, true}; }

// An own disk's count with no client (watch_own_disks): the helper's own, with the disk's
// pickup sound as the client's count has it; told apart by its item.
Popup disk_notice(int count, int required) {
    std::string text = std::string(kDiskName) + " " + std::to_string(count);
    if (required > 0) text += " of " + std::to_string(required);
    Popup popup{static_cast<uint8_t>(kDiskItem), std::move(text), true};
    popup.sounds = true;
    return popup;
}
bool is_disk_notice(const Popup& p) {
    return p.bare && !p.counted && p.batch == 0 && p.item == kDiskItem;
}

}  // namespace

void Game::set_offline(std::vector<OfflineSeed> seeds) {
    add_known_seeds(seeds);
    // Read again each time a pipe client goes: a dressed seed they still hold stays dressed, its
    // record as it is; undressed, its look would wait for play, ASSIST unlocked in a menu.
    int kept = -1;
    // The seed played offline this load (client_back), found again by its record (newest first).
    int played = -1;
    if (offline_played_ >= 0 && static_cast<size_t>(offline_played_) < offline_seeds_.size()) {
        const OfflineSeed& was = offline_seeds_[static_cast<size_t>(offline_played_)];
        auto same = [&](const OfflineSeed& k) { return offline_matches(k, was); };
        auto it = std::find_if(seeds.begin(), seeds.end(), same);
        if (it != seeds.end()) played = static_cast<int>(it - seeds.begin());
    }
    offline_played_ = played;
    if (offline_index_ >= 0) {
        OfflineSeed& dressed = offline_seeds_[static_cast<size_t>(offline_index_)];
        auto same = [&](const OfflineSeed& k) { return offline_matches(k, dressed); };
        auto it = std::find_if(seeds.begin(), seeds.end(), same);
        if (it != seeds.end()) {
            *it = std::move(dressed);
            kept = static_cast<int>(it - seeds.begin());
        }
    }
    if (kept < 0) leave_offline();
    offline_seeds_ = std::move(seeds);
    offline_index_ = kept;
}

void Game::merge_offline(std::vector<OfflineSeed> seeds) {
    add_known_seeds(seeds);
    // In place of its old record, or appended: the indices stay; the dressed seed's record is kept.
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

bool Game::drop_notice(const char* text) {
    // The helper's notices are the only popups no client counts (parse_batch).
    auto notice = [&](const Popup& p) { return !p.counted && p.text == text; };
    return std::erase_if(popups_, notice) > 0;
}

void Game::drop_offline_warning() {
    // Withdrawn before it showed: told again at the next dress with no client this load.
    if (drop_notice(kOfflineWarning)) offline_warned_ = false;
}

void Game::drop_client_notices() {
    // Not shown yet when its client went (or a client configures again): the other-seed notice is
    // told again to the next client that says so; CLIENT CONNECTED. no longer holds.
    drop_notice(kOtherSeedNotice);
    drop_notice(kOnlineNotice);
    other_save_ = false;
    other_warned_ = false;
}

void Game::leave_offline_seed() {
    drop_offline_warning();
    if (offline_index_ < 0) return;
    offline_index_ = -1;
    offline_count_ = -1;
    offline_told_ = -1;
    drop_save_look();
}

void Game::leave_offline() {
    leave_offline_seed();
    offline_frames_ = 0;  // looked at again the next watch (a client gone: dressed at once)
    if (!offline_file_counts_.empty()) {
        offline_file_counts_.clear();
        apply_file_counts();
    }
}

void Game::apply_file_counts() {
    if (offline_file_counts_ != files_.counts()) files_.set_counts(offline_file_counts_);
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
    // A known disk seed's save, in play or not: its count from the seed's file, until the client's
    // (or the offline look's) is set. Mid-load it may read the last save's for a frame: only the
    // branch's code byte follows, put right the next frame, and no pause screen opens mid-load.
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
        // No save: each notice comes again next load; one not shown yet was for the save left.
        drop_notice(kOfflineWarning);
        drop_client_notices();
        offline_warned_ = false;
        offline_played_ = -1;
        offline_settled_ = false;
    } else if (in_play()) {
        offline_settled_ = true;  // this load is past its loading: its marker and section read
    }
    if (offline_seeds_.empty()) {
        leave_offline();
        return;
    }
    if (offline_frames_-- > 0) return;
    offline_frames_ = kOfflineFrames;
    // Dressed with no client, or a client that said other_save; undressed when that stops.
    const bool other = active_ && other_save_;
    const bool dress = !active_ || other;
    const int seed = dress ? loaded_seed() : -1;
    // Dressed once in play since it loaded (while loading, the marker and section may be another
    // save's); undressed at once with no save, a client's look, or in play. One read as no known
    // seed with a menu up is held until play, so the lock cannot lift in the options menu on such
    // a read (none is known).
    const bool undress = !dress || !offline_settled_ || in_play();
    if (seed != offline_index_ && (seed >= 0 ? offline_settled_ : undress)) {
        leave_offline_seed();
        offline_index_ = seed;
        if (seed >= 0) {  // the look itself: below
            // Once a load and after each client goes; with a client of another seed, its notice.
            if (!offline_warned_ && !other) {
                popups_.push_back(notice(kOfflineWarning));
                offline_warned_ = true;
            }
            if (!other) offline_played_ = seed;
        }
    } else if (seed >= 0 && seed != offline_index_ && offline_index_ >= 0) {
        leave_offline_seed();  // another seed's save, not in play yet: dressed once it is
    }
    if (offline_index_ >= 0) {
        const OfflineSeed& sd = offline_seeds_[static_cast<size_t>(offline_index_)];
        // Set when dressed and kept while dressed: a client of another seed attaching in play
        // takes its own look back (no labels, ASSIST free, AREA MAP, no disks).
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
    // The file select: each known disk seed's save by its own section copy, with or without a
    // client (which keeps the seed's file, and so what every save of it shares, up to date).
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

// A cell that turns collected is a pickup only after this many looks in play in a row: the
// first looks after a load, a menu or a death only take the cells as they are (chosen; a load
// taking more than one frame to fill the grid was not measured).
constexpr int kOwnTrapPlayLooks = 2;

void Game::watch_own_traps() {
    // The loaded save's seed, by its section (loaded_seed: the marker first).
    const int seed = configured_ && *at_.save_loaded != kNoSaveLoaded ? loaded_seed() : -1;
    if (seed < 0) {
        own_traps_looked_ = false;
        own_traps_play_looks_ = 0;
        return;
    }
    const std::vector<OfflineSeed::OwnTrap>& traps = offline_seeds_[static_cast<size_t>(seed)].traps;
    std::vector<bool> collected(traps.size());
    for (size_t i = 0; i < traps.size(); ++i) {
        collected[i] = at_.map_grid[traps[i].cell] == kCollectedCell;
    }
    // The same list as the look before (a seed file merged meanwhile, or another seed's save,
    // starts over), and play in the looks before this one: each cell collected now and not then
    // was picked up in between, whatever this look is of (a pause on the pickup's frame).
    if (own_traps_looked_ && own_traps_play_looks_ >= kOwnTrapPlayLooks && traps == own_traps_seen_) {
        for (size_t i = 0; i < traps.size(); ++i) {
            if (collected[i] && !own_traps_collected_[i]) {
                own_trap_found(traps[i].item, traps[i].cell);
            }
        }
    }
    own_traps_seen_ = traps;
    own_traps_collected_ = std::move(collected);
    own_traps_looked_ = true;
    own_traps_play_looks_ = in_play() ? std::min(own_traps_play_looks_ + 1, kOwnTrapPlayLooks) : 0;
}

void Game::watch_own_disks() {
    // Watched with a client or none, so a disk picked up with one is not taken for a pickup
    // once it goes; queued only with none: with a client its follow-up shows the count (and
    // knows the disks received since the seed's file was written).
    const int seed = configured_ && *at_.save_loaded != kNoSaveLoaded ? loaded_seed() : -1;
    const OfflineSeed* sd = seed >= 0 ? &offline_seeds_[static_cast<size_t>(seed)] : nullptr;
    if (sd == nullptr || !sd->discs) {
        if (own_disks_looked_) std::erase_if(popups_, is_disk_notice);
        own_disks_looked_ = false;
        own_disks_play_looks_ = 0;
        return;
    }
    const std::vector<int>& cells = sd->unfound;  // every own disk's cell (client/ticks.py)
    std::vector<bool> collected(cells.size());
    for (size_t i = 0; i < cells.size(); ++i) {
        collected[i] = cells[i] >= 0 && cells[i] < kMapCells &&
                       at_.map_grid[cells[i]] == kCollectedCell;
    }
    if (own_disks_looked_ && cells == own_disks_seen_) {
        bool found = false, undone = false;
        for (size_t i = 0; i < cells.size(); ++i) {
            found = found || (collected[i] && !own_disks_collected_[i]);
            undone = undone || (!collected[i] && own_disks_collected_[i]);
        }
        // A load put a disk back: the counts queued are no longer the save's.
        if (undone) std::erase_if(popups_, is_disk_notice);
        // One notice a look, with the count as it is now (two disks in one frame: one notice).
        // One queued stays when a client attaches: the pickup was the helper's to count (the
        // client takes the disks a save holds at its first look for counted).
        if (found && !active_ && own_disks_play_looks_ >= kOwnTrapPlayLooks) {
            const bool told = at_.flags[flag::kScannerTold] != 0;
            popups_.push_back(
                disk_notice(offline_disk_count(*sd, at_.map_grid), told ? sd->required : 0));
        }
    } else if (own_disks_looked_) {
        std::erase_if(popups_, is_disk_notice);  // another seed's save, or its file merged
    }
    own_disks_seen_ = cells;
    own_disks_collected_ = std::move(collected);
    own_disks_looked_ = true;
    own_disks_play_looks_ = in_play() ? std::min(own_disks_play_looks_ + 1, kOwnTrapPlayLooks) : 0;
}

void Game::own_trap_found(uint8_t item, int cell) {
    std::shared_ptr<const OfflineSeed> origin;
    if (rando_save_loaded()) {
        origin = std::make_shared<const OfflineSeed>(offline_seed_of(at_.rando_blob));
    }
    traps_.push_back({item, origin, cell});
}

std::string Game::other_save() {
    // Only an AP save of another seed: one with a seed file (every ARCHIPELAGO new game is made
    // with a client, which leaves one). A plain randomizer game is left alone.
    if (!configured_ || loaded_seed() < 0) return "unknown";
    other_save_ = true;
    if (other_warned_) return "already told";
    drop_offline_warning();
    popups_.push_back(notice(kOtherSeedNotice));
    other_warned_ = true;
    return "told";
}

bool Game::client_back() {
    bool shown = configured_ && offline_played_ >= 0 && rando_save_loaded() &&
                 static_cast<size_t>(offline_played_) < offline_seeds_.size() &&
                 section_is(offline_seeds_[static_cast<size_t>(offline_played_)], at_.rando_blob);
    offline_played_ = -1;
    if (shown) popups_.push_back(notice(kOnlineNotice));
    return shown;
}

std::string Game::offline_state() const {
    if (offline_seeds_.empty()) return "-";
    std::string out = std::to_string(offline_seeds_.size());
    if (offline_index_ >= 0) out += ":" + offline_seeds_[static_cast<size_t>(offline_index_)].name;
    return out;
}

}  // namespace zap
