// Host tests for Feats and achievements kept out of AP saves (feats.h): the AP-save test, the
// guard going on and off, the ending's patch run on a stand-in ending, Steam's import slot
// hooked, the refusals, and putting both back. This program imports SteamInternal_ContextInit
// by name from a stand-in steam_api64.dll (steam_stub.cpp), as the game does, so the helper finds
// and hooks the real import slot of a real import.

#include <windows.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "game_calls.h"
#include "iat.h"
#include "offline.h"
#include "stand_in.h"

using namespace stand_in;

extern "C" __declspec(dllimport) void* SteamInternal_ContextInit(void* context);
extern "C" __declspec(dllimport) int stub_calls(int slot);

namespace {

constexpr int kSetAchievement = 0x30 / 8, kStoreStats = 0x48 / 8;

void** import_slot() {
    return zap::find_import_slot(GetModuleHandleW(nullptr), "steam_api64.dll",
                                 "SteamInternal_ContextInit");
}

void* steam_function() {
    return reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"steam_api64.dll"), "SteamInternal_ContextInit"));
}

void* slot_value() { return *static_cast<void* volatile*>(import_slot()); }

// Writes the import slot as another hook would.
void write_slot(void* value) {
    DWORD old = 0;
    VirtualProtect(import_slot(), sizeof(void*), PAGE_READWRITE, &old);
    *static_cast<void* volatile*>(import_slot()) = value;
    VirtualProtect(import_slot(), sizeof(void*), old, &old);
}

// An achievement call as the game makes one: the accessor through the import, then the
// interface's vtable slot (SetAchievement with a name, StoreStats without).
using Method = bool (*)(void* self, const char* name);
__declspec(noinline) bool call_stats(void* context, int slot, const char* name) {
    void* holder = SteamInternal_ContextInit(context);
    void* object = *static_cast<void**>(holder);
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<Method>(vtable[slot])(object, name);
}

// A randomizer save loaded whose section has an item table and start of its own (`salt`).
void load_save(uint8_t salt) {
    for (int i = 0; i < 154; ++i) g_section[0x26C + i] = static_cast<uint8_t>(i * 7 + salt);
    for (int i = 0; i < 6; ++i) g_section[0x310 + i] = static_cast<uint8_t>(salt + i);
    g_save_loaded = 3;
    g_rando_marker = 1;
}

zap::OfflineSeed seed_of_save() { return zap::offline_seed_of(g_section); }

// The 12 bytes at the stand-in ending's call, read through volatile (stand_in.h, write_code).
std::vector<unsigned char> call_bytes() {
    std::vector<unsigned char> out;
    for (size_t i = 0; i < zap::kEndingPatchSize; ++i) {
        out.push_back(code_byte(g_ending, static_cast<int>(kEndingCall + i)));
    }
    return out;
}

void reset() {
    reset_game();
    g_rando_marker = 0;
    g_flags[250] = 0;
    g_flags[251] = 0;
    build_ending(import_slot());
    if (slot_value() != steam_function()) write_slot(steam_function());
}

// The stand-in game, with the import slot this program calls Steam through.
std::map<std::string, uint64_t> feats_config() {
    std::map<std::string, uint64_t> config = stand_in_config();
    config["context_init_slot"] = rva(import_slot());
    return config;
}

std::string configure_feats(zap::Game& game, bool active = true) {
    return game.configure(GetModuleHandleW(nullptr), feats_config(), active);
}

// An AP save of a seed the game knows, loaded and watched over two frame calls: the guard on.
void guard_on(zap::Game& game, uint8_t salt = 1) {
    load_save(salt);
    game.add_known_seeds({seed_of_save()});
    game.watch_feats();
    game.watch_feats();
}

std::string seed_text(const zap::OfflineSeed& s) {
    return "zexion-ap offline 1\ntable " + hex(s.table, sizeof(s.table)) + "\nstart " +
           hex(s.start, sizeof(s.start)) + "\nintel 0\nassists free\ndiscs -\nunfound -\n";
}

void test_the_ap_save_test() {
    reset();
    zap::Game game;
    load_save(1);
    check(!game.ap_save_loaded(), "nothing configured: no AP save");
    check(configure_feats(game).empty(), "the stand-in configures");
    check(!game.ap_save_loaded(), "a randomizer save of no known seed (a plain one) is not one");
    game.add_known_seeds({seed_of_save()});
    check(game.ap_save_loaded(), "a seed a client put is");
    g_rando_marker = 0;
    check(!game.ap_save_loaded(), "a normal save (the section still the last game's) is not");
    g_rando_marker = 1;
    g_save_loaded = 0xFF;
    check(!game.ap_save_loaded(), "with no save loaded, nothing is");
    load_save(2);
    check(!game.ap_save_loaded(), "another seed's section is not");
    game.add_known_seeds({seed_of_save(), seed_of_save()});
    check(game.known_seeds() == 2, "each seed known once");

    // Offline: a seed file read from the folder, with no client.
    zap::Game offline;
    check(configure_feats(offline, false).empty(), "configured from the cache");
    load_save(3);
    offline.set_offline({seed_of_save()});
    check(offline.ap_save_loaded(), "a seed file's save, with no client");
    // A client of another seed: the save's own seed known from its file still counts.
    load_save(4);
    offline.add_known_seeds({seed_of_save()});  // the client's own
    check(configure_feats(offline).empty(), "a client configures");
    load_save(3);
    check(offline.ap_save_loaded(), "a seed file's save, with a client of another seed");

    // The seed the labels were set for (a client sends them while its save is loaded).
    zap::Game labeled;
    check(configure_feats(labeled).empty(), "configured");
    load_save(5);
    std::map<int, zap::Label> labels;
    check(zap::parse_labels("ap_labels\nlabel 5 2 41", labels).empty(), "a label parses");
    labeled.set_labels(labels);
    check(labeled.ap_save_loaded(), "the labels' seed's save is one");
    load_save(6);
    check(!labeled.ap_save_loaded(), "and only it");
}

void test_a_put_seed() {
    load_save(7);
    zap::OfflineSeed s = seed_of_save(), out;
    check(zap::put_seed_of("offline_put seed abc-0-1\n" + seed_text(s), out) &&
              out.name == "abc-0-1" && zap::offline_matches(out, g_section + 0x26C,
                                                            g_section + 0x310),
          "a put seed file is its seed, named");
    check(!zap::put_seed_of("offline_put config\n" + seed_text(s), out), "a config is not");
    check(!zap::put_seed_of("offline_put seed abc-0-1\nnot a seed file\n", out),
          "a file that does not parse is not");
    check(!zap::put_seed_of("offline_put seed abc-0-1", out), "nor one with no file");
}

void test_on_and_off() {
    reset();
    zap::Game game;
    check(configure_feats(game).empty(), "configured");
    load_save(1);
    game.add_known_seeds({seed_of_save()});
    game.watch_feats();
    check(!game.feats().on() && !game.feats().ending_patched() && !game.feats().steam_hooked(),
          "one frame call is not enough: during a load the section may be the last save's");
    game.watch_feats();
    check(game.feats().on() && game.feats_state() == "on", "on at the second", game.feats_state());
    check(game.feats().ending_patched() && game.feats().steam_hooked(), "both changes made");
    check(game.unload_blocker() != nullptr &&
              std::string(game.unload_blocker()) == "the ending's patch is still the helper's",
          "an unload waits for a frame to put the ending back");
    g_rando_marker = 0;  // a normal save
    game.watch_feats();
    check(!game.feats().on() && game.feats_state() == "off", "off at once", game.feats_state());
    check(!game.feats().ending_patched() && !game.feats().steam_hooked(), "both put back");
    check(slot_value() == steam_function(), "the import slot is steam_api64's again");
    check(game.unload_blocker() == nullptr, "nothing to wait for");
}

void test_the_ending() {
    reset();
    const std::vector<unsigned char> original = call_bytes();
    // Off: the ending as the game runs it, its Steam call through the import.
    int set_before = stub_calls(kSetAchievement);
    run_ending();
    check(g_flags[250] == 1 && g_flags[251] == 1 && stub_calls(kSetAchievement) == set_before + 1,
          "off: the ending sets 14Victory, then flags 251 and 250");

    zap::Game game;
    check(configure_feats(game).empty(), "configured");
    guard_on(game);
    check(game.feats().ending_patched(), "on: patched", game.feats_state());
    const unsigned char* site = g_ending + kEndingCall;
    const std::vector<unsigned char> patch = call_bytes();
    check(patch[0] == 0xC6 && patch[1] == 0x05 && patch[6] == 0 && patch[7] == 0xE9,
          "as `mov byte [flag 251], 0; jmp join`");
    check(site + 7 + get<int32_t>(patch.data(), 2) == g_flags + 251, "the mov clears flag 251");
    check(site + 12 + get<int32_t>(patch.data(), 8) == g_ending + kEndingJoin,
          "the jmp lands on the join");
    // A save that already holds flag 251 (an ending before the guard).
    g_flags[250] = 0;
    g_flags[251] = 1;
    set_before = stub_calls(kSetAchievement);
    run_ending();
    check(g_flags[250] == 1, "on: flag 250 (the client's goal) is still set");
    check(g_flags[251] == 0, "on: flag 251 is cleared, so no Feats of Natu screen follows");
    check(stub_calls(kSetAchievement) == set_before, "on: no achievement");
    // Run twice: the routine runs once per ending, but nothing in it depends on that.
    run_ending();
    check(g_flags[251] == 0 && stub_calls(kSetAchievement) == set_before, "again the same");

    g_save_loaded = 0xFF;  // MISSION COMPLETE and the title: no save loaded
    game.watch_feats();
    check(!game.feats().ending_patched() && call_bytes() == original,
          "off: the game's bytes back");
    g_save_loaded = 3;
    g_rando_marker = 0;  // a normal save
    g_flags[250] = 0;
    g_flags[251] = 0;
    run_ending();
    check(g_flags[250] == 1 && g_flags[251] == 1 && stub_calls(kSetAchievement) == set_before + 1,
          "a normal save's ending as before");
}

void test_steam() {
    reset();
    zap::Game game;
    check(configure_feats(game).empty(), "configured");
    guard_on(game);
    check(game.feats().steam_hooked() && slot_value() == game.feats().stub(),
          "on: the import slot holds the stub");
    int set_before = stub_calls(kSetAchievement), store_before = stub_calls(kStoreStats);
    check(!call_stats(g_stats_context, kSetAchievement, "02MachineGun"),
          "on: SetAchievement on the stats context reaches the stand-in (0)");
    check(!call_stats(g_stats_context, kStoreStats, nullptr), "and StoreStats");
    check(stub_calls(kSetAchievement) == set_before && stub_calls(kStoreStats) == store_before,
          "Steam sees neither");
    check(SteamInternal_ContextInit(g_stats_context) == game.feats().stand_in(),
          "the accessor returns the stand-in's holder");
    check(call_stats(g_utils_context, kSetAchievement, "x") &&
              stub_calls(kSetAchievement) == set_before + 1,
          "on: another context passes through to steam_api64");
    g_rando_marker = 0;  // a normal save
    game.watch_feats();
    check(slot_value() == steam_function(), "off: the slot is steam_api64's");
    check(call_stats(g_stats_context, kSetAchievement, "02MachineGun") &&
              stub_calls(kSetAchievement) == set_before + 2,
          "off: the stats context reaches Steam");
}

void test_refusals() {
    // The stand-in's own config: a slot that is not the import's. Neither change is made.
    reset();
    zap::Game plain;
    check(configure(plain).empty(), "configured with a slot that is no import");
    guard_on(plain);
    check(plain.feats().on() && !plain.feats().steam_hooked() && !plain.feats().ending_patched(),
          "on, but nothing changed");
    const std::string why = plain.feats_state();
    check(why.find("the ending's call reads otherwise") != std::string::npos &&
              why.find("the slot is not steam_api64's SteamInternal_ContextInit") !=
                  std::string::npos,
          "and hello says why", why);

    // A context that asks for another interface.
    reset();
    zap::Game other;
    auto config = feats_config();
    config["stats_context"] = rva(g_utils_context);
    check(other.configure(GetModuleHandleW(nullptr), config).empty(), "configured");
    guard_on(other);
    check(!other.feats().steam_hooked() &&
              other.feats_state().find("not the user stats'") != std::string::npos,
          "another context is not hooked", other.feats_state());
    check(other.feats().ending_patched(), "the ending is, on its own checks");
    g_rando_marker = 0;
    other.watch_feats();

    // An ending that reads otherwise at its join: nothing written.
    reset();
    const std::vector<unsigned char> before = call_bytes();
    set_code_byte(g_ending, kEndingJoin + 1, 0x8B);  // not a movsxd
    zap::Game join;
    check(configure_feats(join).empty(), "configured");
    guard_on(join);
    check(!join.feats().ending_patched() &&
              call_bytes() == before &&
              join.feats_state().find("join reads otherwise") != std::string::npos,
          "a join that reads otherwise: the ending left alone", join.feats_state());
    check(join.feats().steam_hooked(), "Steam still hooked");
    g_rando_marker = 0;
    join.watch_feats();

    // An import slot something else has hooked: left to it.
    reset();
    void* other_hook = reinterpret_cast<void*>(&stub_calls);
    write_slot(other_hook);
    zap::Game hooked;
    check(configure_feats(hooked).empty(), "configured");
    guard_on(hooked);
    check(!hooked.feats().steam_hooked() && slot_value() == other_hook &&
              hooked.feats_state().find("does not hold steam_api64's") != std::string::npos,
          "another hook in the slot: not replaced", hooked.feats_state());
    write_slot(steam_function());
    hooked.watch_feats();
    check(hooked.feats().steam_hooked(), "hooked once it lets go");
    g_rando_marker = 0;
    hooked.watch_feats();
}

void test_put_back() {
    reset();
    const std::vector<unsigned char> original = call_bytes();
    zap::Game game;
    check(configure_feats(game).empty(), "configured");
    guard_on(game);

    // Configured again with the same addresses (a client reconnecting): left as it is.
    check(configure_feats(game).empty() && game.feats().ending_patched() &&
              game.feats().steam_hooked(),
          "a config with the same addresses keeps both");

    // Before an unload: both back, in the frame call.
    game.restore_feats();
    check(!game.feats().ending_patched() && !game.feats().steam_hooked() &&
              call_bytes() == original &&
              slot_value() == steam_function() && game.unload_blocker() == nullptr,
          "restore_feats puts both back");
    // A refused unload: the next frame calls set them again.
    game.watch_feats();
    game.watch_feats();
    check(game.feats().ending_patched() && game.feats().steam_hooked(), "and they come back");

    // An unload no frame came for: Steam's slot back from the pipe thread, the stub kept.
    void* stub = game.feats().stub();
    game.release_steam();
    check(slot_value() == steam_function() && !game.feats().steam_hooked(),
          "release_steam puts the slot back");
    auto stub_fn = reinterpret_cast<void* (*)(void*)>(stub);
    check(stub_fn(g_stats_context) == &g_stats_context[2],
          "a call that read the stub before then passes through");
    // The stand-in a call may still hold: its methods still answer 0.
    void* object = *static_cast<void**>(game.feats().stand_in());
    void** vtable = *static_cast<void***>(object);
    check(!reinterpret_cast<Method>(vtable[kSetAchievement])(object, "x") &&
              !reinterpret_cast<Method>(vtable[kStoreStats])(object, nullptr),
          "the stand-in stays, answering 0");

    // Something hooked over the stub: the slot is left to it, and the stub passes through.
    game.watch_feats();
    check(game.feats().steam_hooked(), "hooked again");
    void* over = reinterpret_cast<void*>(&stub_calls);
    write_slot(over);
    game.restore_feats();
    check(slot_value() == over, "a hook over the stub is not undone");
    check(stub_fn(g_stats_context) == &g_stats_context[2], "the stub passes through");
    write_slot(steam_function());
    g_rando_marker = 0;
    game.watch_feats();
}

}  // namespace

int main() {
    if (import_slot() == nullptr || steam_function() == nullptr) {
        std::printf("FAIL: the stand-in steam_api64.dll is not imported\n");
        return EXIT_FAILURE;
    }
    test_the_ap_save_test();
    test_a_put_seed();
    test_on_and_off();
    test_the_ending();
    test_steam();
    test_refusals();
    test_put_back();
    return report("feats tests");
}
