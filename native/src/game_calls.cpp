#include "game_calls.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace zap {

namespace {

// Player state, from the inventory (docs/re/memory.md, "Granting each kind of item").
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
constexpr uint64_t kSettledMs = 1000;  // in play, in one cell, this long before a shield
// The player's death (docs/re/memory.md, "DeathLink"): death_fn(player, 0), as the game's
// damage code calls it once energy is 0 or below.
using DeathFn = void (*)(void* player, char kind);

// The addresses the client sends (`config`, client/patterns.py target names), checked in this
// order: each must lie in the game's image, with at least kKeyBytes there (the offline cache
// checks a function's first 16 bytes: offline.h), a function in executable code and anything
// else in writable data, except the C runtime's import slots, which are only read, in the
// read-only data they live in. Some must also leave room in their section for what is read or
// written there. The optional ones may be missing from an older helper's offline cache: the
// title screen's lines (title.h, keys from 0.22.0) and the map cursor's names (mapmarks.h,
// map_handler from 0.24.1) are then left off.
constexpr uint64_t kKeyBytes = 16;
enum class Held { kCode, kData, kImportSlot };
struct ConfigKey {
    const char* name;
    Held held;
    bool optional;
    uint64_t room;         // bytes from the address that must lie in its section, if not 0
    const char* room_for;  // what they hold, for the error
    uint64_t before;       // and bytes before it
};
constexpr ConfigKey code(const char* name, bool optional = false) {
    return {name, Held::kCode, optional, 0, nullptr, 0};
}
constexpr ConfigKey data(const char* name, uint64_t room = 0, const char* room_for = nullptr,
                         uint64_t before = 0) {
    return {name, Held::kData, false, room, room_for, before};
}
constexpr ConfigKey optional_data(const char* name) {
    return {name, Held::kData, true, 0, nullptr, 0};
}
constexpr ConfigKey import_slot(const char* name) {
    return {name, Held::kImportSlot, false, 0, nullptr, 0};
}
// transition_fn is data: it holds the address of the routine the game runs next.
constexpr ConfigKey kConfigKeys[] = {
    // The player state the helper touches, all in the inventory's section.
    data("inventory", kPlayerStateSize - kInventoryInPlayer, "the player state",
         kInventoryInPlayer),
    data("save_loaded"), data("menu_state"), data("item_records"), data("last_message"),
    data("weapon_strip_gate"), data("sound_system"), code("message_fn"), code("sound_fn"),
    code("hud_number_fn"), code("hud_layout_fn"), code("energy_hud_fn"), code("weapon_strip_fn"),
    code("suit_color_fn"), data("rando_blob", kSectionSize, "the section"),
    data("rando_marker"), data("transition_fn"), data("transition_timer"), data("current_slot"),
    code("newgame_fn"), data("current_menu"),
    data("randomizer_menu", kMenuSize, "the menu's state"), code("widget_handler_fn"),
    code("panel_handler_fn"), code("rando_menu_fn"), code("placement_fn"),
    code("rando_newgame_fn"), data("event_table"), data("event_count"), data("start_count"),
    data("start_cells", 4 * kStartTableMax, "64 rooms"),
    data("start_flags", kStartTableMax, "64 rooms"), data("sprite_texture"),
    code("popup_width_code"), data("item_pool"), data("palette_table"), data("frame_clock"),
    data("sprite_templates"), data("event_handlers"), code("death_fn"), data("halt_flag"),
    code("assist_menu_fn"), data("object_pool"), data("text_table"), code("equipment_fn"),
    code("equip_panel_fn"), code("widget_move_fn"), code("text_place_fn"),
    import_slot("malloc_slot"), import_slot("free_slot"), code("widget_new_fn"),
    code("sprite_sheet_fn"), code("panel_link_fn"), code("text_set_fn"), data("routine_table"),
    code("keys_branch"), code("file_panel_fn"), code("file_entry_fn"), code("copy_entry_fn"),
    code("delete_entry_fn"),
    // What the offline look reads whole (offline.h): the map grid, the flags, the save slots
    // and their copies of the randomizer section.
    data("map_grid", kMapCells, "what is read there"),
    data("flags", kFlagCount, "what is read there"),
    data("slot_array", slot::kCount * slot::kStride, "what is read there"),
    data("rando_slots", slot::kCount * kSectionSize, "what is read there"),
    code("text_new_fn", true), code("text_link_fn", true), optional_data("draw_layer"),
    optional_data("map_handler"),
};

// The RVA just past a section, as loaded.
uint64_t section_end(const IMAGE_SECTION_HEADER* section) {
    return section->VirtualAddress + (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
}

// The section of `module` holding `rva`, or nullptr.
const IMAGE_SECTION_HEADER* section_of(HMODULE module, uint64_t rva) {
    auto* base = reinterpret_cast<const BYTE*>(module);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
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

// The addresses configure checked, by key; null for an optional key left out.
struct Found {
    const std::map<std::string, BYTE*>& keys;
    BYTE* operator()(const char* key) const {
        auto it = keys.find(key);
        return it == keys.end() ? nullptr : it->second;
    }
};

MenuAddresses menu_addresses(const Found& f, const Hooks& h) {
    MenuAddresses a;
    a.current_menu = f("current_menu");
    a.panel_handler = f("panel_handler_fn");
    a.panel_hook = h.panel;
    a.widget_handler = f("widget_handler_fn");
    a.rando_menu = f("rando_menu_fn");
    a.assist_menu = f("assist_menu_fn");
    a.palette_table = f("palette_table");
    return a;
}

EquipAddresses equip_addresses(const Found& f, const Hooks& h) {
    EquipAddresses a;
    a.current_menu = f("current_menu");
    a.widget_handler = f("widget_handler_fn");
    a.equipment_fn = f("equipment_fn");
    a.equip_panel_fn = f("equip_panel_fn");
    a.widget_move_fn = f("widget_move_fn");
    a.text_place_fn = f("text_place_fn");
    a.widget_new_fn = f("widget_new_fn");
    a.sprite_sheet_fn = f("sprite_sheet_fn");
    a.panel_link_fn = f("panel_link_fn");
    a.text_set_fn = f("text_set_fn");
    a.malloc_slot = f("malloc_slot");
    a.free_slot = f("free_slot");
    a.sprite_templates = f("sprite_templates");
    a.item_records = f("item_records");
    a.hook = h.equipment;
    a.panel_hook = h.equipment_panel;
    return a;
}

MissionAddresses mission_addresses(const Found& f, const Hooks& h) {
    MissionAddresses a;
    a.current_menu = f("current_menu");
    a.transition_fn = f("transition_fn");
    a.transition_timer = f("transition_timer");
    a.routine_table = f("routine_table");
    a.widget_handler = f("widget_handler_fn");
    a.text_set_fn = f("text_set_fn");
    a.open_hook = h.mission_open;
    a.yes_hook = h.mission_yes;
    return a;
}

PauseAddresses pause_addresses(const Found& f, const Hooks& h) {
    PauseAddresses a;
    a.current_menu = f("current_menu");
    a.equipment_fn = f("equipment_fn");
    a.equipment_hook = h.equipment;
    a.widget_handler = f("widget_handler_fn");
    a.sprite_templates = f("sprite_templates");
    a.sprite_sheet_fn = f("sprite_sheet_fn");
    a.keys_branch = f("keys_branch");
    return a;
}

FileAddresses file_addresses(const Found& f, const Hooks& h) {
    FileAddresses a;
    a.current_menu = f("current_menu");
    a.file_panel_fn = f("file_panel_fn");
    a.entry_fns[0] = f("file_entry_fn");
    a.entry_fns[1] = f("copy_entry_fn");
    a.entry_fns[2] = f("delete_entry_fn");
    a.hook = h.file_entry;
    return a;
}

MapAddresses map_addresses(const Found& f, const Hooks& h) {
    MapAddresses a;
    a.current_menu = f("current_menu");
    a.map_handler = f("map_handler");
    a.text_set_fn = f("text_set_fn");
    a.hook = h.map;
    return a;
}

TitleAddresses title_addresses(const Found& f) {
    TitleAddresses a;
    a.draw_layer = f("draw_layer");
    a.text_new_fn = f("text_new_fn");
    a.text_place_fn = f("text_place_fn");
    a.text_link_fn = f("text_link_fn");
    return a;
}

}  // namespace

std::string Game::configure(HMODULE image, const std::map<std::string, uint64_t>& rvas,
                            bool active) {
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        reinterpret_cast<const BYTE*>(image) +
        reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
    const uint64_t image_size = nt->OptionalHeader.SizeOfImage;
    std::map<std::string, BYTE*> found;
    for (const ConfigKey& key : kConfigKeys) {
        auto it = rvas.find(key.name);
        if (it == rvas.end()) {
            if (key.optional) continue;  // its part left off
            return std::string("missing ") + key.name;
        }
        std::string why = check_key(image, image_size, key, it->second);
        if (!why.empty()) return why;
        found[key.name] = reinterpret_cast<BYTE*>(image) + it->second;
    }
    base_ = reinterpret_cast<BYTE*>(image);
    // A client's config: the offline look put back first, with the addresses it was set with.
    if (configured_ && active) {
        leave_offline();
        drop_offline_warning();
        other_save_ = false;  // the new client says again, and is told again
        other_warned_ = false;
    }
    const Found f{found};
    // The game's state and functions Game uses itself.
    inventory_ = f("inventory");
    save_loaded_ = f("save_loaded");
    menu_state_ = f("menu_state");
    item_records_ = f("item_records");
    last_message_ = f("last_message");
    weapon_strip_gate_ = f("weapon_strip_gate");
    sound_system_ = f("sound_system");
    message_fn_ = f("message_fn");
    sound_fn_ = f("sound_fn");
    hud_number_fn_ = f("hud_number_fn");
    hud_layout_fn_ = f("hud_layout_fn");
    energy_hud_fn_ = f("energy_hud_fn");
    weapon_strip_fn_ = f("weapon_strip_fn");
    suit_color_fn_ = f("suit_color_fn");
    rando_blob_ = f("rando_blob");
    rando_marker_ = f("rando_marker");
    map_grid_ = f("map_grid");
    flags_ = f("flags");
    slot_array_ = f("slot_array");
    rando_slots_ = f("rando_slots");
    transition_fn_ = f("transition_fn");
    transition_timer_ = f("transition_timer");
    current_slot_ = f("current_slot");
    newgame_fn_ = f("newgame_fn");
    randomizer_menu_ = f("randomizer_menu");
    placement_fn_ = f("placement_fn");
    rando_newgame_fn_ = f("rando_newgame_fn");
    event_table_ = f("event_table");
    event_count_ = f("event_count");
    text_table_ = f("text_table");
    start_count_ = f("start_count");
    start_cells_ = f("start_cells");
    start_flags_ = f("start_flags");
    sprite_texture_ = f("sprite_texture");
    popup_width_code_ = f("popup_width_code");
    item_pool_ = f("item_pool");
    palette_table_ = f("palette_table");
    frame_clock_ = f("frame_clock");
    sprite_templates_ = f("sprite_templates");
    event_handlers_ = f("event_handlers");
    death_fn_ = f("death_fn");
    object_pool_ = f("object_pool");
    halt_flag_ = f("halt_flag");
    // And each part's own, with the helper's stand-ins it points the game at.
    menu_ = menu_addresses(f, hooks_);
    equip_at_ = equip_addresses(f, hooks_);
    mission_at_ = mission_addresses(f, hooks_);
    pause_at_ = pause_addresses(f, hooks_);
    files_at_ = file_addresses(f, hooks_);
    map_at_ = map_addresses(f, hooks_);
    title_at_ = title_addresses(f);
    gl_ = load_gl();
    configured_ = true;
    active_ = active;
    return "";
}

BYTE* Game::player() const { return inventory_ - kInventoryInPlayer; }

bool Game::in_play() const {
    return configured_ && *save_loaded_ != kNoSaveLoaded && *menu_state_ == 0 &&
           at<int16_t>(player(), kEnergy) > 0;
}

BYTE* Game::record(uint8_t item) const {
    BYTE* records = at<BYTE*>(item_records_, 0);
    if (records == nullptr || item >= record::kCount) return nullptr;
    return records + record::kSize * item;
}

void Game::refresh_hud() {
    BYTE* p = player();
    reinterpret_cast<HudNumberFn>(hud_number_fn_)(p + kHudMissiles, at<int16_t>(p, kMissiles));
    reinterpret_cast<HudNumberFn>(hud_number_fn_)(p + kHudBombs, at<int16_t>(p, kBombs));
    reinterpret_cast<PlayerFn>(hud_layout_fn_)(p);
    reinterpret_cast<EnergyHudFn>(energy_hud_fn_)(p, 1);
    if (*weapon_strip_gate_ != 0 && at<int16_t>(p, kWeaponStripTimer) > 0) {
        reinterpret_cast<PlayerFn>(weapon_strip_fn_)(p);
    }
}

std::string Game::apply(const Batch& batch) {
    if (!configured_) return "refused not configured";
    if (!in_play()) return "refused not in play";
    BYTE* inv = inventory_;
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
        // As the pickup routine does (docs/re/memory.md, "Granting each kind of item").
        auto missiles = static_cast<int16_t>(inv[kMissileContainer] + 10 * inv[kMissileBox]);
        if (missiles != at<int16_t>(p, kMissilesMax)) {
            at<int16_t>(p, kMissiles) +=
                static_cast<int16_t>(missiles - at<int16_t>(p, kMissilesMax));
            at<int16_t>(p, kMissilesMax) = missiles;
        }
        auto bombs = static_cast<int16_t>(inv[kMegaBomb] + 10 * inv[kBombPack]);
        if (bombs != at<int16_t>(p, kBombsMax)) {
            at<int16_t>(p, kBombs) += static_cast<int16_t>(bombs - at<int16_t>(p, kBombsMax));
            at<int16_t>(p, kBombsMax) = bombs;
        }
        auto energy = static_cast<int16_t>(inv[kEnergyTank] * 16);
        if (energy > at<int16_t>(p, kEnergyMax)) {
            at<int16_t>(p, kEnergyMax) = energy;
            at<int16_t>(p, kEnergy) = energy;
        }
    }
    if (!batch.cas.empty() || batch.refresh) refresh_hud();
    if (!had_armor && inv[kMagnusArmor] != 0) {
        auto palette = static_cast<short>((~(at<uint32_t>(p, kSuitBits) >> 18)) & 1);
        reinterpret_cast<SuitColorFn>(suit_color_fn_)(0, palette);
    }
    for (const Popup& popup : batch.popups) popups_.push_back(popup);
    return "applied";
}

size_t Game::pending_popups() const {
    return static_cast<size_t>(
        std::count_if(popups_.begin(), popups_.end(), [](const Popup& p) { return p.counted; }));
}

bool Game::popup_busy() const {
    BYTE* last = at<BYTE*>(last_message_, 0);
    if (last == nullptr) return false;
    return at<int32_t>(last, kMessageSequence) == at<int32_t>(last_message_, 8) &&
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
    void* message = reinterpret_cast<MessageFn>(message_fn_)(
        text.c_str(), per_line, kPopupX, kPopupY, 0, 0, kPopupFrames);
    if (message == nullptr) return;  // all four slots busy: try again next frame
    if (popup.bare) fit_notice_box(static_cast<BYTE*>(message), text, per_line);
    BYTE* p = player();
    auto sound = at<int16_t>(rec, record::kSound);
    if (!popup.silent && sound >= 0) {
        reinterpret_cast<SoundFn>(sound_fn_)(
            sound_system_, sound, static_cast<float>(at<double>(p, kX)),
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
    // A received DeathLink: the player dies as from damage, through the shield too. The game's
    // damage code calls the death routine once energy is 0 or below (0x1B9D3A, 0x1F07A,
    // 0x4771B); the helper sets energy to 0 and calls it the same way, once the player has been
    // in play in one cell for a second (not during a load, a respawn or a room change). The
    // routine returns at once while the halt flag is set, which would leave the player alive at
    // 0 energy, so that is refused first. It also posts a message and writes through the
    // result unchecked (0x1DA6E7, 0x1DA6EF), which is NULL when all four message slots are
    // busy; each message cuts the previous one short, and the helper shows one popup at a
    // time, so it waits until the last message has ended.
    if (!configured_) return "refused not configured";
    if (!in_play() || play_since_ms_ == 0 || now_ms - play_since_ms_ < kSettledMs) {
        return "refused not settled";
    }
    if (*halt_flag_ != 0) return "refused halted";
    if (popup_busy()) return "refused message showing";
    BYTE* p = player();
    at<int16_t>(p, kEnergy) = 0;
    reinterpret_cast<DeathFn>(death_fn_)(p, 0);
    return "died";
}

std::string Game::run_item_event(uint8_t item) {
    // A received Energy Shield's effect is its pickup's area event, 3553, type 11, which
    // spawns object 2018, the shield (docs/re/memory.md, "Received Energy Shields"). The
    // helper runs that event as the pickup does (0x1FE85): the handler for its type, from the
    // table the game fills, with a cell and the record; the cell is the player's. Every part
    // is checked against B1 first.
    BYTE* rec = record(item);
    if (rec == nullptr) return "no item record";
    int16_t event = at<int16_t>(rec, record::kEvent);
    BYTE* table = nullptr;
    int16_t events = 0;
    if (event < 0 || !safe_read(event_table_, &table, sizeof(table)) || table == nullptr ||
        !safe_read(event_count_, &events, sizeof(events)) || event >= events) {
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
    void* handler = nullptr;
    if (!safe_read(event_handlers_ + kSpawnEventType * sizeof(void*), &handler, sizeof(handler)) ||
        handler == nullptr) {
        return "no handler";
    }
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
    if (at<void*>(transition_fn_, 0) != newgame_fn_) return;  // no new game queued
    if (at<int32_t>(transition_timer_, 0) <= 1) return;       // too close to being built
    if (at<uint16_t>(rando_marker_, 0) != 1) return;          // a normal new game: not ours
    // The section the seed's mode, unless ARCHIPELAGO started it (starting_): that section was
    // checked before the plan went in, and the plan's own writes (the starting amounts) now
    // differ from the mode's.
    int differs = starting_ ? -1 : first_difference(rando_blob_, newgame_.expect);
    if (differs >= 0) {
        newgame_status_ = "refused the new game is not the seed's mode (section +" +
                          hex_offset(differs) + " differs)";
        disarm_newgame();
        return;
    }
    write_edits(rando_blob_, newgame_.write);
    newgame_status_ = "applied slot " + std::to_string(at<int32_t>(current_slot_, 0) + 1);
    disarm_newgame();
}

void Game::watch_menu(WidgetCallback callback) {
    if (!configured_) return;
    if (newgame_armed_ && !newgame_.menu.empty() && !starting_) {
        choice_.install(menu_, callback);
        if (choice_.installed()) choice_.fit_border(menu_);
    } else if (choice_.installed() || choice_.holds_hook()) {
        choice_.restore(menu_, callback);
    }
}

void Game::restore_menu(WidgetCallback callback) {
    if (configured_) choice_.restore(menu_, callback);
}

void Game::watch_assists() {
    if (!configured_) return;
    if (assists_locked_) {
        // The padlock in the font while armed.
        lock_glyph_.paint(sprite_texture_, gl_, texture_check_);
        assists_.install(menu_);
        return;
    }
    if (assists_.installed()) assists_.restore(menu_);
    if (lock_glyph_.painted()) lock_glyph_.clear(sprite_texture_, gl_, texture_check_);
}

void Game::restore_assists() {
    if (!configured_) return;
    assists_.restore(menu_);
    lock_glyph_.clear(sprite_texture_, gl_, texture_check_);
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
    // The placement reads the flag of the second target's room by its index: it must be one.
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
    int16_t count = at<int16_t>(start_count_, 0);
    if (count == 0) {
        // The menu never opened in this run: build the table as it would, count last.
        for (size_t i = 0; i < rooms.size(); ++i) {
            at<int16_t>(start_cells_, static_cast<ptrdiff_t>(4 * i)) = rooms[i].x;
            at<int16_t>(start_cells_, static_cast<ptrdiff_t>(4 * i + 2)) = rooms[i].y;
            at<uint8_t>(start_flags_, static_cast<ptrdiff_t>(i)) = rooms[i].flag;
        }
        at<int16_t>(start_count_, 0) = static_cast<int16_t>(rooms.size());
        return "";
    }
    if (static_cast<size_t>(count) != rooms.size()) {
        return "the game's start table has " + std::to_string(count) + " rooms, the seed's " +
               std::to_string(rooms.size());
    }
    for (size_t i = 0; i < rooms.size(); ++i) {
        if (at<int16_t>(start_cells_, static_cast<ptrdiff_t>(4 * i)) != rooms[i].x ||
            at<int16_t>(start_cells_, static_cast<ptrdiff_t>(4 * i + 2)) != rooms[i].y ||
            at<uint8_t>(start_flags_, static_cast<ptrdiff_t>(i)) != rooms[i].flag) {
            return "the game's start table differs from the seed's at room " + std::to_string(i);
        }
    }
    return "";
}

void Game::start_archipelago(WidgetCallback callback) {
    // The game calls the choice's callback once per press; a press after the plan was dropped,
    // or while a new game is queued, does nothing.
    if (!configured_ || !newgame_armed_ || newgame_.menu.empty() || starting_) return;
    if (at<void*>(transition_fn_, 0) == newgame_fn_) return;
    starting_ = true;
    // Refused: RANDOMIZER back, the plan dropped, nothing queued.
    auto refuse = [&](const std::string& why) {
        newgame_status_ = "refused " + why;
        choice_.restore(menu_, callback);
        disarm_newgame();
    };
    try {
        std::string table = ensure_start_table();
        if (!table.empty()) return refuse(table);
        int differs = -1;
        {
            // The menu's state is the player's own (the game keeps it in its preferences): set
            // to the seed's mode for the placement, then put back.
            MenuStateGuard guard(randomizer_menu_);
            write_edits(randomizer_menu_, newgame_.menu);
            // START GAME's first step would pick a start; the placement reads the AP seed's
            // instead, so the plan goes in first. Then the placement, which builds the rest of
            // the section from the menu (the location list, the settings, the map tables).
            // START GAME's winnability check is skipped: the AP generator made the seed
            // beatable.
            write_edits(rando_blob_, newgame_.write);
            reinterpret_cast<void (*)()>(placement_fn_)();
            differs = first_difference(rando_blob_, newgame_.expect);
        }
        if (differs >= 0) {
            return refuse("the placement is not the seed's mode (section +" +
                          hex_offset(differs) + " differs)");
        }
        // The AP seed over the placement now, so the game is the seed's even if the plan is
        // dropped before the next frame; then the new game in the chosen slot, as the
        // play-anyway path queues it. watch_newgame reports it in the next frame.
        write_edits(rando_blob_, newgame_.write);
        reinterpret_cast<void (*)()>(rando_newgame_fn_)();
        if (at<void*>(transition_fn_, 0) != newgame_fn_) {
            return refuse("the game did not queue a new game (is the slot empty?)");
        }
        // The game is leaving the file screen: ARCHIPELAGO stays on the label through the fade,
        // and only RANDOMIZER's callback goes back, so the closing menu holds nothing of ours.
        choice_.release(menu_, callback);
    } catch (...) {
        choice_.restore(menu_, callback);
        disarm_newgame();
        throw;
    }
}

void Game::watch_skip() {
    if (!configured_ || (!active_ && !offline_skip_)) return;
    if (skip_frames_-- > 0) return;
    skip_frames_ = kSkipFrames;
    skip_.apply(event_table_, event_count_);
}

void Game::restore_skip() {
    if (configured_) skip_.restore(event_table_);
}

void Game::set_discs(int count, int required, std::string scan_line, bool told) {
    disc_count_ = count;
    discs_required_ = required;
    discs_told_ = told;
    scan_line_ = std::move(scan_line);
    pause_row_.set(scan_line_.empty() ? std::string() : pause_row_text(count, told ? required : 0));
    if (scan_line_.empty() && configured_) mission_.cancel(mission_at_);  // not this seed now
    scan_frames_ = 0;  // checked in the next frame
}

void Game::watch_discs() {
    if (!configured_) return;
    if (scan_frames_-- > 0) return;
    scan_frames_ = kScanFrames;
    if (!scan_line_.empty()) {
        scan_.apply(text_table_, scan_line_);
    } else if (scan_.written()) {
        scan_.restore(text_table_);
    }
}

void Game::restore_discs() {
    if (!configured_) return;
    scan_.restore(text_table_);
    equip_.restore(equip_at_);
    equip_screen_.restore(equip_at_);
    pause_row_.restore(pause_at_);
    disk_glyph_.clear(sprite_texture_, gl_, texture_check_);
}

void Game::watch_equipment() {
    if (!configured_) return;
    equip_screen_.tidy(equip_at_);
    pause_row_.watch(pause_at_);
    if (!scan_line_.empty()) {
        // The disk's icon while a count is shown.
        disk_glyph_.paint(sprite_texture_, gl_, texture_check_);
        equip_.install(equip_at_);
    } else {
        if (disk_glyph_.painted()) disk_glyph_.clear(sprite_texture_, gl_, texture_check_);
        if (equip_.installed()) equip_.restore(equip_at_);
        if (equip_screen_.holds_hook()) equip_screen_.restore(equip_at_);
    }
}

void Game::equipment_built() {
    if (!configured_ || scan_line_.empty()) return;
    std::string why = equip_screen_.add(equip_at_, disc_count_);
    equip_status_ = why.empty() ? "shown " + std::to_string(disc_count_) : "not shown: " + why;
}

void Game::watch_mission() {
    if (!configured_) return;
    // Not over a popup: once none is showing or queued, and the last one a moment ago.
    // Another save, or none: a prompt asked for in the last one is dropped.
    int save = *save_loaded_;
    if (save != mission_save_) {
        if (mission_save_ != -1) mission_.cancel(mission_at_);
        mission_save_ = save;
    }
    bool idle = in_play() && popups_.empty() && !popup_busy() &&
                frames_since_popup_ >= kPopupGapFrames;
    mission_.watch(mission_at_, idle);
}

void Game::watch_files() {
    if (!configured_) return;
    files_.watch(files_at_);
    for (FontGlyph& glyph : disk_header_) {
        if (files_.on()) {
            // DISK over the keys while the count is on.
            glyph.paint(sprite_texture_, gl_, texture_check_);
        } else if (glyph.painted()) {
            glyph.clear(sprite_texture_, gl_, texture_check_);
        }
    }
}

void Game::file_event(void* widget, intptr_t event) {
    if (configured_) files_.on_event(files_at_, widget, event);
}

void Game::restore_files() {
    if (!configured_) return;
    client_file_counts_.clear();
    offline_file_counts_.clear();
    files_.set_counts({});
    files_.restore(files_at_);
    for (FontGlyph& glyph : disk_header_) glyph.clear(sprite_texture_, gl_, texture_check_);
}

bool Game::labels_seed_loaded(bool in_play_only) const {
    if (!labels_seed_ || *save_loaded_ == kNoSaveLoaded || (in_play_only && !in_play()) ||
        at<uint16_t>(rando_marker_, 0) != 1) {
        return false;
    }
    return section_is(*labels_seed_, rando_blob_);
}

void Game::watch_map_names() {
    // The map cursor's name for the stand-ins' squares (mapmarks.h): the map's handler wrapped
    // while this save's labels are known.
    // The map is a menu: the save loaded, in play or not.
    if (configured_) map_names_.watch(map_at_, ap_items_.active() && labels_seed_loaded(false));
}

void Game::map_after() {
    if (configured_) map_names_.after(map_at_, ap_items_.labels());
}

void* Game::map_original(void* child) const {
    void* handler = map_names_.original(child);
    // An object the helper did not wrap: the map's own handler, as the game holds it.
    if (handler == nullptr && map_at_.map_handler != nullptr) {
        safe_read(map_at_.map_handler, &handler, sizeof(handler));
    }
    return handler;
}

void Game::restore_map_names() {
    if (configured_) map_names_.restore(map_at_);
}

void Game::watch_map_marks() {
    constexpr int kMapFrames = 15;
    if (!configured_ || !ap_items_.active()) return;
    if (map_frames_-- > 0) return;
    map_frames_ = kMapFrames;
    // An Archipelago save in use: a randomizer save loaded, as the labels are for (a plain
    // randomizer game may hold the game's own Scrap Chunk).
    if (!labels_seed_loaded(true)) return;
    mark_important(rando_blob_, map_grid_, flags_[flag::kEnding] != 0);
}

void Game::watch_title() {
    // Only with no save loaded (the title, the file select): in play the draw list is long.
    if (!configured_ || *save_loaded_ != kNoSaveLoaded) return;
    std::string why = title_.watch(title_at_, active_);
    if (!why.empty()) title_error_ = why;
    if (title_.shown()) title_error_.clear();
}

void Game::hide_title() {
    if (configured_) title_.hide(title_at_);
}

std::string Game::title_state() const {
    if (!title_error_.empty()) return title_error_;
    return title_.shown() ? "1" : "0";
}

void Game::watch_intel() {
    if (configured_) intel_.watch(item_records_);
}

void Game::restore_intel() {
    if (!configured_) return;
    intel_.set(false);
    intel_.restore(item_records_);
}

void Game::mission_open() {
    if (configured_) mission_.open(mission_at_);
}

void Game::restore_mission() {
    if (configured_) mission_.restore(mission_at_);
}

void Game::equipment_event(void* widget, intptr_t event) {
    if (configured_) equip_screen_.on_event(equip_at_, widget, event);
}

void Game::set_labels(std::map<int, Label> labels) {
    labels_seed_.reset();
    if (!labels.empty() && *save_loaded_ != kNoSaveLoaded &&
        at<uint16_t>(rando_marker_, 0) == 1) {
        labels_seed_ = offline_seed_of(rando_blob_);
    }
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
    BYTE* records = nullptr;
    if (!safe_read(item_records_, &records, sizeof(records)) || records == nullptr) return;
    std::map<int, std::string> names = ap_items_.labels();
    for (auto it = item_labels_.begin(); it != item_labels_.end();) {
        const Label& pending = it->second;
        BYTE record[record::kSize];
        if (!safe_read(records + pending.item * record::kSize, record, sizeof(record))) return;
        std::string name;
        if (!pending.item_name.empty()) {
            name = pending.item_name;  // this player's own, named by the client
        } else if (pending.item == kStandInItem || pending.item == kStandInProgression) {
            // The stand-ins' records are restyled: their own names, as the game loads them.
            name = kStandInNames[pending.item == kStandInItem ? 0 : 1];
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
    if (popup_width_code_ == nullptr) return;
    if (width < 1 || width > kGamePopupWidth) width = kGamePopupWidth;
    if (width == popup_width_) return;  // already so (popup_width_ starts unknown: -1)
    // `mov edx, imm32`: B1's instruction, holding 24 or a width a helper wrote (this one, or
    // one that left without putting 24 back). Anything else is not the code traced: it is
    // left alone, and not counted as ours to put back.
    BYTE now[5] = {};
    int32_t imm = 0;
    if (safe_read(popup_width_code_, now, sizeof(now))) std::memcpy(&imm, now + 1, sizeof(imm));
    if (now[0] != 0xBA || imm < 1 || imm > kGamePopupWidth) {
        popup_width_ = kGamePopupWidth;
        popup_width_code_ = nullptr;
        return;
    }
    popup_width_ = imm;
    if (imm == width) return;
    DWORD old = 0;
    if (!VirtualProtect(popup_width_code_ + 1, sizeof(int32_t), PAGE_EXECUTE_READWRITE, &old)) {
        return;  // not written: popup_width_ says what the game still holds
    }
    int32_t value = width;
    std::memcpy(popup_width_code_ + 1, &value, sizeof(value));
    DWORD unused = 0;
    VirtualProtect(popup_width_code_ + 1, sizeof(int32_t), old, &unused);
    FlushInstructionCache(GetCurrentProcess(), popup_width_code_, sizeof(now));
    popup_width_ = width;
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
    bool world = *save_loaded_ != kNoSaveLoaded && *menu_state_ == 0;
    // This frame's look at the live item objects, taken once for naming the stand-ins and for
    // dressing them (only in the world, as both are): the item pool copied, and the item
    // sprites' frame list.
    const ItemLooks::Places places = looks_places();
    BYTE* base = ItemLooks::item_frame_base(places);
    const BYTE* pool = world ? read_item_pool() : nullptr;
    // The cell whose label sets the popup's width: the player's, or the one the stand-in about to
    // be picked up was named for.
    int width_cell = cell;
    if (world) {
        // Each stand-in named for the location of one of its kind within reach, by the cell the
        // object carries: a drop lands wherever its enemy dies, and a room can span several
        // cells (apitem.h). The general objects among them as of the last scan, below.
        int cells[kStandInCount];
        int touching = name_cells(stand_ins_seen(pool, base), at<double>(player(), kX),
                                  at<double>(player(), kY), cell, ap_items_.labels(), cells);
        if (touching >= 0) width_cell = cells[touching];
        ap_items_.watch(item_records_, cells);
    } else {
        ap_items_.release(item_records_);
    }
    ap_items_.paint(sprite_texture_, gl_, texture_check_, GetTickCount64(), world);
    // The pickup popup as wide as that label (width_cell's), if the stand-ins are ours; the
    // game's 24 elsewhere. Called from the frame call, on the game's thread, so the game is not
    // running the instruction while it changes.
    auto it = label_widths_.find(width_cell);
    set_popup_width(ap_items_.restyled() && it != label_widths_.end() ? it->second
                                                                      : kGamePopupWidth);
    // Other Zexion players' items shown as themselves, only while the stand-ins are ours.
    if (ap_items_.restyled()) {
        looks_.scan(places, base);
        looks_.apply(places, pool, base);
    } else {
        looks_.restore(places);
    }
}

const BYTE* Game::read_item_pool() {
    if (item_pool_ == nullptr) return nullptr;
    pool_copy_.resize(obj::kItemSize * ItemLooks::kSlots);
    return safe_read(item_pool_, pool_copy_.data(), pool_copy_.size()) ? pool_copy_.data()
                                                                       : nullptr;
}

std::vector<StandInSeen> Game::stand_ins_seen(const BYTE* pool, BYTE* base) const {
    // Item objects keep their position in their sprite fields (pixels, as the player's), and the
    // general objects are assumed to; each pool keeps its cell (obj::) (docs/re/memory.md, "Live
    // item objects"). A position off the map is not used.
    constexpr double kMapWidth = kMapSide * 320.0, kMapHeight = kMapSide * 224.0 + 8;
    std::vector<StandInSeen> out;
    auto add = [&](int item, uint8_t row, uint8_t column, const BYTE* o) {
        double x = 0, y = 0;
        std::memcpy(&x, o + sprite::kX, sizeof(x));
        std::memcpy(&y, o + sprite::kY, sizeof(y));
        if (row >= kMapSide || column >= kMapSide || !(x >= 0 && x < kMapWidth) ||
            !(y >= 0 && y < kMapHeight)) {
            return;
        }
        out.push_back({item, row * kMapSide + column, x, y});
    };
    if (item_pool_ != nullptr) {
        if (pool == nullptr) return out;  // not read: none named after an object
        for (int slot = 0; slot < ItemLooks::kSlots; ++slot) {
            const BYTE* o = pool + slot * obj::kItemSize;
            int32_t spawn = 0;
            int16_t record = 0;
            std::memcpy(&spawn, o + obj::kSpawn, sizeof(spawn));
            std::memcpy(&record, o + obj::kRecord, sizeof(record));
            if (spawn == 0 || (record != kStandInItem && record != kStandInProgression)) continue;
            add(record, o[obj::kItemRow], o[obj::kItemColumn], o);
        }
    }
    if (object_pool_ != nullptr && base != nullptr) {
        BYTE o[obj::kGeneralRow + 2];
        for (int slot : looks_.flipped_slots()) {
            // Checked again: the list is from the last scan, up to 30 frames ago.
            if (slot < 0 || slot >= ItemLooks::kObjectSlots ||
                !safe_read(object_pool_ + slot * obj::kGeneralSize, o, sizeof(o)) ||
                !ItemLooks::shows_stand_in(o, base)) {
                continue;
            }
            int32_t frame = 0;
            std::memcpy(&frame, o + sprite::kFrameIndex, sizeof(frame));
            int item = frame == kStandInFrame ? kStandInItem : kStandInProgression;
            add(item, o[obj::kGeneralRow], o[obj::kGeneralColumn], o);
        }
    }
    return out;
}

ItemLooks::Places Game::looks_places() const {
    return {item_pool_,     palette_table_, frame_clock_, sprite_templates_,
            item_records_,  object_pool_};
}

void Game::restore_ap_items() {
    if (!configured_) return;
    looks_.restore(looks_places());  // the objects first, while their records are still ours
    looks_.set_items({});
    ap_items_.restore(item_records_, sprite_texture_, gl_, texture_check_);
    set_popup_width(kGamePopupWidth);
    label_widths_.clear();
    item_labels_.clear();
    labels_.clear();
}

void Game::put_back_all(WidgetCallback callback) {
    disarm_newgame();
    restore_skip();
    set_discs(0, 0, "");
    restore_discs();
    restore_mission();
    restore_files();
    restore_intel();
    restore_map_names();
    restore_ap_items();
    restore_menu(callback);
    set_assists_locked(false);
    restore_assists();
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
    set_disk_slot_counts(std::move(s.file_counts));
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
