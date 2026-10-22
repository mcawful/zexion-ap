// Hands one piece of work at a time from the pipe thread to the game's thread.
//
// The pipe thread posts work and waits for its result. The game's frame call takes the work at
// its next frame, runs it, and posts the result. If no frame takes the work within the timeout,
// the pipe thread withdraws it, and it is never run afterwards: a frame either took it before
// the withdrawal (and the pipe thread waits for that run to finish) or never will.
#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <string>

namespace zap {

class Mailbox {
public:
    Mailbox();
    ~Mailbox();
    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;

    // Pipe thread. Runs `work` on the next frame and returns true with its result, or returns
    // false if no frame took it within `timeout_ms` (it then never runs). One caller at a time.
    bool call(std::function<std::string()> work, DWORD timeout_ms, std::string& result);

    // Game thread, once per frame. Never blocks: one atomic exchange when nothing is posted.
    // An exception in the work becomes an "error ..." result.
    void serve();

private:
    enum State : int { kEmpty, kPending, kRunning, kDone };
    std::atomic<int> state_{kEmpty};
    std::function<std::string()> work_;
    std::string result_;
    HANDLE done_ = nullptr;
};

}  // namespace zap
