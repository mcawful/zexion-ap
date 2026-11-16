// A list of map cells watched for turning collected (or put back) between looks: what
// Game::watch_own_traps and Game::watch_own_disks share (native/README.md, "Source map").
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace zap {

// A cell that turns collected is a pickup only after this many looks in play in a row: the first
// looks after a load, a menu or a death only take the cells as they are (chosen; a load taking
// more than one frame to fill the grid was not measured).
constexpr int kCellWatchPlayLooks = 2;

// `Key` is what each watched cell is listed by (its cell, or the cell and what it holds): two
// looks are of the same list when their keys are equal one by one. Kept between frames, so a
// look allocates only when the list grows.
template <class Key>
class CellWatch {
public:
    // A look at `keys`, `collected(i)` telling whether the cell of keys[i] reads collected now,
    // `in_play` whether the game is in play on this look. Returns whether the look before it
    // was of the same list (false on the first look since reset(), and when the list changed:
    // `looked` is read before the look to tell the two apart). `found()` and `undone()` are then
    // the indices whose cell turned collected, or no longer is, since that look; `found()` stays
    // empty until kCellWatchPlayLooks looks in play in a row came before this one (`undone()`
    // does not wait for them). Both are empty when the list is not the same.
    template <class Collected>
    bool look(const std::vector<Key>& keys, Collected collected, bool in_play) {
        found_.clear();
        undone_.clear();
        now_.resize(keys.size());
        for (size_t i = 0; i < keys.size(); ++i) now_[i] = collected(i);
        const bool same = looked_ && keys == seen_;
        if (same) {
            const bool settled = play_looks_ >= kCellWatchPlayLooks;
            for (size_t i = 0; i < keys.size(); ++i) {
                if (now_[i] && !before_[i]) {
                    if (settled) found_.push_back(i);
                } else if (!now_[i] && before_[i]) {
                    undone_.push_back(i);
                }
            }
        } else {
            seen_ = keys;  // another list: starts over
        }
        before_.swap(now_);
        looked_ = true;
        play_looks_ = in_play ? std::min(play_looks_ + 1, kCellWatchPlayLooks) : 0;
        return same;
    }

    // Starts over: the save left, or a seed that lists none (the next look takes the cells as
    // they are).
    void reset() {
        looked_ = false;
        play_looks_ = 0;
    }

    // Whether a look has been taken since the last reset().
    bool looked() const { return looked_; }
    const std::vector<size_t>& found() const { return found_; }
    const std::vector<size_t>& undone() const { return undone_; }

private:
    std::vector<Key> seen_;       // the list as last looked at
    std::vector<char> before_;    // each of its cells collected at that look
    std::vector<char> now_;       // scratch for the look being taken
    std::vector<size_t> found_, undone_;
    bool looked_ = false;
    int play_looks_ = 0;  // looks in a row before this one that were in play, at most the limit
};

}  // namespace zap
