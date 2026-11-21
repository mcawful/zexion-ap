#include "offline_inbox.h"

#include <algorithm>
#include <utility>

#include "game_calls.h"

namespace zap {

void OfflineInbox::set_files(OfflineFiles files) {
    std::lock_guard<std::mutex> lock(lock_);
    files_ = std::move(files);
}

void OfflineInbox::put_seed(OfflineSeed seed, bool kept) {
    std::lock_guard<std::mutex> lock(lock_);
    std::erase_if(put_, [&](const Put& p) { return p.seed.name == seed.name; });
    put_.push_back({std::move(seed), kept});
}

bool OfflineInbox::take(Game& game, const std::function<void(const OfflineFiles&)>& before_files) {
    std::optional<OfflineFiles> files;
    std::vector<Put> put;
    if (lock_.try_lock()) {
        files = std::exchange(files_, std::nullopt);
        put = std::exchange(put_, {});
        lock_.unlock();
    }
    if (files) {
        if (before_files) before_files(*files);
        if (game.active()) {
            game.merge_offline(std::move(files->seeds));
        } else {
            game.set_offline(std::move(files->seeds));
            if (!unkept_.empty()) game.merge_offline(unkept_);
        }
    }
    std::vector<OfflineSeed> seeds;
    for (Put& p : put) {
        // The newest put of a name is the helper's own copy, until a put that was kept: then the
        // folder's is, and read again from it.
        std::erase_if(unkept_, [&](const OfflineSeed& s) { return s.name == p.seed.name; });
        if (!p.kept) unkept_.push_back(p.seed);
        seeds.push_back(std::move(p.seed));
    }
    if (!seeds.empty()) {
        // The client puts its own seed alone: the one its look applies in. Noted after the
        // client-gone work of this frame (a put from a client that has gone is ignored).
        try {
            game.note_client_seed(seeds.back());
        } catch (...) {  // a throw skips no other work
        }
        game.merge_offline(std::move(seeds));  // after the files, read earlier
    }
    return files.has_value();
}

}  // namespace zap
