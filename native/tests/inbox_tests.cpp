// The seeds the serving thread hands the game's thread (offline_inbox.h), on both platforms: the
// files read replace the game's seeds, the seeds put are added, and a seed put that its file could
// not keep stays the helper's until the game closes.

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "game_calls.h"
#include "offline_inbox.h"

namespace {

zap::OfflineSeed seed_of(const char* name, uint8_t variant) {
    zap::OfflineSeed seed;
    seed.name = name;
    seed.table[0] = variant;  // a seed is told from another by its table and start
    return seed;
}

zap::OfflineFiles files_of(std::vector<zap::OfflineSeed> seeds) {
    zap::OfflineFiles files;
    files.seeds = std::move(seeds);
    return files;
}

void test_files_replace_and_puts_are_added() {
    zap::Game game;
    zap::OfflineInbox inbox;
    int heard = 0;
    auto count = [&](const zap::OfflineFiles&) { ++heard; };
    check(!inbox.take(game, count) && heard == 0 && game.offline_state() == "-",
          "nothing waiting: nothing taken");
    inbox.set_files(files_of({seed_of("A.txt", 1), seed_of("B.txt", 2)}));
    check(inbox.take(game, count) && heard == 1 && game.offline_state() == "2",
          "files read are taken once, and told to the host first");
    check(!inbox.take(game, count) && heard == 1, "taken once");
    inbox.set_files(files_of({seed_of("C.txt", 3)}));
    inbox.take(game, count);
    check(game.offline_state() == "1", "with no client, files replace the seeds");
    inbox.put_seed(seed_of("D.txt", 4), true);
    inbox.put_seed(seed_of("D.txt", 4), true);  // put again, no frame since: one in the queue
    check(!inbox.take(game, count) && heard == 2 && game.offline_state() == "2",
          "a seed put is added, once, with no files taken");
}

void test_a_seed_not_kept_stays_until_the_game_closes() {
    zap::Game game;
    zap::OfflineInbox inbox;
    inbox.put_seed(seed_of("A.txt", 1), false);  // its file could not be written
    inbox.put_seed(seed_of("B.txt", 2), true);
    inbox.take(game);
    check(game.offline_state() == "2", "both known once put");
    // The client goes and the files are read again: B is in the folder, A is not.
    inbox.set_files(files_of({seed_of("B.txt", 2)}));
    inbox.take(game);
    check(game.offline_state() == "2", "read again: the one not kept is still the helper's");
    inbox.set_files(files_of({}));
    inbox.take(game);
    check(game.offline_state() == "1", "and only it: B's file was taken away");
    // A put of the same name that is kept makes the folder's file the seed's again.
    inbox.put_seed(seed_of("A.txt", 1), true);
    inbox.take(game);
    inbox.set_files(files_of({}));
    inbox.take(game);
    check(game.offline_state() == "-", "kept at last: no longer the helper's own");
}

}  // namespace

int main() {
    test_files_replace_and_puts_are_added();
    test_a_seed_not_kept_stays_until_the_game_closes();
    return report("inbox tests");
}
