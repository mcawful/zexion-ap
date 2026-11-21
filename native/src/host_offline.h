// The Windows helper's seed files, its own scan and the thread that writes the seeds' copies,
// apart from the frame hook (dllmain.cpp): one object with the state those share, under one rule
// for each part. Which thread touches what: the helper's thread (the scan, the first read, the copy
// thread's start), the pipe thread (puts, the read after each disconnect, `hello`), the game's
// thread (take, snapshot_saves, the scan's notes) and the copy thread (read_files for its pass).
#pragma once

#include <windows.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>

#include "findaddr.h"
#include "game_calls.h"
#include "offline.h"
#include "offline_inbox.h"

namespace zap {

// A hello field kept to one word: spaces (a seed file's name, a reason) as underscores.
std::string one_word(std::string text);

class HostOffline {
public:
    explicit HostOffline(Game& game) : game_(game) {}

    // The two folders the files live in, chosen by the target at compile time (dllmain.cpp: the
    // release's are the real ones, the hook-test build's only a test's own, through
    // restrict_to_test_folder), so the release DLL holds none of the test build's rules. `save`
    // is given the build (null: a program that is not the game, which has no save folder).
    struct Folders {
        std::wstring (*offline)();
        std::wstring (*save)(const char* build);
    };

    // The build the helper hooked, once known (null: a program that is not the game, which takes
    // no seed), and where the files go.
    void set_build(const char* build, const Folders& folders) {
        build_ = build;
        folders_ = folders;
    }

    // Helper's thread: the helper's own scan of the game's code for its addresses (findaddr.h).
    // Made after the hook and before the first read of the files and the pipe, so every reader (the
    // game's thread taking those files, a `config` request, `hello`) comes after it. Never in
    // DllMain: the thread starts after the loader lock is released. Everything that allocates is
    // inside the try, and the catch allocates nothing (an assignment of a short literal to a
    // string, `clear`), so a failed allocation cannot escape: it would skip the files and the
    // pipe, leaving the helper hooked, unreachable and unable to unload.
    void run_self_scan();

    // The files read (the helper's own folder's and the save folder's copies), for the next frame:
    // from the helper's thread at start and the pipe thread after each disconnect, and by the
    // copy thread's pass. `write` false only makes the seeds one, writing nothing (a client that
    // set nothing went).
    void read_files(bool write);

    // Pipe thread: an `offline_put` request: the reply. A seed that reads right is the running
    // helper's from the next frame whether its file could be kept or not.
    std::string put(const std::string& request);

    // Game thread, each frame: the files read since and the seeds put, taken into the game, and
    // with no client the helper configured from its own scan's addresses (else the cache's).
    void take();
    // Game thread, the first frame and every 16th (cheap: 30 slots' markers and sections): the
    // slots' seeds published for the other threads; a change wakes the copy thread.
    void snapshot_saves();

    // Game thread: a client's `config` took over, its addresses as they are. "" or the keys where
    // they differ from the helper's own scan: a bug in one scan, said so it is seen
    // (client/native.py logs it).
    std::string client_configured(const std::map<std::string, uint64_t>& rvas);

    // `hello`'s fields: the scan and where the configuration came from (game thread), and the
    // reads taken and the seeds the last one found, then the last error, if any (any thread).
    std::string scan_fields() const;
    std::string read_fields() const;
    std::string error_field() const;

    // The copy thread: when the game's slots change (a save made, a new game, a deleted save) it
    // reads the seed files and writes the copy of each seed a slot now uses into the save folder:
    // the case of a seed a client put before the game's first save, which the put could not
    // copy, whatever the client does after (it may be connected till the game closes). Never the
    // frame call. Polls the slots' generation every 250 ms. Started at the helper's start, again
    // after a refused unload stopped it, and at each request, which finds one that went since a
    // stop timed out; a running one is the one to keep.
    void start_copy_thread();
    // Stops it and waits at most `milliseconds`: true if it is gone (or there was none). It may be
    // inside a file call that does not return (an antivirus filter, a cloud placeholder on the
    // save folder), and the DLL must not be freed under it: on false it is asked to go on as before
    // (it was not stopped), and the caller does not unload. With `resume` false (the helper's last
    // wait, which is not followed by any unload) the stop stays asked for: the thread ends when
    // its call returns, and the DLL, which stays loaded, is never freed under it.
    bool stop_copy_thread(DWORD milliseconds, bool resume = true);

private:
    std::wstring offline_dir() const;
    std::wstring save_dir() const;
    void configure_without_client(const OfflineFiles& files);
    void note_error(std::string error);
    static DWORD WINAPI copy_thread(void* self);
    void copy_loop();

    Game& game_;
    const char* build_ = nullptr;
    Folders folders_{};

    OfflineInbox inbox_;
    // The seeds of the game's randomizer saves, as its memory holds the slots (Game::saved_seeds),
    // published from the frame call and read by the pipe thread and the copy thread: a seed's copy
    // goes into the save folder only while a slot here uses it, and no save file of the game is
    // opened (the game opens one to write with no share mode, so a read open of ours could fail
    // its save).
    SavedSeeds saved_;
    uint32_t frames_ = 0;  // game thread: snapshot_saves' count

    // The scan's result, kept for the client's `config` to compare with; `scan_why_` is "" once it
    // ran, else why it did not (the hook-test build, a build the helper does not know). The notes
    // are the game thread's, from the scan on: `hello`'s scan and addresses fields
    // (native/README.md, "Finding its own addresses").
    std::string scan_why_ = "not run";
    find::Found scan_found_;
    std::string scan_note_ = "-";
    std::string addresses_ = "-";  // "scan", "cache" or "client": where the configuration came from

    // For `hello`: the reads taken by a frame, the seeds the last one found, and its error (a
    // write here may come from any thread, and the game's never waits for the lock).
    std::atomic<int> reads_{0};
    std::atomic<int> found_{-1};
    mutable std::mutex error_lock_;
    std::string error_;

    std::atomic<bool> copy_stop_{false};
    HANDLE copy_thread_ = nullptr;
    HANDLE copy_wake_ = nullptr;  // set to stop it at once (else it looks every 250 ms)
};

}  // namespace zap
