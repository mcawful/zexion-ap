// The traps: what the helper writes to set one off, as the game's own touch does (op 4 and op 5
// of the type-74 state handler, 0x46BE0 on B1: docs/re/memory.md, "Ailments: the script step
// that sets them", "The Phazo's disruption", "The Balorb's disorienting wave"). Offsets are into
// the player state (game_calls.cpp, player()), B1's.
#pragma once

#include <cstddef>
#include <cstdint>

namespace zap {
namespace trap {

// The world's item codes (client/ids.py, worlds/zexion/items.py): never held, no record.
constexpr int kDisruption = 102;  // Disruption Trap: the Phazo's touch
constexpr int kDisorient = 103;   // Disorient Trap: the Balorb's wave
constexpr bool is_trap(int item) { return item == kDisruption || item == kDisorient; }
// Each one's name, as the popup shows it (in capitals in the 8 x 8 font).
constexpr const char* name(int item) {
    return item == kDisruption ? "Disruption Trap" : item == kDisorient ? "Disorient Trap" : "";
}

// The timers, 16-bit frames left: energy weapons don't fire while the first is above 0, the
// controls are reversed while the second is.
constexpr ptrdiff_t kDisruptionTimer = 0xDC2;
constexpr ptrdiff_t kDisorientTimer = 0xDC4;
// The Phazo's state 2857 sets 240 frames, the wave's 2902 251 (neither forces its timer).
constexpr int16_t kDisruptionFrames = 240;
constexpr int16_t kDisorientFrames = 251;
// The Energy Shield's strength (32-bit): with it above 0, op 4 takes the strength down by the
// frames / 60 and sets +0xD1E to 5 (16-bit) instead of the timer.
constexpr ptrdiff_t kShieldStrength = 0x103C;
constexpr ptrdiff_t kShieldHit = 0xD1E;
constexpr int16_t kShieldHitValue = 5;
// Op 5, when no disorientation is running: this byte gains 4, modulo 8 (C's %, as the game's
// code: its meaning is open, assumed a direction of eight). And +0xD20 (16-bit) at least 2.
constexpr ptrdiff_t kFacing = 0xDDB;
constexpr ptrdiff_t kFlash = 0xD20;
constexpr int16_t kFlashAtLeast = 2;
// AILMENTS (the assist setting, 16-bit, `ailments`): at 2 op 5 does nothing. Op 4 does not read
// it; the game's per-frame code shortens or ends the disruption itself.
constexpr int16_t kAilmentsOff = 2;

// The sounds, each as the game plays it: the Phazo's contact state's (26) by the state's entry
// step (0x43B50: volume the state's 1.0, pitch -1, then 8 and 16); the shield's hit (50) by op 4
// (volume 0.7, pitch -1, then 0 and 8); and the wave's (82), by the state 2902 chains to after
// op 5 whatever op 5 did (2904, type 78, op 82: volume its +0x44 1.0, pitch its +0x60 -1, then 8
// and 16, at a point on the screen rather than the object's position).
struct Sound {
    int16_t id;
    float volume;
    int16_t a, b;  // the two 16-bit arguments after the pitch
};
constexpr Sound kContactSound = {26, 1.0f, 8, 16};
constexpr Sound kShieldSound = {50, 0.7f, 0, 8};
constexpr Sound kWaveSound = {82, 1.0f, 8, 16};
constexpr float kPitch = -1.0f;

}  // namespace trap
}  // namespace zap
