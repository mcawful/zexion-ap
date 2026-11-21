// mailbox.h for the game's Linux build (the Windows one: mailbox.cpp): the same hand-off, its
// event a condition variable.
#include <chrono>
#include <exception>
#include <thread>

#include "mailbox.h"

namespace zap {

Mailbox::Mailbox() = default;
Mailbox::~Mailbox() = default;

bool Mailbox::call(std::function<std::string()> work, DWORD timeout_ms, std::string& result) {
    work_ = std::move(work);
    {
        std::lock_guard<std::mutex> lock(lock_);
        signaled_ = false;
    }
    state_.store(kPending, std::memory_order_release);
    try {
        std::unique_lock<std::mutex> lock(lock_);
        if (!done_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [this] { return signaled_; })) {
            int expected = kPending;
            if (state_.compare_exchange_strong(expected, kEmpty, std::memory_order_acq_rel)) {
                work_ = nullptr;  // withdrawn before any frame took it
                return false;
            }
            // A frame took it just now: its run is short, wait for it to finish.
            done_.wait(lock, [this] { return signaled_; });
        }
    } catch (...) {
        // Never left posted when this call unwinds: `work` refers to the caller's frame. Taken
        // back if no frame has it, else waited for (without the lock), then thrown on.
        int expected = kPending;
        if (!state_.compare_exchange_strong(expected, kEmpty, std::memory_order_acq_rel)) {
            while (state_.load(std::memory_order_acquire) != kDone) std::this_thread::yield();
        }
        work_ = nullptr;
        state_.store(kEmpty, std::memory_order_release);
        throw;
    }
    result = std::move(result_);
    work_ = nullptr;
    state_.store(kEmpty, std::memory_order_release);
    return true;
}

void Mailbox::signal_done() {
    {
        std::lock_guard<std::mutex> lock(lock_);
        signaled_ = true;
    }
    done_.notify_all();
}

}  // namespace zap
