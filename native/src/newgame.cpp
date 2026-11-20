#include "newgame.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace zap {

namespace {

template <typename T>
T get(const uint8_t* p, ptrdiff_t offset) {
    T v;
    std::memcpy(&v, p + offset, sizeof(v));
    return v;
}

template <typename T>
void set(uint8_t* p, ptrdiff_t offset, T v) {
    std::memcpy(p + offset, &v, sizeof(v));
}

}  // namespace

std::string hex_offset(unsigned offset) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%X", offset);
    return text;
}

int first_difference(const uint8_t* block, const std::vector<SectionBytes>& edits) {
    for (const SectionBytes& e : edits) {
        if (std::memcmp(block + e.offset, e.bytes.data(), e.bytes.size()) != 0) return e.offset;
    }
    return -1;
}

void write_edits(uint8_t* block, const std::vector<SectionBytes>& edits) {
    for (const SectionBytes& e : edits) {
        std::memcpy(block + e.offset, e.bytes.data(), e.bytes.size());
    }
}

void NewGames::arm(NewGamePlan plan) {
    plan_ = std::move(plan);
    armed_ = true;
    status_ = "armed";
}

void NewGames::disarm() {
    armed_ = false;
    plan_ = {};
    starting_ = false;
    if (status_ == "armed") status_ = "idle";
}

void NewGames::refuse(const std::string& why) {
    status_ = "refused " + why;
    disarm();
}

bool NewGames::queued(const NewGameAddresses& a) {
    return get<uint8_t*>(a.transition_fn, 0) == a.newgame_fn;
}

void NewGames::watch(const NewGameAddresses& a) {
    if (!armed_) return;
    if (!queued(a)) return;
    if (get<int32_t>(a.transition_timer, 0) <= 1) return;  // too close to being built
    if (get<uint16_t>(a.rando_marker, 0) != 1) return;     // a normal new game: not ours
    // From ARCHIPELAGO (starting_): placed and checked already. From RANDOMIZER: it must hold
    // the seed text, then is placed again as the seed.
    if (!starting_) {
        int differs = first_difference(a.rando_blob, plan_.identify);
        if (differs >= 0) {
            refuse("the new game is not this seed (section +" + hex_offset(differs) + " differs)");
            return;
        }
        // The player's own placement, put back if the seed's is refused (or throws).
        std::vector<uint8_t> players(a.rando_blob, a.rando_blob + kSectionSize);
        std::string why;
        try {
            why = place_seed(a);
        } catch (...) {
            std::memcpy(a.rando_blob, players.data(), kSectionSize);
            disarm();
            throw;
        }
        if (!why.empty()) {
            std::memcpy(a.rando_blob, players.data(), kSectionSize);
            refuse(why);
            return;
        }
    }
    write_edits(a.rando_blob, plan_.write);
    status_ = "applied slot " + std::to_string(get<int32_t>(a.current_slot, 0) + 1);
    disarm();
}

std::string NewGames::ensure_start_table(const NewGameAddresses& a) {
    // Over a RANDOMIZER game, the game's own table, as START GAME used it (the client's fallback).
    static const std::vector<StartRoom> kNone;
    const std::vector<StartRoom>& rooms = starting_ ? plan_.start_table : kNone;
    int16_t count = get<int16_t>(a.start_count, 0);
    // The second target (+0x6CC) is a room's index, whose flag the placement reads: it must be in
    // the table used.
    const size_t rooms_in_table = !rooms.empty() ? rooms.size() : static_cast<size_t>(count);
    constexpr uint16_t kSecondTarget = 0x6CC;
    for (const SectionBytes& e : plan_.write) {
        if ((!rooms.empty() || count > 0) && e.offset == kSecondTarget && e.bytes.size() >= 2) {
            int16_t index;
            std::memcpy(&index, e.bytes.data(), sizeof(index));
            if (index < 0 || static_cast<size_t>(index) >= rooms_in_table) {
                return rooms.empty() ? "the second start is not a room of the game's own table"
                                     : "the second start is outside the start table";
            }
        }
    }
    if (rooms.empty()) return "";
    if (count == 0) {
        // The menu never opened this run: built as it would, count last. rooms.size() <=
        // kStartTableMax (parse_newgame), the room configure checked.
        for (size_t i = 0; i < rooms.size(); ++i) {
            set<int16_t>(a.start_cells, static_cast<ptrdiff_t>(4 * i), rooms[i].x);
            set<int16_t>(a.start_cells, static_cast<ptrdiff_t>(4 * i + 2), rooms[i].y);
            set<uint8_t>(a.start_flags, static_cast<ptrdiff_t>(i), rooms[i].flag);
        }
        set<int16_t>(a.start_count, 0, static_cast<int16_t>(rooms.size()));
        return "";
    }
    if (static_cast<size_t>(count) != rooms.size()) {
        return "the game's start table has " + std::to_string(count) + " rooms, the seed's " +
               std::to_string(rooms.size());
    }
    for (size_t i = 0; i < rooms.size(); ++i) {
        if (get<int16_t>(a.start_cells, static_cast<ptrdiff_t>(4 * i)) != rooms[i].x ||
            get<int16_t>(a.start_cells, static_cast<ptrdiff_t>(4 * i + 2)) != rooms[i].y ||
            get<uint8_t>(a.start_flags, static_cast<ptrdiff_t>(i)) != rooms[i].flag) {
            return "the game's start table differs from the seed's at room " + std::to_string(i);
        }
    }
    return "";
}

std::string NewGames::place_seed(const NewGameAddresses& a) {
    std::string table = ensure_start_table(a);
    if (!table.empty()) return table;
    // The menu's state is the player's (kept in the preferences): put back however it ends.
    struct MenuBack {
        uint8_t* menu;
        uint8_t saved[kMenuSize];
        explicit MenuBack(uint8_t* m) : menu(m) { std::memcpy(saved, menu, kMenuSize); }
        ~MenuBack() { std::memcpy(menu, saved, kMenuSize); }
        MenuBack(const MenuBack&) = delete;
        MenuBack& operator=(const MenuBack&) = delete;
    };
    int differs;
    {
        MenuBack back(a.randomizer_menu);
        write_edits(a.randomizer_menu, plan_.menu);
        // The plan in place of START GAME's first step (the start), then the placement
        // (docs/re/memory.md, "Starting a new game"). No winnability check: the AP generator made
        // the seed beatable.
        write_edits(a.rando_blob, plan_.write);
        reinterpret_cast<void (*)()>(a.placement_fn)();
        differs = first_difference(a.rando_blob, plan_.expect);
    }
    if (differs >= 0) {
        return "the placement is not the seed's (section +" + hex_offset(differs) + " differs)";
    }
    // The seed over the placement now, even if the plan is dropped before the next frame.
    write_edits(a.rando_blob, plan_.write);
    return "";
}

}  // namespace zap
