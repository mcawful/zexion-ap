// Mailbox::serve, the game thread's half, the same on both platforms (call and signal_done are
// mailbox.cpp's and mailbox_linux.cpp's).
#include <exception>

#include "mailbox.h"

namespace zap {

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
    signal_done();
}

}  // namespace zap
