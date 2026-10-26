#include "game_calls.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace zap {

namespace {

// Player state, from the inventory (docs/re/memory.md, "Granting each kind of item", "Game
// functions the helper calls", "Other players' items").
constexpr ptrdiff_t kInventoryInPlayer = 0x108E;
constexpr ptrdiff_t kEnergy = 0xCEA, kEnergyMax = 0xCEC;
constexpr ptrdiff_t kMissiles = 0xCBC, kMissilesMax = 0xCC0;
constexpr ptrdiff_t kBombs = 0xCBE, kBombsMax = 0xCC2;
constexpr ptrdiff_t kHudMissiles = 0xB30, kHudBombs = 0xBB8;
constexpr ptrdiff_t kWeaponStripTimer = 0xA4C;
constexpr ptrdiff_t kSuitBits = 0x10D8;
constexpr ptrdiff_t kX = 0x1100, kY = 0x1108;
constexpr ptrdiff_t kPlayerCellRow = 0x107A, kPlayerCellColumn = 0x107B;
constexpr uint64_t kPlayerStateSize = 0x1110;  // through the position, the last field used
constexpr uint8_t kEnergyTank = 0, kMissileContainer = 1, kMegaBomb = 2, kMagnusArmor = 25;
constexpr uint8_t kMissileBox = 36, kBombPack = 37;
// A message's sequence number and frames left (the popup's place and time: text.h), and the
// frames between two of the helper's popups.
constexpr size_t kMessageSequence = 0x1D8, kMessageFramesLeft = 0x1EC;
constexpr int kPopupGapFrames = 20;
// Frames between two looks at the escape skip's event and at the scanner's text.
constexpr int kSkipFrames = 30, kScanFrames = 30;

using MessageFn = void* (*)(const char* text, int per_line, int x, int y, int, int, int frames);
using SoundFn = void (*)(void* system, short id, float x, float y, float volume, float pitch,
                         short, short category);
using HudNumberFn = void (*)(void* hud_object, double value);
using PlayerFn = void (*)(void* player);
using EnergyHudFn = void (*)(void* player, char force);
using SuitColorFn = void (*)(int slot, short palette);
using EventFn = void (*)(char row, char column, BYTE* event);  // an area-event handler
// A received Energy Shield runs its pickup's area event (Game::run_item_event).
constexpr uint8_t kEnergyShield = 50;
constexpr uint8_t kSpawnEventType = 11;  // its arguments: the subtype, then the object
constexpr int16_t kShieldObject = 2018;
constexpr uint64_t kSettledMs = 1000;  // in play in one cell this long: a shield, a death
// The player's death (docs/re/memory.md, "DeathLink"): death_fn(player, 0), as the game's
// damage code calls it once energy is 0 or below.
using DeathFn = void (*)(void* player, char kind);

// The config keys (client/patterns.py target names), in checking order, each with the member of
// GameAddresses it fills. Each lies in the image with kKeyBytes there (the offline cache checks a
// function's first 16 bytes): functions in code, data in writable data, the CRT's import slots
// (only read) outside code. room/before: what must also fit in the key's section. Every key is
// required.
constexpr uint64_t kKeyBytes = 16;
enum class Held { kCode, kData, kImportSlot };
using Member = BYTE* GameAddresses::*;
struct ConfigKey {
    const char* name;
    Member member;
    Held held;
    uint64_t room;         // bytes from the address that must lie in its section, if not 0
    const char* room_for;  // what they hold, for the error
    uint64_t before;       // and bytes before it
};
constexpr ConfigKey code(const char* name, Member member) {
    return {name, member, Held::kCode, 0, nullptr, 0};
}
constexpr ConfigKey data(const char* name, Member member, uint64_t room = 0,
                         const char* room_for = nullptr, uint64_t before = 0) {
    return {name, member, Held::kData, room, room_for, before};
}
constexpr ConfigKey import_slot(const char* name, Member member) {
    return {name, member, Held::kImportSlot, 0, nullptr, 0};
}
// transition_fn is data: it holds the address of the routine the game runs next.
using G = GameAddresses;  // for the table's width
constexpr ConfigKey kConfigKeys[] = {
    // The player state the helper touches, all in the inventory's section.
    data("inventory", &G::inventory, kPlayerStateSize - kInventoryInPlayer, "the player state",
         kInventoryInPlayer),
    data("save_loaded", &G::save_loaded), data("menu_state", &G::menu_state),
    data("item_records", &G::item_records), data("last_message", &G::last_message),
    data("weapon_strip_gate", &G::weapon_strip_gate), data("sound_system", &G::sound_system),
    code("message_fn", &G::message_fn), code("sound_fn", &G::sound_fn),
    code("hud_number_fn", &G::hud_number_fn), code("hud_layout_fn", &G::hud_layout_fn),
    code("energy_hud_fn", &G::energy_hud_fn), code("weapon_strip_fn", &G::weapon_strip_fn),
    code("suit_color_fn", &G::suit_color_fn),
    data("rando_blob", &G::rando_blob, kSectionSize, "the section"),
    data("rando_marker", &G::rando_marker), data("transition_fn", &G::transition_fn),
    data("transition_timer", &G::transition_timer), data("current_slot", &G::current_slot),
    code("newgame_fn", &G::newgame_fn), data("current_menu", &G::current_menu),
    data("randomizer_menu", &G::randomizer_menu, kMenuSize, "the menu's state"),
    code("widget_handler_fn", &G::widget_handler_fn),
    code("panel_handler_fn", &G::panel_handler_fn), code("rando_menu_fn", &G::rando_menu_fn),
    code("placement_fn", &G::placement_fn), code("rando_newgame_fn", &G::rando_newgame_fn),
    data("event_table", &G::event_table), data("event_count", &G::event_count),
    data("start_count", &G::start_count),
    data("start_cells", &G::start_cells, 4 * kStartTableMax, "64 rooms"),
    data("start_flags", &G::start_flags, kStartTableMax, "64 rooms"),
    data("sprite_texture", &G::sprite_texture), code("popup_width_code", &G::popup_width_code),
    data("item_pool", &G::item_pool), data("palette_table", &G::palette_table),
    data("frame_clock", &G::frame_clock), data("sprite_templates", &G::sprite_templates),
    data("event_handlers", &G::event_handlers), code("death_fn", &G::death_fn),
    data("halt_flag", &G::halt_flag), code("assist_menu_fn", &G::assist_menu_fn),
    data("object_pool", &G::object_pool), data("text_table", &G::text_table),
    code("equipment_fn", &G::equipment_fn), code("equip_panel_fn", &G::equip_panel_fn),
    code("widget_move_fn", &G::widget_move_fn), code("text_place_fn", &G::text_place_fn),
    import_slot("malloc_slot", &G::malloc_slot), import_slot("free_slot", &G::free_slot),
    code("widget_new_fn", &G::widget_new_fn), code("sprite_sheet_fn", &G::sprite_sheet_fn),
    code("panel_link_fn", &G::panel_link_fn), code("text_set_fn", &G::text_set_fn),
    data("routine_table", &G::routine_table), code("keys_branch", &G::keys_branch),
    code("file_panel_fn", &G::file_panel_fn), code("file_entry_fn", &G::file_entry_fn),
    code("copy_entry_fn", &G::copy_entry_fn), code("delete_entry_fn", &G::delete_entry_fn),
    // What the offline look reads whole (offline.h): the map grid, the flags, the save slots
    // and their copies of the randomizer section.
    data("map_grid", &G::map_grid, kMapCells, "what is read there"),
    data("flags", &G::flags, kFlagCount, "what is read there"),
    data("slot_array", &G::slot_array, slot::kCount * slot::kStride, "what is read there"),
    data("rando_slots", &G::rando_slots, slot::kCount * kSectionSize, "what is read there"),
    code("text_new_fn", &G::text_new_fn), code("text_link_fn", &G::text_link_fn),
    data("draw_layer", &G::draw_layer), data("map_handler", &G::map_handler),
};

// The RVA just past a section, as loaded.
uint64_t section_end(const IMAGE_SECTION_HEADER* section) {
    return section->VirtualAddress + (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
}

// The section of `module` holding `rva`, or nullptr.
const IMAGE_SECTION_HEADER* section_of(HMODULE module, uint64_t rva) {
    const IMAGE_NT_HEADERS64* nt = nt_headers(module);
    if (nt == nullptr) return nullptr;
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (rva >= section->VirtualAddress && rva < section_end(section)) return section;
    }
    return nullptr;
}

// "" if `rva` is where `key` can be in `image` (kConfigKeys), else what is wrong.
std::string check_key(HMODULE image, uint64_t image_size, const ConfigKey& key, uint64_t rva) {
    const std::string name = key.name;
    if (rva == 0 || rva + kKeyBytes > image_size) return name + " is outside the game";
    const IMAGE_SECTION_HEADER* section = section_of(image, rva);
    if (section == nullptr) return name + " is in no section";
    bool code = (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    if (code != (key.held == Held::kCode)) {
        return name + (code ? " is in code, not data" : " is not in code");
    }
    if (key.held == Held::kData && (section->Characteristics & IMAGE_SCN_MEM_WRITE) == 0) {
        return name + " is in read-only data";
    }
    if (key.room != 0 &&
        (rva < section->VirtualAddress + key.before || rva + key.room > section_end(section))) {
        return name + " leaves no room for " + key.room_for;
    }
    return "";
}

// A section offset as a reply names it: "0x6CC".
std::string hex_offset(unsigned offset) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%X", offset);
    return text;
}

// The offset of the first of `edits` that `block` does not hold, or -1.
int first_difference(const BYTE* block, const std::vector<SectionBytes>& edits) {
    for (const SectionBytes& e : edits) {
        if (std::memcmp(block + e.offset, e.bytes.data(), e.bytes.size()) != 0) return e.offset;
    }
    return -1;
}

void write_edits(BYTE* block, const std::vector<SectionBytes>& edits) {
    for (const SectionBytes& e : edits) {
        std::memcpy(block + e.offset, e.bytes.data(), e.bytes.size());
    }
}

}  // namespace

std::string Game::configure(HMODULE image, const std::map<std::string, uint64_t>& rvas,
                            bool active) {
    const IMAGE_NT_HEADERS64* nt = nt_headers(image);
    if (nt == nullptr) return "the game is not an image";
    const uint64_t image_size = nt->OptionalHeader.SizeOfImage;
    GameAddresses found;
    found.hooks = at_.hooks;
    for (const ConfigKey& key : kConfigKeys) {
        auto it = rvas.find(key.name);
        if (it == rvas.end()) return std::string("missing ") + key.name;
        std::string why = check_key(image, image_size, key, it->second);
        if (!why.empty()) return why;
        found.*key.member = reinterpret_cast<BYTE*>(image) + it->second;
    }
    base_ = reinterpret_cast<BYTE*>(image);
    // A client's config: the offline look put back first, with the addresses it was set with.
    if (configured_ && active) {
        leave_offline();
        drop_offline_warning();
        other_save_ = false;  // the new client says again, and is told again
        other_warned_ = false;
    }
    at_ = found;
    gl_ = load_gl();
    configured_ = true;
    active_ = active;
    return "";
}

BYTE* Game::player() const { return at_.inventory - kInventoryInPlayer; }

bool Game::in_play() const {
    return configured_ && *at_.save_loaded != kNoSaveLoaded && *at_.menu_state == 0 &&
           at<int16_t>(player(), kEnergy) > 0;
}

BYTE* Game::record(uint8_t item) const {
    BYTE* records = at<BYTE*>(at_.item_records, 0);
    if (records == nullptr || item >= record::kCount) return nullptr;
    return records + record::kSize * item;
}

void Game::refresh_hud() {
    BYTE* p = player();
    reinterpret_cast<HudNumberFn>(at_.hud_number_fn)(p + kHudMissiles, at<int16_t>(p, kMissiles));
    reinterpret_cast<HudNumberFn>(at_.hud_number_fn)(p + kHudBombs, at<int16_t>(p, kBombs));
    reinterpret_cast<PlayerFn>(at_.hud_layout_fn)(p);
    reinterpret_cast<EnergyHudFn>(at_.energy_hud_fn)(p, 1);
    if (*at_.weapon_strip_gate != 0 && at<int16_t>(p, kWeaponStripTimer) > 0) {
        reinterpret_cast<PlayerFn>(at_.weapon_strip_fn)(p);
    }
}

std::string Game::apply(const Batch& batch) {
    if (!configured_) return "refused not configured";
    if (!in_play()) return "refused not in play";
    BYTE* inv = at_.inventory;
    for (const Cas& c : batch.cas) {
        if (inv[c.item] != c.expected) {
            return "refused item " + std::to_string(c.item) + " holds " +
                   std::to_string(inv[c.item]);
        }
    }
    const bool had_armor = inv[kMagnusArmor] != 0;
    for (const Cas& c : batch.cas) inv[c.item] = c.value;
    BYTE* p = player();
    if (!batch.cas.empty() || batch.refresh) {
        // As the pickup routine does (docs/re/memory.md, "Granting each kind of item"): each
        // maximum from its items, the count moved with it.
        auto sync = [p](int want, ptrdiff_t count, ptrdiff_t max) {
            const auto now = at<int16_t>(p, max);
            if (want == now) return;
            at<int16_t>(p, count) += static_cast<int16_t>(want - now);
            at<int16_t>(p, max) = static_cast<int16_t>(want);
        };
        sync(inv[kMissileContainer] + 10 * inv[kMissileBox], kMissiles, kMissilesMax);
        sync(inv[kMegaBomb] + 10 * inv[kBombPack], kBombs, kBombsMax);
        auto energy = static_cast<int16_t>(inv[kEnergyTank] * 16);
        if (energy > at<int16_t>(p, kEnergyMax)) {
            at<int16_t>(p, kEnergyMax) = energy;
            at<int16_t>(p, kEnergy) = energy;
        }
        refresh_hud();
    }
    if (!had_armor && inv[kMagnusArmor] != 0) {
        auto palette = static_cast<short>((~(at<uint32_t>(p, kSuitBits) >> 18)) & 1);
        reinterpret_cast<SuitColorFn>(at_.suit_color_fn)(0, palette);
    }
    for (const Popup& popup : batch.popups) popups_.push_back(popup);
    return "applied";
}

size_t Game::pending_popups() const {
    return static_cast<size_t>(
        std::count_if(popups_.begin(), popups_.end(), [](const Popup& p) { return p.counted; }));
}

bool Game::popup_busy() const {
    BYTE* last = at<BYTE*>(at_.last_message, 0);
    if (last == nullptr) return false;
    return at<int32_t>(last, kMessageSequence) == at<int32_t>(at_.last_message, 8) &&
           at<int32_t>(last, kMessageFramesLeft) > 1;
}

void Game::pump_popups() {
    if (frames_since_popup_ < (1 << 20)) ++frames_since_popup_;
    if (popups_.empty() || !in_play()) return;
    if (frames_since_popup_ < kPopupGapFrames || popup_busy()) return;
    const Popup& popup = popups_.front();
    BYTE* rec = record(popup.item);
    if (rec == nullptr) {
        popups_.pop_front();
        return;
    }
    int per_line = kGamePopupWidth;
    std::string text;
    if (popup.bare) {
        text = notice_text(popup.suffix, kGamePopupWidth, &per_line);
    } else {
        std::string head;
        if (rec[record::kIcon] != 0) head += static_cast<char>(rec[record::kIcon]);
        const char* name = reinterpret_cast<const char*>(rec + record::kName);
        head.append(name, strnlen(name, record::kNameSize));
        text = popup_text(head, popup.suffix, kGamePopupWidth, &per_line);
    }
    void* message = reinterpret_cast<MessageFn>(at_.message_fn)(
        text.c_str(), per_line, kPopupX, kPopupY, 0, 0, kPopupFrames);
    if (message == nullptr) return;  // all four slots busy: try again next frame
    if (popup.bare) fit_notice_box(static_cast<BYTE*>(message), text, per_line);
    BYTE* p = player();
    auto sound = at<int16_t>(rec, record::kSound);
    if (!popup.silent && sound >= 0) {
        reinterpret_cast<SoundFn>(at_.sound_fn)(
            at_.sound_system, sound, static_cast<float>(at<double>(p, kX)),
            static_cast<float>(at<double>(p, kY)), at<float>(rec, record::kVolume), -1.0f, 0, 8);
    }
    popups_.pop_front();
    frames_since_popup_ = 0;
}

void Game::note_play(uint64_t now_ms) {
    // How long the player has been in play in one cell: a spawned shield waits for that, not
    // for a load, a respawn or a room change still settling.
    if (!configured_ || !in_play()) {
        play_since_ms_ = 0;
        return;
    }
    BYTE* p = player();
    int cell = at<uint8_t>(p, kPlayerCellRow) * 256 + at<uint8_t>(p, kPlayerCellColumn);
    if (play_since_ms_ == 0 || cell != play_cell_) {
        play_cell_ = cell;
        play_since_ms_ = now_ms;
    }
}

std::string Game::spawn_shield(uint64_t now_ms) {
    if (!configured_) return "refused not configured";
    if (!in_play() || play_since_ms_ == 0 || now_ms - play_since_ms_ < kSettledMs) {
        return "refused not settled";
    }
    std::string why = run_item_event(kEnergyShield);
    return why.empty() ? "spawned" : "refused " + why;
}

std::string Game::kill(uint64_t now_ms) {
    // As the damage code does (calls at 0x1B9D3A, 0x1F07A, 0x4771B): energy 0, then death_fn.
    // It returns at once while the halt flag is set (leaving the player alive at 0), and writes
    // unchecked through the message it posts (0x1DA6E7, 0x1DA6EF), NULL with all four slots
    // busy: so refuse while halted or a message shows.
    if (!configured_) return "refused not configured";
    if (!in_play() || play_since_ms_ == 0 || now_ms - play_since_ms_ < kSettledMs) {
        return "refused not settled";
    }
    if (*at_.halt_flag != 0) return "refused halted";
    if (popup_busy()) return "refused message showing";
    BYTE* p = player();
    at<int16_t>(p, kEnergy) = 0;
    reinterpret_cast<DeathFn>(at_.death_fn)(p, 0);
    return "died";
}

std::string Game::run_item_event(uint8_t item) {
    // The Energy Shield's pickup event (3553, type 11, spawns object 2018), run as the pickup
    // does (0x1FE85): the type's handler with the player's cell; every field checked first
    // (docs/re/memory.md, "Received Energy Shields").
    BYTE* rec = record(item);
    if (rec == nullptr) return "no item record";
    int16_t event = at<int16_t>(rec, record::kEvent);
    BYTE* table = pointer_at(at_.event_table);
    int16_t events = 0;
    if (event < 0 || table == nullptr || !read(at_.event_count, 0, events) || event >= events) {
        return "no event";
    }
    BYTE* ev = table + static_cast<ptrdiff_t>(event) * event::kSize;
    BYTE copy[event::kSize];
    if (!safe_read(ev, copy, sizeof(copy))) return "no event";
    if (copy[event::kType] != kSpawnEventType || copy[event::kTypeHigh] != 0 ||
        word(copy, event::kArg0) != 1 || word(copy, event::kArg1) != kShieldObject ||
        word(copy, event::kSelf) != event) {
        return "event not as traced";
    }
    BYTE* handler = pointer_at(at_.event_handlers, kSpawnEventType * sizeof(void*));
    if (handler == nullptr) return "no handler";
    // The handler must be code in the game's own image.
    auto address = reinterpret_cast<uintptr_t>(handler);
    auto image = reinterpret_cast<uintptr_t>(base_);
    const IMAGE_SECTION_HEADER* section =
        address > image ? section_of(reinterpret_cast<HMODULE>(base_), address - image) : nullptr;
    if (section == nullptr || (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
        return "handler not in the game's code";
    }
    BYTE* p = player();
    uint8_t row = at<uint8_t>(p, kPlayerCellRow), column = at<uint8_t>(p, kPlayerCellColumn);
    if (row >= kMapSide || column >= kMapSide) return "off the map";  // the raw cell can be -1
    reinterpret_cast<EventFn>(handler)(static_cast<char>(row), static_cast<char>(column), ev);
    return "";
}

void Game::arm_newgame(NewGamePlan plan) {
    newgame_ = std::move(plan);
    newgame_armed_ = true;
    newgame_status_ = "armed";
}

void Game::disarm_newgame() {
    newgame_armed_ = false;
    newgame_ = {};
    starting_ = false;
    if (newgame_status_ == "armed") newgame_status_ = "idle";
}

std::string Game::newgame_status() const { return newgame_status_; }

void Game::watch_newgame() {
    if (!configured_ || !newgame_armed_) return;
    if (at<void*>(at_.transition_fn, 0) != at_.newgame_fn) return;  // no new game queued
    if (at<int32_t>(at_.transition_timer, 0) <= 1) return;       // too close to being built
    if (at<uint16_t>(at_.rando_marker, 0) != 1) return;          // a normal new game: not ours
    // Check the section is the seed's mode, unless ARCHIPELAGO started it (starting_): that was
    // checked before the plan went in, and the plan's own writes now differ from the mode's.
    int differs = starting_ ? -1 : first_difference(at_.rando_blob, newgame_.expect);
    if (differs >= 0) {
        newgame_status_ = "refused the new game is not the seed's mode (section +" +
                          hex_offset(differs) + " differs)";
        disarm_newgame();
        return;
    }
    write_edits(at_.rando_blob, newgame_.write);
    newgame_status_ = "applied slot " + std::to_string(at<int32_t>(at_.current_slot, 0) + 1);
    disarm_newgame();
}

void Game::watch_menu(WidgetCallback callback) {
    if (!configured_) return;
    if (newgame_armed_ && !newgame_.menu.empty() && !starting_) {
        choice_.install(at_, callback);
        if (choice_.installed()) choice_.fit_border(at_);
    } else if (choice_.installed() || choice_.holds_hook()) {
        choice_.restore(at_, callback);
    }
}

void Game::watch_all(WidgetCallback callback) {
    if (!configured_) return;
    watch_menu(callback);
    watch_assists();
    watch_skip();
    watch_discs();
    watch_equipment();
    watch_mission();
    watch_files();
    watch_intel();
    watch_title();
    watch_offline();
    watch_map_names();
    watch_ap_items();
    watch_map_marks();
}

void Game::show_glyph(FontGlyph& glyph, bool wanted) {
    if (wanted) {
        glyph.paint(at_.sprite_texture, gl_, texture_check_);
    } else if (glyph.painted()) {
        glyph.clear(at_.sprite_texture, gl_, texture_check_);
    }
}

void Game::watch_assists() {
    if (!configured_) return;
    show_glyph(lock_glyph_, assists_locked_);
    if (assists_locked_) {
        assists_.install(at_);
    } else if (assists_.installed()) {
        assists_.restore(at_);
    }
}

void Game::restore_assists() {
    if (!configured_) return;
    assists_.restore(at_);
    lock_glyph_.clear(at_.sprite_texture, gl_, texture_check_);
}

namespace {

// Puts the randomizer menu's state back however start_archipelago leaves.
class MenuStateGuard {
public:
    explicit MenuStateGuard(BYTE* state) : state_(state) {
        std::memcpy(saved_, state_, kMenuSize);
    }
    ~MenuStateGuard() { std::memcpy(state_, saved_, kMenuSize); }
    MenuStateGuard(const MenuStateGuard&) = delete;
    MenuStateGuard& operator=(const MenuStateGuard&) = delete;

private:
    BYTE* state_;
    BYTE saved_[kMenuSize];
};

}  // namespace

std::string Game::ensure_start_table() {
    const std::vector<StartRoom>& rooms = newgame_.start_table;
    if (rooms.empty()) return "";
    // The second target (+0x6CC) names a room of the start table by index; the placement reads
    // that room's flag, so the index must be in the table.
    constexpr uint16_t kSecondTarget = 0x6CC;
    for (const SectionBytes& e : newgame_.write) {
        if (e.offset == kSecondTarget && e.bytes.size() >= 2) {
            int16_t index;
            std::memcpy(&index, e.bytes.data(), sizeof(index));
            if (index < 0 || static_cast<size_t>(index) >= rooms.size()) {
                return "the second start is outside the start table";
            }
        }
    }
    int16_t count = at<int16_t>(at_.start_count, 0);
    if (count == 0) {
        // The menu never opened in this run: build the table as it would, count last.
        // rooms.size() <= kStartTableMax (parse_newgame, requests.cpp), the room configure
        // checked for start_cells and start_flags.
        for (size_t i = 0; i < rooms.size(); ++i) {
            at<int16_t>(at_.start_cells, static_cast<ptrdiff_t>(4 * i)) = rooms[i].x;
            at<int16_t>(at_.start_cells, static_cast<ptrdiff_t>(4 * i + 2)) = rooms[i].y;
            at<uint8_t>(at_.start_flags, static_cast<ptrdiff_t>(i)) = rooms[i].flag;
        }
        at<int16_t>(at_.start_count, 0) = static_cast<int16_t>(rooms.size());
        return "";
    }
    if (static_cast<size_t>(count) != rooms.size()) {
        return "the game's start table has " + std::to_string(count) + " rooms, the seed's " +
               std::to_string(rooms.size());
    }
    for (size_t i = 0; i < rooms.size(); ++i) {
        if (at<int16_t>(at_.start_cells, static_cast<ptrdiff_t>(4 * i)) != rooms[i].x ||
            at<int16_t>(at_.start_cells, static_cast<ptrdiff_t>(4 * i + 2)) != rooms[i].y ||
            at<uint8_t>(at_.start_flags, static_cast<ptrdiff_t>(i)) != rooms[i].flag) {
            return "the game's start table differs from the seed's at room " + std::to_string(i);
        }
    }
    return "";
}

void Game::start_archipelago(WidgetCallback callback) {
    // The game calls the choice's callback once per press; a press after the plan was dropped,
    // or while a new game is queued, does nothing.
    if (!configured_ || !newgame_armed_ || newgame_.menu.empty() || starting_) return;
    if (at<void*>(at_.transition_fn, 0) == at_.newgame_fn) return;
    starting_ = true;
    // Refused: RANDOMIZER back, the plan dropped, nothing queued.
    auto refuse = [&](const std::string& why) {
        newgame_status_ = "refused " + why;
        choice_.restore(at_, callback);
        disarm_newgame();
    };
    try {
        std::string table = ensure_start_table();
        if (!table.empty()) return refuse(table);
        int differs = -1;
        {
            // The menu's state is the player's own (the game keeps it in its preferences): set
            // to the seed's mode for the placement, then put back.
            MenuStateGuard guard(at_.randomizer_menu);
            write_edits(at_.randomizer_menu, newgame_.menu);
            // START GAME's first step would pick a start; the placement reads the AP seed's
            // instead, so the plan goes in first. Then the placement, which builds the rest of
            // the section from the menu (the location list, the settings, the map tables).
            // START GAME's winnability check is skipped: the AP generator made the seed
            // beatable.
            write_edits(at_.rando_blob, newgame_.write);
            reinterpret_cast<void (*)()>(at_.placement_fn)();
            differs = first_difference(at_.rando_blob, newgame_.expect);
        }
        if (differs >= 0) {
            return refuse("the placement is not the seed's mode (section +" +
                          hex_offset(differs) + " differs)");
        }
        // The AP seed over the placement now, so the game is the seed's even if the plan is
        // dropped before the next frame; then the new game in the chosen slot, as the
        // play-anyway path queues it. watch_newgame reports it in the next frame.
        write_edits(at_.rando_blob, newgame_.write);
        reinterpret_cast<void (*)()>(at_.rando_newgame_fn)();
        if (at<void*>(at_.transition_fn, 0) != at_.newgame_fn) {
            return refuse("the game did not queue a new game (is the slot empty?)");
        }
        // The game is leaving the file screen: ARCHIPELAGO stays on the label through the fade,
        // and only RANDOMIZER's callback goes back, so the closing menu holds nothing of ours.
        choice_.release(at_, callback);
    } catch (...) {
        choice_.restore(at_, callback);
        disarm_newgame();
        throw;
    }
}

void Game::watch_skip() {
    if (!configured_ || (!active_ && !offline_skip_)) return;
    if (skip_frames_-- > 0) return;
    skip_frames_ = kSkipFrames;
    skip_.apply(at_.event_table, at_.event_count);
}

void Game::restore_skip() {
    if (configured_) skip_.restore(at_.event_table);
}

void Game::set_discs(int count, int required, std::string scan_line, bool told) {
    disc_count_ = count;
    discs_required_ = required;
    discs_told_ = told;
    scan_line_ = std::move(scan_line);
    pause_row_.set(scan_line_.empty() ? std::string() : pause_row_text(count, told ? required : 0));
    // discs off: drop a prompt asked for.
    if (scan_line_.empty() && configured_) mission_.cancel(at_);
    scan_frames_ = 0;  // checked in the next frame
}

void Game::watch_discs() {
    if (!configured_) return;
    if (scan_frames_-- > 0) return;
    scan_frames_ = kScanFrames;
    if (!scan_line_.empty()) {
        scan_.apply(at_.text_table, scan_line_);
    } else if (scan_.written()) {
        scan_.restore(at_.text_table);
    }
}

void Game::restore_discs() {
    if (!configured_) return;
    scan_.restore(at_.text_table);
    equip_.restore(at_);
    equip_screen_.restore(at_);
    pause_row_.restore(at_);  // its disc's entry off the image before the image is cleared
    disk_glyph_.clear(at_.sprite_texture, gl_, texture_check_);
    pause_disc_.clear(at_.sprite_texture, gl_, texture_check_);
}

void Game::watch_equipment() {
    equip_screen_.tidy(at_);
    // Every frame, before the row: a pause screen opened on a load's first frame still has it.
    pause_row_.prepare(known_disk_row());
    show_glyph(pause_disc_, pause_row_.on());
    pause_row_.watch(at_, pause_disc_.painted());
    show_glyph(disk_glyph_, !scan_line_.empty());
    if (!scan_line_.empty()) {
        equip_.install(at_);
    } else {
        if (equip_.installed()) equip_.restore(at_);
        if (equip_screen_.holds_hook()) equip_screen_.restore(at_);
    }
}

void Game::equipment_built() {
    if (!configured_ || scan_line_.empty()) return;
    std::string why = equip_screen_.add(at_, disc_count_);
    equip_status_ = why.empty() ? "shown " + std::to_string(disc_count_) : "not shown: " + why;
}

void Game::watch_mission() {
    // A save loaded or closed (save_loaded flips between 0 and 0xFF): a prompt asked for drops.
    int save = *at_.save_loaded;
    if (save != mission_save_) {
        if (mission_save_ != -1) mission_.cancel(at_);
        mission_save_ = save;
    }
    // Idle: no popup showing or queued, the last one kPopupGapFrames ago.
    bool idle = in_play() && popups_.empty() && !popup_busy() &&
                frames_since_popup_ >= kPopupGapFrames;
    mission_.watch(at_, idle);
}

void Game::watch_files() {
    files_.watch(at_);
    for (FontGlyph& glyph : disk_header_) show_glyph(glyph, files_.on());
}

void Game::file_event(void* widget, intptr_t event) {
    if (configured_) files_.on_event(at_, widget, event);
}

void Game::restore_files() {
    if (!configured_) return;
    client_file_counts_.clear();
    offline_file_counts_.clear();
    files_.set_counts({});
    files_.restore(at_);
    for (FontGlyph& glyph : disk_header_) glyph.clear(at_.sprite_texture, gl_, texture_check_);
}

bool Game::rando_save_loaded() const {
    return *at_.save_loaded != kNoSaveLoaded && at<uint16_t>(at_.rando_marker, 0) == 1;
}

bool Game::labels_seed_loaded(bool in_play_only) const {
    return labels_seed_ && rando_save_loaded() && (!in_play_only || in_play()) &&
           section_is(*labels_seed_, at_.rando_blob);
}

void Game::watch_map_names() {
    // The map is a menu: the save loaded, in play or not.
    map_names_.watch(at_, ap_items_.active() && labels_seed_loaded(false));
}

void Game::map_after() {
    if (configured_) map_names_.after(at_, ap_items_.labels(), label_widths_);
}

void* Game::map_original(void* child) const {
    void* handler = map_names_.original(child);
    // An object the helper did not wrap: the map's own handler, as the game holds it (none
    // read before configure).
    if (handler == nullptr) handler = pointer_at(at_.map_handler);
    return handler;
}

void Game::watch_map_marks() {
    constexpr int kMapFrames = 15;
    if (!ap_items_.active()) return;
    if (map_frames_-- > 0) return;
    map_frames_ = kMapFrames;
    // Only in the labels' seed's save (a plain randomizer game has the game's own Scrap Chunks),
    // in play or in a menu: a player who pauses at once and opens the map sees them raised.
    if (!labels_seed_loaded(false)) return;
    mark_important(at_.rando_blob, at_.map_grid, at_.flags[flag::kEnding] != 0);
}

void Game::watch_title() {
    // Only with no save loaded (the title, the file select): in play the draw list is long.
    if (*at_.save_loaded != kNoSaveLoaded) return;
    std::string why = title_.watch(at_, active_);
    if (!why.empty()) title_error_ = why;
    if (title_.shown()) title_error_.clear();
}

void Game::hide_title() {
    if (configured_) title_.hide(at_);
}

std::string Game::title_state() const {
    if (!title_error_.empty()) return title_error_;
    return title_.shown() ? "1" : "0";
}

void Game::watch_intel() { intel_.watch(at_.item_records); }

void Game::restore_intel() {
    if (!configured_) return;
    intel_.set(false);
    intel_.restore(at_.item_records);
}

void Game::mission_open() {
    if (configured_) mission_.open(at_);
}

void Game::equipment_event(void* widget, intptr_t event) {
    if (configured_) equip_screen_.on_event(at_, widget, event);
}

void Game::set_labels(std::map<int, Label> labels) {
    labels_seed_.reset();
    if (!labels.empty() && rando_save_loaded()) labels_seed_ = offline_seed_of(at_.rando_blob);
    if (labels.empty()) {
        restore_ap_items();  // no labels: Scrap Energy is Scrap Energy again
        return;
    }
    labels_ = labels;
    std::map<int, std::string> names;
    std::map<int, int> items;
    label_widths_.clear();
    item_labels_.clear();
    for (auto& [cell, label] : labels) {
        label_widths_[cell] = label.width;
        if (label.item >= 0) {
            item_labels_[cell] = label;
            items[cell] = label.item;
        }
        names[cell] = std::move(label.name);
    }
    looks_.set_items(std::move(items));
    ap_items_.set_labels(std::move(names));
    resolve_item_labels();
}

void Game::resolve_item_labels() {
    // Another Zexion player's item: its own icon and the game's name for it, from the item's
    // record, once the records are loaded.
    if (item_labels_.empty() || !configured_) return;
    BYTE* records = pointer_at(at_.item_records);
    if (records == nullptr) return;
    std::map<int, std::string> names = ap_items_.labels();
    for (auto it = item_labels_.begin(); it != item_labels_.end();) {
        const Label& pending = it->second;
        BYTE record[record::kSize];
        if (!safe_read(records + pending.item * record::kSize, record, sizeof(record))) return;
        std::string name;
        if (!pending.item_name.empty()) {
            name = pending.item_name;  // this player's own, named by the client
        } else if (stand_in_index(pending.item) >= 0) {
            // The stand-ins' records are restyled: their own names, as the game loads them.
            name = kStandInNames[stand_in_index(pending.item)];
            record[record::kIcon] = 0;
        } else {
            for (size_t i = 0; i < record::kNameSize && record[record::kName + i] != 0; ++i) {
                name += static_cast<char>(record[record::kName + i]);
            }
        }
        const size_t room = static_cast<size_t>(kGamePopupWidth) - 1;  // after the icon
        if (name.size() > room) name.resize(room);
        if (name.empty()) name = kApItemName;  // a record with no name
        uint8_t icon = record[record::kIcon] != 0 ? record[record::kIcon]
                       : pending.item == kDiskItem ? kDiskGlyph  // the helper's (equip.h)
                                                   : kApItemGlyph;
        Label done = layout_label(std::string(1, static_cast<char>(icon)) + name, pending.who);
        names[it->first] = done.name;
        label_widths_[it->first] = done.width;
        it = item_labels_.erase(it);
    }
    ap_items_.set_labels(std::move(names));
}

void Game::set_popup_width(int width) {
    if (at_.popup_width_code == nullptr) return;
    if (width < 1 || width > kGamePopupWidth) width = kGamePopupWidth;
    if (width == popup_width_) return;  // already so (popup_width_ starts unknown: -1)
    // `mov edx, imm32`: B1's instruction, holding 24 or a width a helper wrote (this one, or
    // one that left without putting 24 back). Anything else is not the code traced: it is
    // left alone, and not counted as ours to put back.
    uint8_t opcode = 0;
    int32_t imm = 0;
    if (!read(at_.popup_width_code, 0, opcode) || !read(at_.popup_width_code, 1, imm) ||
        opcode != 0xBA || imm < 1 || imm > kGamePopupWidth) {
        popup_width_ = kGamePopupWidth;
        at_.popup_width_code = nullptr;
        return;
    }
    popup_width_ = imm;
    if (imm == width) return;
    const int32_t value = width;
    // Not written: popup_width_ says what the game still holds.
    if (patch_code(at_.popup_width_code + 1, &value, sizeof(value))) popup_width_ = width;
}

void Game::watch_ap_items() {
    if (!configured_ || !ap_items_.active()) return;
    resolve_item_labels();
    // The player's map cell, as the player update keeps it: the row then the column, the
    // grid's index row * 54 + column (docs/re/memory.md, "Other players' items").
    uint8_t row = at<uint8_t>(player(), kPlayerCellRow);
    uint8_t column = at<uint8_t>(player(), kPlayerCellColumn);
    int cell = row < kMapSide && column < kMapSide ? row * kMapSide + column : -1;
    // Restyled only in the world: menus show the records too (the equipment screen draws Scrap
    // Chunk's frame, the map cursor a record's name), and nothing is picked up in a menu.
    bool world = *at_.save_loaded != kNoSaveLoaded && *at_.menu_state == 0;
    // One read of the item pool a frame, shared by naming and dressing (world only).
    BYTE* base = ItemLooks::item_frame_base(at_);
    const BYTE* pool = world ? read_item_pool() : nullptr;
    // The cell whose label sets the popup's width: the player's, or the one the stand-in about to
    // be picked up was named for.
    int width_cell = cell;
    if (world) {
        // Each stand-in named by the cell its object carries (a drop lands where its enemy died;
        // apitem.h). The general objects among them as of the last scan, below.
        int cells[kStandInCount];
        int touching = name_cells(stand_ins_seen(pool, base), at<double>(player(), kX),
                                  at<double>(player(), kY), cell, ap_items_.labels(), cells);
        if (touching >= 0) width_cell = cells[touching];
        ap_items_.watch(at_.item_records, cells);
    } else {
        ap_items_.release(at_.item_records);
    }
    ap_items_.paint(at_.sprite_texture, gl_, texture_check_, GetTickCount64(), world);
    // The pickup popup as wide as that label (width_cell's), if the stand-ins are ours; the
    // game's 24 elsewhere. Called from the frame call, on the game's thread, so the game is not
    // running the instruction while it changes.
    auto it = label_widths_.find(width_cell);
    set_popup_width(ap_items_.restyled() && it != label_widths_.end() ? it->second
                                                                      : kGamePopupWidth);
    // Other Zexion players' items shown as themselves, only while the stand-ins are ours.
    if (ap_items_.restyled()) {
        looks_.scan(at_, base);
        looks_.apply(at_, pool, base);
    } else {
        looks_.restore(at_);
    }
}

const BYTE* Game::read_item_pool() {
    if (at_.item_pool == nullptr) return nullptr;
    pool_copy_.resize(obj::kItemSize * ItemLooks::kSlots);
    return safe_read(at_.item_pool, pool_copy_.data(), pool_copy_.size()) ? pool_copy_.data()
                                                                       : nullptr;
}

std::vector<StandInSeen> Game::stand_ins_seen(const BYTE* pool, BYTE* base) const {
    // Item objects keep their position in their sprite fields (pixels, as the player's), and the
    // general objects are assumed to; each pool keeps its cell (obj::) (docs/re/memory.md, "Live
    // item objects"). A position off the map is not used: a cell is 320 x 224 pixels, its row
    // floor((y - 8) / 224) (docs/re/memory.md, "Other players' items").
    constexpr double kMapWidth = kMapSide * 320.0, kMapHeight = kMapSide * 224.0 + 8;
    std::vector<StandInSeen> out;
    auto add = [&](int item, uint8_t row, uint8_t column, const BYTE* o) {
        const double x = field<double>(o, sprite::kX), y = field<double>(o, sprite::kY);
        if (row >= kMapSide || column >= kMapSide || !(x >= 0 && x < kMapWidth) ||
            !(y >= 0 && y < kMapHeight)) {
            return;
        }
        out.push_back({item, row * kMapSide + column, x, y});
    };
    if (at_.item_pool != nullptr) {
        if (pool == nullptr) return out;  // not read: none named after an object
        for (int slot = 0; slot < ItemLooks::kSlots; ++slot) {
            const BYTE* o = pool + slot * obj::kItemSize;
            const int16_t record = field<int16_t>(o, obj::kRecord);
            if (field<int32_t>(o, obj::kSpawn) == 0 || stand_in_index(record) < 0) continue;
            add(record, o[obj::kItemRow], o[obj::kItemColumn], o);
        }
    }
    if (at_.object_pool != nullptr && base != nullptr) {
        BYTE o[obj::kGeneralRow + 2];
        for (int slot : looks_.flipped_slots()) {
            // Checked again: the list is from the last scan, up to 30 frames ago.
            if (slot < 0 || slot >= ItemLooks::kObjectSlots ||
                !safe_read(at_.object_pool + slot * obj::kGeneralSize, o, sizeof(o)) ||
                !ItemLooks::shows_stand_in(o, base)) {
                continue;
            }
            // A logo frame (shows_stand_in): which stand-in it is.
            int item = kStandInRecords[logo_index(field<int32_t>(o, sprite::kFrameIndex))];
            add(item, o[obj::kGeneralRow], o[obj::kGeneralColumn], o);
        }
    }
    return out;
}

void Game::restore_ap_items() {
    if (!configured_) return;
    looks_.restore(at_);  // the objects first, while their records are still ours
    looks_.set_items({});
    ap_items_.restore(at_.item_records, at_.sprite_texture, gl_, texture_check_);
    set_popup_width(kGamePopupWidth);
    label_widths_.clear();
    item_labels_.clear();
    labels_.clear();
}

void Game::drop_save_look() {
    restore_ap_items();
    restore_intel();
    set_assists_locked(false);
    restore_assists();
    set_discs(0, 0, "");
    restore_discs();
    restore_skip();
}

void Game::put_back_all(WidgetCallback callback) {
    disarm_newgame();
    drop_save_look();  // `discs` off first: the prompt's request dropped before it is put back
    if (configured_) {
        mission_.restore(at_);
        map_names_.restore(at_);
        choice_.restore(at_, callback);
    }
    restore_files();
    leave_offline();
}

ClientState Game::snapshot() const {
    ClientState s;
    s.file_counts = client_file_counts_;
    s.newgame_armed = newgame_armed_;
    s.starting = starting_;
    s.newgame = newgame_;
    s.newgame_status = newgame_status_;
    s.mission_asked = mission_.asked();
    s.mission_ending = mission_.ending();
    if (offline_index_ >= 0) return s;  // the look is the offline one's, set again by its watch
    s.assists_locked = assists_locked_;
    s.intel = intel_.on();
    s.disc_count = disc_count_;
    s.discs_required = discs_required_;
    s.discs_told = discs_told_;
    s.scan_line = scan_line_;
    s.labels = labels_;
    s.labels_seed = labels_seed_;
    return s;
}

void Game::rearm(ClientState s) {
    if (!configured_) return;
    set_assists_locked(s.assists_locked);
    set_intel(s.intel);
    if (!s.scan_line.empty()) {
        set_discs(s.disc_count, s.discs_required, std::move(s.scan_line), s.discs_told);
    }
    set_disk_slots(std::move(s.file_counts));
    if (!s.labels.empty()) {
        set_labels(std::move(s.labels));
        labels_seed_ = std::move(s.labels_seed);  // the seed they were set for
    }
    if (s.newgame_armed) {
        newgame_ = std::move(s.newgame);
        newgame_armed_ = true;
        starting_ = s.starting;
        newgame_status_ = std::move(s.newgame_status);
    }
    mission_.resume(s.mission_asked, s.mission_ending);
}

}  // namespace zap
