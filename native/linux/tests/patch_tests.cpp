// The Linux build's code patches run side by side with the game's bytes they replace: the map's
// on-screen write (native/src/mapmarks.h, seen_first_test_patch and seen_second_test_patch),
// each of its two tests of the item's ID run in a harness that gives it the ID as the game's code
// does and reports which of the game's two writes it reaches, for every ID; and the text window's
// top cut (native/src/text.h, text_cut_patch), run with the window's edge and the glyph's row on
// the stack as the game keeps them, for every row and edge it is taken at.
#include <sys/mman.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../../tests/check.h"
#include "mapmarks.h"
#include "text.h"

namespace {

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

using Cut = int (*)(int edge, int row_y);

// A function running `cut` as setGameMessageStringClipping does: the window's top edge in
// [rbp-0x1c], the glyph's row's 10 x row in [rbp-0xc], edx left as the call before leaves it;
// the cut, eax, returned.
Cut cut_harness(BYTE* page, const BYTE* cut) {
    std::vector<BYTE> code = {
        0x55,                          // push rbp
        0x48, 0x89, 0xE5,              // mov rbp, rsp
        0x48, 0x83, 0xEC, 0x40,        // sub rsp, 0x40
        0x89, 0x7D, 0xE4,              // mov [rbp-0x1c], edi
        0x89, 0x75, 0xF4,              // mov [rbp-0xc], esi
        0xBA, 0x5A, 0x5A, 0x5A, 0x5A,  // mov edx, 0x5A5A5A5A
    };
    code.insert(code.end(), cut, cut + zap::kTextCutSize);
    code.push_back(0xC9);  // leave
    code.push_back(0xC3);  // ret
    std::memcpy(page, code.data(), code.size());
    return reinterpret_cast<Cut>(page);
}

void test_the_text_cut_patch_cuts_every_row_as_the_first() {
    // B1's cut, as the game has it (setGameMessageStringClipping, 0x569617).
    constexpr BYTE kGame[zap::kTextCutSize] = {0x8B, 0x45, 0xE4, 0x99, 0xC1, 0xEA, 0x1D,
                                               0x01, 0xD0, 0x83, 0xE0, 0x07, 0x29, 0xD0};
    BYTE ours[zap::kTextCutSize] = {};
    BYTE other[zap::kTextCutSize];
    std::memcpy(other, kGame, sizeof(other));
    other[2] = 0xE8;  // another stack slot: not the code as traced
    check(!zap::text_cut_patch(other, ours), "other bytes are refused");
    check(zap::text_cut_patch(kGame, ours), "the game's cut is patched");

    void* p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(p != MAP_FAILED, "an executable page");
    if (p == MAP_FAILED) return;
    auto* page = static_cast<BYTE*>(p);
    const Cut as_game = cut_harness(page, kGame);
    const Cut as_ours = cut_harness(page + 2048, ours);
    // The cut is taken while the edge is past the row's top and within its 7 (0x56954D).
    for (int row = 0; row < 32; ++row) {
        const int y = 10 * row;
        for (int edge = y + 1; edge <= y + 7; ++edge) {
            if (as_game(edge, y) != edge % 8) check(false, "the game's cut: the edge mod 8");
            if (as_ours(edge, y) != edge - y) {
                std::printf("row %d edge %d: %d\n", row, edge, as_ours(edge, y));
                check(false, "ours: the edge less the row's top");
            }
            if (row == 0 && as_ours(edge, y) != as_game(edge, y)) {
                check(false, "the first row as the game cuts it");
            }
        }
    }
    munmap(p, 4096);
}

}  // namespace

int main() {
    test_the_seen_marker_patch_makes_item_48_a_major_and_nothing_else();
    test_the_text_cut_patch_cuts_every_row_as_the_first();
    return report("patch");
}
