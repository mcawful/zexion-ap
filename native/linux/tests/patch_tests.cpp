// The Linux build's code patches run side by side with the game's bytes they replace: the map's
// on-screen write (native/src/mapmarks.h, seen_first_test_patch and seen_second_test_patch),
// each of its two tests of the item's ID run in a harness that gives it the ID as the game's code
// does and reports which of the game's two writes it reaches, for every ID.
#include <sys/mman.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "mapmarks.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

// B1's two tests, as the game has them (PowerUpMoveProc, 0x53DB6C and 0x53DC7E), and how far
// past each its minor write is.
constexpr BYTE kTests[2][zap::kSeenTestSize] = {
    {0x66, 0x83, 0xF8, 0x02, 0x7E, 0x60, 0x48, 0x8B, 0x45, 0xE8, 0x0F, 0xB7,
     0x80, 0xE8, 0x01, 0x00, 0x00, 0x66, 0x83, 0xF8, 0x23, 0x7F, 0x4F},
    {0x66, 0x83, 0xF8, 0x02, 0x7E, 0x5D, 0x48, 0x8B, 0x45, 0xE8, 0x0F, 0xB7,
     0x80, 0xE8, 0x01, 0x00, 0x00, 0x66, 0x83, 0xF8, 0x23, 0x7F, 0x4C}};
constexpr size_t kMinor[2] = {0x66, 0x63};

using Test = int (*)(const void* object);

// A function running `test` as the game does: the object in [rbp-0x18], its ID (+0x1E8) in ax;
// 1 at the major's write (right after the test), 0 at the minor's (kMinor past its start).
Test harness(BYTE* page, const BYTE* test, size_t minor) {
    std::vector<BYTE> code = {
        0x55,                                      // push rbp
        0x48, 0x89, 0xE5,                          // mov rbp, rsp
        0x48, 0x83, 0xEC, 0x20,                    // sub rsp, 0x20
        0x48, 0x89, 0x7D, 0xE8,                    // mov [rbp-0x18], rdi
        0x0F, 0xB7, 0x87, 0xE8, 0x01, 0x00, 0x00,  // movzx eax, word [rdi+0x1E8]
    };
    const size_t at = code.size();
    code.insert(code.end(), test, test + zap::kSeenTestSize);
    const BYTE major[] = {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC9, 0xC3};  // mov eax, 1; leave; ret
    code.insert(code.end(), major, major + sizeof(major));
    code.resize(at + minor, 0x90);  // nop to the minor's write
    const BYTE minor_write[] = {0xB8, 0x00, 0x00, 0x00, 0x00, 0xC9, 0xC3};  // mov eax, 0; ...
    code.insert(code.end(), minor_write, minor_write + sizeof(minor_write));
    std::memcpy(page, code.data(), code.size());
    return reinterpret_cast<Test>(page);
}

void test_the_seen_marker_patch_makes_item_48_a_major_and_nothing_else() {
    using Patch = bool (*)(const BYTE (&)[zap::kSeenTestSize], BYTE (&)[zap::kSeenTestSize]);
    const Patch patches[2] = {zap::seen_first_test_patch, zap::seen_second_test_patch};
    BYTE ours[2][zap::kSeenTestSize] = {};
    for (int site = 0; site < 2; ++site) {
        check(patches[site](kTests[site], ours[site]), "the game's test is patched");
        check(!patches[site](kTests[1 - site], ours[site]), "the other test's bytes are refused");
        BYTE other[zap::kSeenTestSize];
        std::memcpy(other, kTests[site], sizeof(other));
        other[4] = 0x7C;  // not the code as traced
        check(!patches[site](other, ours[site]), "other bytes are refused");
        patches[site](kTests[site], ours[site]);
    }

    void* p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(p != MAP_FAILED, "an executable page");
    if (p == MAP_FAILED) return;
    auto* page = static_cast<BYTE*>(p);
    alignas(8) BYTE object[0x200] = {};
    for (int site = 0; site < 2; ++site) {
        for (int id = 0; id < 0x100; ++id) {
            const auto word = static_cast<uint16_t>(id);
            std::memcpy(object + 0x1E8, &word, sizeof(word));
            const int as_game = harness(page, kTests[site], kMinor[site])(object);
            const int as_ours = harness(page + 2048, ours[site], kMinor[site])(object);
            const bool game_major = id >= 3 && id <= 0x23;
            if (as_game != (game_major ? 1 : 0)) check(false, "the game's test as traced");
            if (as_ours != ((game_major || id == 48) ? 1 : 0)) {
                std::printf("site %d id %d: %d\n", site, id, as_ours);
                check(false, "ours: 3 to 35 and 48 major, the rest minor");
            }
        }
    }
    munmap(p, 4096);
}

}  // namespace

int main() {
    test_the_seen_marker_patch_makes_item_48_a_major_and_nothing_else();
    if (g_failures == 0) std::printf("patch: all passed\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
