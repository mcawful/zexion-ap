#include "mailbox.h"

#include <exception>

namespace zap {

Mailbox::Mailbox() { done_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); }

Mailbox::~Mailbox() {
    if (done_ != nullptr) CloseHandle(done_);
}

bool Mailbox::call(std::function<std::string()> work, DWORD timeout_ms, std::string& result) {
    if (done_ == nullptr) {
        result = "error no event";
        return true;
    }
    work_ = std::move(work);
    ResetEvent(done_);
    state_.store(kPending, std::memory_order_release);
    if (WaitForSingleObject(done_, timeout_ms) != WAIT_OBJECT_0) {
        int expected = kPending;
        if (state_.compare_exchange_strong(expected, kEmpty, std::memory_order_acq_rel)) {
            work_ = nullptr;  // withdrawn before any frame took it
            return false;
        }
        // A frame took it just now: its run is short, wait for it to finish.
        WaitForSingleObject(done_, INFINITE);
    }
    result = std::move(result_);
    work_ = nullptr;
    state_.store(kEmpty, std::memory_order_release);
    return true;
}

void Mailbox::serve() {
    int expected = kPending;
    if (!state_.compare_exchange_strong(expected, kRunning, std::memory_order_acq_rel)) return;
    try {
        result_ = work_();
    } catch (const std::exception& e) {
        result_ = std::string("error ") + e.what();
    } catch (...) {
        result_ = "error exception";
    }
    state_.store(kDone, std::memory_order_release);
    SetEvent(done_);
}

}  // namespace zap
