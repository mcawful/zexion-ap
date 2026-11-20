// The Linux helper's SteamInternal_ContextInit (native/src/feats.h, feats_context_init) in a
// program with no libsteam_api to go on to, which this test is: the game has it NEEDED, so only a
// program the preload reaches that does not has no real function. The game's accessor reads
// through what it gets (`mov rax,[rax]`; docs/re/linux.md, "Mapped by disassembly: the Feats
// guard's Steam and load test"), so the answer is never null: the stand-in's holder, an
// interface whose every method returns 0.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "feats.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

// The accessor's own reads of the answer: the holder, the interface, the vtable, a method.
bool reads_like_the_games_accessor(void* answer) {
    if (answer == nullptr) return false;
    void* interface = *static_cast<void**>(answer);  // mov rax, [rax]
    if (interface == nullptr) return false;
    void** vtable = *static_cast<void***>(interface);  // the interface's vtable
    if (vtable == nullptr) return false;
    auto method = reinterpret_cast<uint64_t (*)(void*)>(vtable[0x30 / 8]);  // SetAchievement's
    return method != nullptr && method(interface) == 0;
}

void test_no_libsteam_api_answers_the_stand_in_not_null() {
    int context[6] = {};  // any context: nothing is blocking, and no real function is found
    void* answer = zap::feats_context_init(context);
    check(answer != nullptr, "the answer is not null");
    check(reads_like_the_games_accessor(answer), "and it reads through as the accessor does");
    check(zap::feats_context_init(nullptr) == answer, "the same holder for a null context");
    zap::FeatsGuard guard;
    check(answer == guard.stand_in(), "the stand-in guard's own holder");
}

void test_the_stats_context_not_blocking_gets_the_stand_in_too() {
    int stats[6] = {};
    zap::set_feats_stats_context(stats);  // the game's context, with the guard off
    void* answer = zap::feats_context_init(stats);
    check(reads_like_the_games_accessor(answer), "the stats context with nothing to go on to");
}

}  // namespace

int main() {
    test_no_libsteam_api_answers_the_stand_in_not_null();
    test_the_stats_context_not_blocking_gets_the_stand_in_too();
    if (g_failures == 0) std::printf("feats: all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
