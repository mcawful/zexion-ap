#include "trap.h"

#include <cstring>

namespace zap {
namespace trap {

namespace {

// B1's bytes at each site, the loads' displacements left 0 (masked: kHitJumps, kWaveJumps).
//   movzx ecx, word [rip+d]; cmp cx, r12w; jne +16; lea eax, [rdx-3]; mov [rbx+0xDC2], ax;
//   movzx eax, di; movzx ecx, word [rip+d]; jmp +2; mov eax, ebp; cmp cx, 2; jne +9
constexpr BYTE kHitGame[kHitPatchSize] = {
    0x0F, 0xB7, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x66, 0x41, 0x3B, 0xCC, 0x75, 0x16, 0x8D, 0x42,
    0xFD, 0x66, 0x89, 0x83, 0xC2, 0x0D, 0x00, 0x00, 0x0F, 0xB7, 0xC7, 0x0F, 0xB7, 0x0D, 0x00,
    0x00, 0x00, 0x00, 0xEB, 0x02, 0x8B, 0xC5, 0x66, 0x83, 0xF9, 0x02, 0x75, 0x09};
//   movzx ecx, word [rip+d]; cmp cx, r12w; jne +19; cmp ax, r15w; jle +13;
//   mov [rbx+0xDC4], r15w; movzx eax, r15w; movzx ecx, word [rip+d]; cmp cx, 2; jne +9
constexpr BYTE kWaveGame[kWavePatchSize] = {
    0x0F, 0xB7, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x66, 0x41, 0x3B, 0xCC, 0x75, 0x19, 0x66, 0x41,
    0x3B, 0xC7, 0x7E, 0x13, 0x66, 0x44, 0x89, 0xBB, 0xC4, 0x0D, 0x00, 0x00, 0x41, 0x0F, 0xB7,
    0xC7, 0x0F, 0xB7, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x66, 0x83, 0xF9, 0x02, 0x75, 0x09};
constexpr BYTE kJne = 0x75, kJmp = 0xEB;
constexpr size_t kLoadSize = 7, kDisplacement = 3;

// `game` as `expected` but for the loads' displacements; then `out`, its two jne's made jmps.
template <size_t N>
bool patch(const BYTE (&game)[N], BYTE (&out)[N], const BYTE (&expected)[N], const Jumps& jumps) {
    BYTE masked[N];
    std::memcpy(masked, game, N);
    for (size_t load : jumps.loads) std::memset(masked + load + kDisplacement, 0, 4);
    if (std::memcmp(masked, expected, N) != 0) return false;
    if (game[jumps.first] != kJne || game[jumps.second] != kJne) return false;
    std::memcpy(out, game, N);
    out[jumps.first] = kJmp;
    out[jumps.second] = kJmp;
    return true;
}

}  // namespace

bool hit_patch(const BYTE (&game)[kHitPatchSize], BYTE (&out)[kHitPatchSize]) {
    return patch(game, out, kHitGame, kHitJumps);
}

bool wave_patch(const BYTE (&game)[kWavePatchSize], BYTE (&out)[kWavePatchSize]) {
    return patch(game, out, kWaveGame, kWaveJumps);
}

bool ailments_reads(const BYTE* site, const Jumps& jumps, const BYTE* ailments) {
    if (site == nullptr || ailments == nullptr) return false;
    for (size_t load : jumps.loads) {
        BYTE bytes[kLoadSize];
        if (!safe_read(site + load, bytes, kLoadSize)) return false;
        int32_t displacement = 0;
        std::memcpy(&displacement, bytes + kDisplacement, 4);
        if (site + load + kLoadSize + displacement != ailments) return false;
    }
    return true;
}

}  // namespace trap
}  // namespace zap
