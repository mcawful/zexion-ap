// What the serving thread hands the game's thread of the seeds, for both helpers: the files it
// read (at start, and each time a client goes), and the seeds a client has put (`offline_put`).
// The next frame takes both (take). A seed put that its file could not keep (no folder, a write
// that failed) is the helper's until the game closes: added again after each read of the files
// replaces the seeds, as a seed in the folder is.
#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include "offline.h"

namespace zap {

class Game;

class OfflineInbox {
public:
    // Serving thread: the files read, replacing any not yet taken.
    void set_files(OfflineFiles files);
    // Serving thread: a seed a client has just put, `kept` once its file was written ("stored",
    // "unchanged"). One of the same name still waiting is replaced (no frame since: the game in
    // the background), so the queue holds one a seed however often a client puts it, and the
    // newest is always last: the client's own seed is the last put.
    void put_seed(OfflineSeed seed, bool kept);

    // Game thread: takes what is new, never waiting for the lock (the serving thread holds it only
    // to swap the files in). `before_files` gets the files first (the Windows helper's own
    // configuration with no client is made from them). With no client the files replace the
    // game's seeds; with one they are added to them (Game::set_offline, merge_offline). The seeds
    // put follow, the client's own noted (Game::note_client_seed: the look its seed applies in).
    // Whether files were taken.
    bool take(Game& game, const std::function<void(const OfflineFiles&)>& before_files = {});

private:
    struct Put {
        OfflineSeed seed;
        bool kept;
    };
    std::mutex lock_;
    std::optional<OfflineFiles> files_;  // read, not yet taken
    std::vector<Put> put_;               // put since, not taken
    std::vector<OfflineSeed> unkept_;    // game thread only: put and not kept, still the helper's
};

}  // namespace zap
