// Host tests for the helper's parts that need no game: the requests' parsers, the popup text's
// layout, and the mailbox between the pipe thread and the frame call.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>

#include "check.h"
#include "mailbox.h"
#include "requests.h"
#include "text.h"

namespace {

void test_parsers() {
    std::map<std::string, uint64_t> rvas;
    check(zap::parse_config("config a=0x10 b=32", rvas).empty() && rvas["a"] == 16 &&
              rvas["b"] == 32,
          "config takes hex and decimal");
    check(!zap::parse_config("config a=zz", rvas).empty(), "config refuses a bad number");
    check(!zap::parse_config("hello", rvas).empty(), "config refuses another request");

    zap::Batch batch;
    check(zap::parse_batch("apply\ncas 36 0 1\nrefresh\npopup 36 from Alice ü!", batch).empty(),
          "parses a batch");
    check(batch.cas.size() == 1 && batch.cas[0].item == 36 && batch.cas[0].value == 1, "cas");
    check(batch.refresh, "refresh");
    check(batch.popups.size() == 1 && batch.popups[0].suffix == "FROM ALICE !",
          "the suffix is upper-cased and filtered to the font");
    zap::Batch bad;
    check(!zap::parse_batch("apply\ncas 50 0 1", bad).empty(), "item 50 is not writable");
    check(!zap::parse_batch("apply\ncas 3 0 256", bad).empty(), "values are bytes");
    check(!zap::parse_batch("apply\nwrite 3 0 1", bad).empty(), "unknown lines are refused");
    check(!zap::parse_batch("apply\npopup 51 X", bad).empty(), "popup items are 0..50");
    zap::Batch longer;
    zap::parse_batch("apply\npopup 1 " + std::string(100, 'a'), longer);
    check(longer.popups[0].suffix.size() == 40, "the suffix is capped");
    zap::Batch notice;
    check(zap::parse_batch("apply\nnotice 49 Hint: Phaser at Machinus 29,30", notice).empty() &&
              notice.popups.size() == 1 && notice.popups[0].bare &&
              notice.popups[0].suffix == "HINT: PHASER AT MACHINUS 29,30",
          "a notice keeps its colon and has no head");
    zap::Batch plain;
    zap::parse_batch("apply\npopup 49 Hint: X", plain);
    check(!plain.popups[0].bare && plain.popups[0].suffix == "HINT X",
          "a popup's suffix still drops the colon");
    zap::Batch empty_notice;
    check(!zap::parse_batch("apply\nnotice 49 ~~", empty_notice).empty(), "an empty notice is refused");
}

void test_newgame_parser() {
    zap::NewGamePlan plan;
    check(zap::parse_newgame("ap_newgame\nexpect 0x6C0 0300\nwrite 620 2E2E\nwrite 0x310 010203040506",
                             plan)
                  .empty(),
          "parses a new-game plan");
    check(plan.expect.size() == 1 && plan.expect[0].offset == 0x6C0 && plan.expect[0].bytes.size() == 2,
          "expect");
    check(plan.write.size() == 2 && plan.write[0].offset == 620 && plan.write[1].bytes[5] == 6,
          "writes, decimal and hex offsets");
    zap::NewGamePlan bad;
    check(!zap::parse_newgame("ap_newgame\nwrite 0x906 112233", bad).empty(),
          "a write past the section is refused");
    check(!zap::parse_newgame("ap_newgame\nwrite 0x10 123", bad).empty(), "odd hex is refused");
    check(!zap::parse_newgame("ap_newgame\nexpect 0x10 00", bad).empty(), "nothing to write");
    check(!zap::parse_newgame("ap_newgame\npoke 0x10 00", bad).empty(), "unknown lines");
}

void test_popup_text_breaks_before_the_sender() {
    int width = 0;
    check(zap::popup_text("\xC5MISSILE BOX", "FROM ALICE", 24, &width) ==
                  "\xC5MISSILE BOX FROM ALICE" &&
              width == 12,
          "the sender starts the second line, centered in the widest line, which sets the width");
    check(zap::popup_text("\xC5MISSILE BOX", "", 24) == "\xC5MISSILE BOX", "no sender: one line");
    std::string head(24, 'A');
    check(zap::popup_text(head, "FROM BOB", 24, &width) == head + std::string(8, ' ') + "FROM BOB" &&
              width == 24,
          "a head that fills its line needs no padding");
    std::string wrapped = zap::popup_text("X", "FROM A VERY LONG PLAYER NAME HERE", 24, &width);
    check(wrapped == std::string(11, ' ') + "X" + std::string(11, ' ') +
                         "FROM A VERY LONG PLAYER" + std::string(7, ' ') + "NAME HERE" &&
              width == 23,
          "a long sender wraps at a word, not inside one");
}

void test_mailbox() {
    zap::Mailbox box;
    std::string result;
    std::atomic<bool> ran{false};
    check(!box.call([&] { ran = true; return std::string("x"); }, 20, result),
          "no frame: the call times out");
    box.serve();
    check(!ran, "withdrawn work never runs later");

    std::atomic<bool> stop{false};
    std::atomic<int> runs{0};
    std::thread frames([&] {
        while (!stop) {
            box.serve();
        }
    });
    int answered = 0;
    for (int i = 0; i < 20000; ++i) {
        if (box.call([&] { ++runs; return std::string("ok"); }, i % 3, result)) {
            if (result == "ok") ++answered;
        }
    }
    stop = true;
    frames.join();
    box.serve();
    check(runs == answered, "every answered call ran exactly once, and no other did");

    std::thread one([&] { Sleep(10); box.serve(); });
    check(box.call([] { throw 1; return std::string(); }, 1000, result) && result == "error exception",
          "an exception in the work becomes an error, not a crash");
    one.join();
}

}  // namespace

int main() {
    test_parsers();
    test_newgame_parser();
    test_popup_text_breaks_before_the_sender();
    test_mailbox();
    return report("requests tests");
}
