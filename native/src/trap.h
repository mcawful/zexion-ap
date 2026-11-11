// The traps: what the helper writes to set one off, as the game's own touch does (op 4 and op 5
// of the type-74 state handler, 0x46BE0 on B1: docs/re/memory.md, "Ailments: the script step
// that sets them", "The Phazo's disruption", "The Balorb's disorienting wave"), but longer, past
// an Energy Shield, and with AILMENTS left out while a trap's timer runs (the owner's choices,
// 2026-11-11). Offsets are into the player state (game_calls.cpp, player()), B1's.
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

#include "game_mem.h"  // CodePatch

namespace zap {
namespace trap {

// The world's item codes (client/ids.py, worlds/zexion/items.py): never held, no record.
constexpr int kDisruption = 102;  // Disruption Trap: the Phazo's touch
constexpr int kDisorient = 103;   // Disorient Trap: the Balorb wave's
constexpr bool is_trap(int item) { return item == kDisruption || item == kDisorient; }
// Each one's name, as the popup shows it (in capitals in the 8 x 8 font).
constexpr const char* name(int item) {
    return item == kDisruption ? "Disruption Trap" : item == kDisorient ? "Disorient Trap" : "";
}

// The timers, 16-bit frames left: energy weapons don't fire while the first is above 0, the
// controls are reversed while the second is.
constexpr ptrdiff_t kDisruptionTimer = 0xDC2;
constexpr ptrdiff_t kDisorientTimer = 0xDC4;
// About 12 s at the 55 frames a second measured (the Phazo's state 2857 sets 240, about 4.3 s;
// the wave's 2902 251, about 4.6 s). Each is set as the op sets its own: the larger of the timer
// and this.
constexpr int16_t kFrames = 660;
// Op 5, when no disorientation is running: this byte gains 4, modulo 8 (C's %, as the game's
// code: its meaning is open, assumed a direction of eight). And +0xD20 (16-bit) at least 2.
constexpr ptrdiff_t kFacing = 0xDDB;
constexpr ptrdiff_t kFlash = 0xD20;
constexpr int16_t kFlashAtLeast = 2;

// The sounds, each as the game plays it: the Phazo's contact state's (26) by the state's entry
// step (0x43B50: volume the state's 1.0, pitch -1, then 8 and 16); and the wave's (82), by the
// state 2902 chains to after op 5 (2904, type 78, op 82: volume its +0x44 1.0, pitch its +0x60
// -1, then 8 and 16, at a point on the screen rather than the object's position).
struct Sound {
    int16_t id;
    float volume;
    int16_t a, b;  // the two 16-bit arguments after the pitch
};
constexpr Sound kContactSound = {26, 1.0f, 8, 16};
constexpr Sound kWaveSound = {82, 1.0f, 8, 16};
constexpr float kPitch = -1.0f;

// AILMENTS left out while a trap's timer runs. The per-frame player update (0x1CC5C4 to
// 0x1CC661 on B1) tests the AILMENTS word twice for each timer: at 1 (REDUCED) the disruption
// counts down 3 a frame and the disorientation is cut to 60, at 2 (IMMUNE) each is set to 0.
// Each test ends in a `jne` (75) past what it does; the patch makes each a `jmp` (EB), so the
// timer counts down as at NORMAL. Two patches, one per timer (`ailments_hit_code` and
// `ailments_wave_code`), each from its first test's load of the word to its second test's jump.
// The bytes it checks are B1's but the loads' 32-bit displacements, which the helper checks
// lead to the configured `ailments` instead (game_calls.cpp, ailments_reads).
constexpr size_t kHitPatchSize = 0x2B;   // 0x1CC5C4 to 0x1CC5EE
constexpr size_t kWavePatchSize = 0x2C;  // 0x1CC62C to 0x1CC657
struct Jumps {
    size_t first, second;  // the two jne's opcode bytes
    size_t loads[2];       // each `movzx ecx, word [rip+d]` of AILMENTS (0F B7 0D d32)
};
constexpr Jumps kHitJumps = {0x0B, 0x29, {0x00, 0x1A}};
constexpr Jumps kWaveJumps = {0x0B, 0x2A, {0x00, 0x1F}};
bool hit_patch(const BYTE (&game)[kHitPatchSize], BYTE (&out)[kHitPatchSize]);
bool wave_patch(const BYTE (&game)[kWavePatchSize], BYTE (&out)[kWavePatchSize]);
using HitPatch = CodePatch<kHitPatchSize, hit_patch>;
using WavePatch = CodePatch<kWavePatchSize, wave_patch>;
// Whether each of `jumps`' loads at `site` reads `ailments`.
bool ailments_reads(const BYTE* site, const Jumps& jumps, const BYTE* ailments);

}  // namespace trap
}  // namespace zap
