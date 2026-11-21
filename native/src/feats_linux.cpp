// The Feats guard's Linux half: the ending's patch, and the SteamInternal_ContextInit the helper
// exports in place of Steam's (feats.h). The Windows build's own is feats_win.cpp.

#include <dlfcn.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "feats.h"
#include "feats_ending.h"
#include "game_layout.h"
#include "game_mem.h"

namespace zap {

using namespace feats_ending;

// The Linux build's ending, as traced (docs/re/linux.md, the `ending_unlocks` and `ending_join`
// rows): `mov edi, 13` (14Victory) and `call setTheAchievement` at the site, the six-byte `mov
// word [rbp-0x26], 0` after them (the patch's last two bytes land in it: nothing jumps there);
// flag 251's store just before the join, flag 250's at it.
namespace {
constexpr BYTE kMovEdi13[] = {0xBF, 0x0D, 0x00, 0x00, 0x00};
constexpr ptrdiff_t kCallAt = 5, kAfterCall = 10;
constexpr BYTE kCallRel = 0xE8;
constexpr BYTE kMovWord[] = {0x66, 0xC7};
std::atomic<const BYTE*> g_set_achievement{nullptr};  // the game's setTheAchievement(int)

// The rel32 at `code + at` that takes code ending at `next` to `to` (iat.h's, Windows-only).
void put_rel32(BYTE* code, size_t at, const BYTE* next, const BYTE* to) {
    const auto d = static_cast<int32_t>(to - next);
    std::memcpy(code + at, &d, sizeof(d));
}
}  // namespace

void set_feats_achievement_fn(const void* fn) {
    g_set_achievement.store(static_cast<const BYTE*>(fn));
}

void FeatsGuard::patch_ending(const GameAddresses& a) {
    BYTE* site = a.ending_unlocks;
    BYTE* join = a.ending_join;
    BYTE now[kEndingPatchSize];
    if (site == nullptr || join == nullptr || !safe_read(site, now, sizeof(now))) {
        ending_error_ = "the ending cannot be read";
        return;
    }
    if (ending_patched_ && site == site_ && std::memcmp(now, written_, sizeof(now)) == 0) return;
    // The call as traced: 14Victory to the game's own setTheAchievement, then the word store.
    const BYTE* target = site + kAfterCall + field<int32_t>(now, kCallAt + 1);
    if (std::memcmp(now, kMovEdi13, sizeof(kMovEdi13)) != 0 || now[kCallAt] != kCallRel ||
        g_set_achievement.load() == nullptr || target != g_set_achievement.load() ||
        std::memcmp(now + kAfterCall, kMovWord, sizeof(kMovWord)) != 0) {
        ending_error_ = "the ending's call reads otherwise";
        return;
    }
    // The join after it: flag 251's store just before it, flag 250's at it.
    BYTE around[2 * kStoreSize];
    if (join <= site || join - site > kMaxStretch ||
        !safe_read(join - kStoreSize, around, sizeof(around))) {
        ending_error_ = "the ending's join is not after its call";
        return;
    }
    BYTE* flag250 = a.flags + flag::kEnding;
    BYTE* flag251 = a.flags + flag::kFeat;
    if (store_target(join - kStoreSize, around, 1) != flag251 ||
        store_target(join, around + kStoreSize, 1) != flag250) {
        ending_error_ = "the ending's join reads otherwise";
        return;
    }
    // mov byte [rip+flag 251], 0; jmp join: as on Windows.
    BYTE patch[kEndingPatchSize];
    if (!fits_rel32(site + kStoreSize, flag251) || !fits_rel32(site + kEndingPatchSize, join)) {
        ending_error_ = "the ending is out of reach";
        return;
    }
    std::memcpy(patch, kStoreByte, sizeof(kStoreByte));
    put_rel32(patch, 2, site + kStoreSize, flag251);
    patch[6] = 0;
    patch[7] = kJmp;
    put_rel32(patch, 8, site + kEndingPatchSize, join);
    if (!patch_code(site, patch, sizeof(patch))) {
        ending_error_ = "the ending could not be written";
        return;
    }
    std::memcpy(original_, now, sizeof(now));
    std::memcpy(written_, patch, sizeof(patch));
    site_ = site;
    ending_patched_ = true;
    ending_error_.clear();
}

// The Linux build: no slot written and no code made. The helper exports SteamInternal_ContextInit
// itself (the dynamic linker binds the game's call to it: docs/re/linux.md, "Mapped by
// disassembly: the Feats guard's Steam and load test"), and feats_context_init decides each call
// from these, which the game thread sets (set_switch) and any caller reads.
namespace {

using Init = void* (*)(void*);

struct Steam {
    std::atomic<const void*> stats{nullptr};  // SteamUserStats()'s context
    std::atomic<const void*> ours{nullptr};   // the helper's export
    std::atomic<Init> real{nullptr};          // libsteam_api's, once found
    std::atomic<bool> blocked{false};         // the guard on: an AP save loaded
    // The load test, on while `marker` is set: the save loaded, the randomizer marker word 1 and
    // the current slot (0-based) one of `ap_slots`' bits.
    std::atomic<const BYTE*> marker{nullptr};
    std::atomic<const BYTE*> saved{nullptr};
    std::atomic<const BYTE*> slot{nullptr};
    std::atomic<uint32_t> ap_slots{0};
};
Steam g_steam;

// The stand-in: the holder the game reads the interface from, the interface, its vtable. Every
// method returns 0 (SetAchievement, ClearAchievement, StoreStats: their results are not read),
// whatever its arguments. Built at load time, before any constructor runs (constinit: the build
// fails if it could not be), so the helper's constructor, or another library's, never finds the
// holder still zero. It fits the game's accessor, which reads one interface pointer through the
// holder (`mov rax,[rax]`) and calls slots of its vtable that return nothing read, and no more:
// an accessor that reads more of a context block, an interface of more than kMethods methods,
// or a method whose result is a pointer would not be served.
using Method = uint64_t (*)();
extern "C" uint64_t zap_feats_zero() { return 0; }
constexpr size_t kMethods = 64;  // the library's ISteamUserStats has 44
struct StandIn {
    Method vtable[kMethods];
    void* interface = vtable;  // an object is its vtable's address
    void* holder = &interface;
    constexpr StandIn() : vtable() {
        for (Method& m : vtable) m = &zap_feats_zero;
    }
};
constinit StandIn g_stand_in;

bool blocking() {
    if (g_steam.blocked.load()) return true;
    const BYTE* marker = g_steam.marker.load();
    if (marker == nullptr) return false;
    const BYTE* saved = g_steam.saved.load();
    const BYTE* slot = g_steam.slot.load();
    uint16_t word = 0;
    int32_t current = -1;
    std::memcpy(&word, marker, sizeof(word));
    std::memcpy(&current, slot, sizeof(current));
    return *saved != kNoSaveLoaded && word == 1 && current >= 0 && current < slot::kCount &&
           (g_steam.ap_slots.load() >> current & 1u) != 0;
}

}  // namespace

// libsteam_api's SteamInternal_ContextInit: the next definition after the helper's, else the
// library's own if it is loaded but not in the global scope; never the helper's. Not kept while
// not found (a later load may bring it).
static Init real_init() {
    Init real = g_steam.real.load();
    if (real != nullptr) return real;
    real = reinterpret_cast<Init>(dlsym(RTLD_NEXT, "SteamInternal_ContextInit"));
    if (real == nullptr) {
        if (void* steam = dlopen("libsteam_api.so", RTLD_LAZY | RTLD_NOLOAD)) {
            real = reinterpret_cast<Init>(dlsym(steam, "SteamInternal_ContextInit"));
            dlclose(steam);  // its count back: RTLD_NOLOAD took one
        }
    }
    if (real == nullptr || reinterpret_cast<const void*>(real) == g_steam.ours.load()) {
        return nullptr;
    }
    g_steam.real.store(real);
    return real;
}

void set_feats_stats_context(const void* context) { g_steam.stats.store(context); }

void set_feats_export(const void* export_fn) { g_steam.ours.store(export_fn); }

void* feats_context_init(void* context) {
    if (context != nullptr && context == g_steam.stats.load() && blocking()) {
        return &g_stand_in.holder;
    }
    Init real = real_init();
    static std::atomic<bool> said{false};
    if (real == nullptr && !said.exchange(true)) {
        // In the game this stubs Steam (no achievement outside AP saves) with `feats_status` the
        // only other sign (check_steam says the library's function is not found).
        std::fprintf(stderr,
                     "zexion-ap: SteamInternal_ContextInit has no libsteam_api's to go on to: "
                     "Steam's calls are answered by the stand-in, which does nothing\n");
    }
    // With no libsteam_api to go on to (a program without it that the preload reaches: the game
    // has it NEEDED), the stand-in's holder, not null: the game's accessor reads through what it
    // gets (`mov rax,[rax]`), and the stand-in's every method returns 0.
    return real != nullptr ? real(context) : &g_stand_in.holder;
}

void* FeatsGuard::stand_in() const { return &g_stand_in.holder; }

bool FeatsGuard::load_test() const { return g_steam.marker.load() != nullptr; }

std::string FeatsGuard::check_steam(const GameAddresses&) {
    if (g_steam.stats.load() == nullptr) return "the stats context's symbol is not known";
    // The game's call binds to the first definition in the global scope: the helper's, if it is
    // (another preload ahead of it would take the game's calls), and libsteam_api's to forward to.
    if (dlsym(RTLD_DEFAULT, "SteamInternal_ContextInit") != g_steam.ours.load()) {
        return "the game's SteamInternal_ContextInit is not the helper's";
    }
    if (real_init() == nullptr) return "libsteam_api's SteamInternal_ContextInit is not found";
    return "";
}

bool FeatsGuard::make_pages() { return true; }

void FeatsGuard::hook_steam(const GameAddresses& a, bool on, const std::set<int>& loading) {
    // A failed check is tried again every kSteamRetryFrames frames, not kept for the run: the
    // library may not be loaded yet (real_init: "a later load may bring it"), and until the check
    // passes an AP save would earn Steam achievements. Only a passed one stays (no set_image on
    // Linux to reset it).
    if (!steam_checked_) {
        if (steam_wait_ > 0) {
            --steam_wait_;
        } else {
            steam_error_ = check_steam(a);
            if (steam_error_.empty()) {
                steam_checked_ = true;
            } else {
                steam_wait_ = kSteamRetryFrames;
            }
        }
    }
    if (!steam_error_.empty()) return;
    set_switch(a, on, loading);
    // "Hooked": the export decides from here (no slot to write; any non-null marks it).
    slot_ = reinterpret_cast<void**>(&g_stand_in.holder);
    steam_slot_error_.clear();
}

void FeatsGuard::set_switch(const GameAddresses& a, bool on, const std::set<int>& loading) {
    g_steam.blocked.store(on);
    if (loading.empty() || a.rando_marker == nullptr) {
        g_steam.marker.store(nullptr);  // the test off before its table is cleared
        g_steam.ap_slots.store(0);
        return;
    }
    uint32_t bits = 0;
    for (int n : loading) {
        if (n >= 1 && n <= slot::kCount) bits |= 1u << (n - 1);
    }
    // The table and the places before the marker's, which turns the test on.
    g_steam.saved.store(a.save_loaded);
    g_steam.slot.store(a.current_slot);
    g_steam.ap_slots.store(bits);
    g_steam.marker.store(a.rando_marker);
}

void FeatsGuard::release_steam() {
    if (slot_ == nullptr) return;
    set_switch({}, false, {});
    slot_ = nullptr;
}
}  // namespace zap
