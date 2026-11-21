#include "host_offline.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace zap {

std::string one_word(std::string text) {
    std::replace(text.begin(), text.end(), ' ', '_');
    return text;
}

void HostOffline::run_self_scan() {
    try {
        scan_found_ = {};
        scan_why_ = find::find_in_image(GetModuleHandleW(nullptr), scan_found_);
        if (!scan_why_.empty()) {
            scan_note_ = scan_why_;
        } else if (!scan_found_.failed.empty()) {
            scan_note_ = "failed " + scan_found_.failed.front();  // taken or refused: take()
        } else {
            scan_note_ = "ok";
        }
    } catch (...) {
        scan_found_.rvas.clear();
        scan_found_.failed.clear();
        scan_why_ = "scan_failed";
        scan_note_ = "scan_failed";
    }
}

std::wstring HostOffline::offline_dir() const {
    return test_build_ ? restrict_to_test_folder(offline_folder()) : offline_folder();
}

std::wstring HostOffline::save_dir() const {
    if (!test_build_) return build_ == nullptr ? std::wstring() : save_folder();
    wchar_t folder[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ZAP_TEST_SAVE_DIR", folder, MAX_PATH);
    return n != 0 && n < MAX_PATH ? restrict_to_test_folder(std::wstring(folder, n))
                                  : std::wstring();
}

void HostOffline::note_error(std::string error) {
    std::lock_guard<std::mutex> lock(error_lock_);
    error_ = std::move(error);
}

void HostOffline::read_files(bool write) {
    const std::vector<OfflineSeed> saved = saved_.get();
    OfflineFiles files = read_offline_files(offline_dir(), build_, save_dir(), write, &saved);
    note_error(files.error);
    found_.store(static_cast<int>(files.seeds.size()));
    inbox_.set_files(std::move(files));
}

std::string HostOffline::put(const std::string& request) {
    // Written by the helper: a packaged client's AppData writes are redirected (offline.h). The
    // seed's copy in the game's save folder too (offline.h): Steam Cloud carries it.
    OfflineSeed seed;
    std::string save_error;
    const std::vector<OfflineSeed> saved = saved_.get();
    std::string reply =
        put_offline_file(offline_dir(), build_, request, &seed, save_dir(), &save_error, &saved);
    if (!save_error.empty()) note_error("save folder copy: " + save_error);
    if (!seed.name.empty()) inbox_.put_seed(std::move(seed), reply == "stored" || reply == "unchanged");
    return reply;
}

// Game thread: with no client yet, the helper configured (inactive: only what runs with no client
// uses it) from its own scan's addresses if it found every one the game needs; else, from the
// cached addresses for this build, if they check out against the game (client/offline.py wrote
// them). Nothing from a scan that missed a required address: those are the cache's or the
// client's.
void HostOffline::configure_without_client(const OfflineFiles& files) {
    HMODULE exe = GetModuleHandleW(nullptr);
    if (scan_why_.empty()) {
        std::string why = game_.configure(exe, scan_found_.rvas, false);
        if (why.empty()) {
            addresses_ = "scan";
            // Only optional keys can be missing from an accepted scan (kConfigKeys).
            const std::string lacking = find::failed_names(scan_found_);
            scan_note_ = lacking.empty() ? "ok" : "ok without " + lacking;
            return;
        }
        scan_note_ = "refused " + find::failure_of(scan_found_, why);
    }
    if (files.config.empty()) return;
    std::string why = check_offline_cache(exe, files);
    if (why.empty()) why = game_.configure(exe, files.config, false);
    if (why.empty()) {
        addresses_ = "cache";
        return;
    }
    // The game's thread never waits for the lock (a writer holds it only to swap a string): an
    // error it cannot record now is dropped.
    if (error_lock_.try_lock()) {
        error_ = "cache: " + why;
        error_lock_.unlock();
    }
}

void HostOffline::take() {
    inbox_.take(game_, [this](const OfflineFiles& files) {
        reads_.fetch_add(1);
        if (!game_.configured()) configure_without_client(files);
    });
}

void HostOffline::snapshot_saves() {
    if ((frames_++ & 15u) != 0) return;
    saved_.publish(game_.saved_seeds());
}

std::string HostOffline::client_configured(const std::map<std::string, uint64_t>& rvas) {
    addresses_ = "client";
    // The client's addresses take over as they are; a key where they differ from the helper's own
    // is a bug in one scan.
    return scan_why_.empty() ? find::differing_keys(rvas, scan_found_.rvas) : "";
}

std::string HostOffline::scan_fields() const {
    return " scan=" + one_word(scan_note_) + " addresses=" + addresses_;
}

std::string HostOffline::read_fields() const {
    return " reads=" + std::to_string(reads_.load()) + "/" + std::to_string(found_.load());
}

std::string HostOffline::error_field() const {
    std::lock_guard<std::mutex> lock(error_lock_);
    return error_.empty() ? "" : " read_error=" + one_word(error_);
}

DWORD WINAPI HostOffline::copy_thread(void* self) {
    static_cast<HostOffline*>(self)->copy_loop();
    return 0;
}

void HostOffline::copy_loop() {
    uint64_t done = 0;
    while (!copy_stop_.load()) {
        WaitForSingleObject(copy_wake_, 250);
        if (copy_stop_.load()) break;
        const uint64_t generation = saved_.generation();
        if (generation == done) continue;
        done = generation;
        try {
            const std::vector<OfflineSeed> saved = saved_.get();
            OfflineFiles files = read_offline_files(offline_dir(), build_, save_dir(), true, &saved);
            if (!files.error.empty()) note_error(files.error);
        } catch (...) {
            // Nothing may end this thread but being asked to.
        }
    }
}

void HostOffline::start_copy_thread() {
    if (copy_thread_ != nullptr) {
        // One that is still running is the one to keep. One that is gone (a stop that timed out and
        // was not taken back, or one that ended on its own) leaves its handles: closed and cleared,
        // so a new thread starts in its place, not none ever again.
        if (WaitForSingleObject(copy_thread_, 0) != WAIT_OBJECT_0) return;
        CloseHandle(copy_thread_);
        CloseHandle(copy_wake_);
        copy_thread_ = nullptr;
        copy_wake_ = nullptr;
    }
    copy_stop_.store(false);
    copy_wake_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (copy_wake_ == nullptr) return;
    copy_thread_ = CreateThread(nullptr, 0, &HostOffline::copy_thread, this, 0, nullptr);
    if (copy_thread_ == nullptr) {
        CloseHandle(copy_wake_);
        copy_wake_ = nullptr;
    }
}

bool HostOffline::stop_copy_thread(DWORD milliseconds, bool resume) {
    if (copy_thread_ == nullptr) return true;
    copy_stop_.store(true);
    SetEvent(copy_wake_);
    if (WaitForSingleObject(copy_thread_, milliseconds) != WAIT_OBJECT_0) {
        if (resume) {
            copy_stop_.store(false);
            ResetEvent(copy_wake_);
        }
        // It may have seen the stop just before: then it is gone, and the caller may go on.
        if (WaitForSingleObject(copy_thread_, 0) != WAIT_OBJECT_0) return false;
    }
    CloseHandle(copy_thread_);
    CloseHandle(copy_wake_);
    copy_thread_ = nullptr;
    copy_wake_ = nullptr;
    return true;
}

}  // namespace zap
