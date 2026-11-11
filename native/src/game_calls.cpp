#include "game_calls.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "iat.h"

namespace zap {

namespace {

// Player state, from the inventory (docs/re/memory.md, "Granting each kind of item, and what
// keeps it", "Game functions the helper calls", "Other players' items").
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
// A received OS Install Disk's popup: the disk glyph and OS INSTALL DISK, as its map label shows
// it, not the record's DATA DISC with no icon (docs/re/memory.md, "The Data Disc").
bool disk_popup(const Popup& popup) { return !popup.bare && popup.item == kDiskItem; }

// The last message's frames left and its sequence beside the pointer to it (docs/re/memory.md,
// "Game functions the helper calls"), and the frame calls between two of the helper's popups.
constexpr size_t kMessageFramesLeft = 0x1EC;
constexpr ptrdiff_t kLastSequence = 8;
constexpr int kPopupGapFrames = 20;
constexpr int kFileSlotFrames = 15;  // frames between looks for the AP saves' slots
constexpr int kScanFrames = 30;  // frames between looks at the scanner's text
constexpr int kNoteFrames = 7;   // frames between looks for the Feats of Natu screen

// The most characters of a translated item name read for a popup when its room is not known.
constexpr int32_t kNameRoom = 64;

using MessageFn = void* (*)(const char* text, int per_line, int x, int y, int, int, int frames);
using SoundFn = void (*)(void* system, short id, float x, float y, float volume, float pitch,
                         short, short category);
using HudNumberFn = void (*)(void* hud_object, double value);
using PlayerFn = void (*)(void* player);
using EnergyHudFn = void (*)(void* player, char force);
using SuitColorFn = void (*)(int slot, short palette);
using EventFn = void (*)(char row, char column, BYTE* event);  // an area-event handler
// A received Energy Shield, Scrap Metal, Scrap Energy or Scrap Chunk runs its pickup's area
// event (Game::run_item_event), each as B1's AreaEvents file holds it, which the game reads into
// the table as is (docs/re/memory.md, "Received Energy Shields" and "Received scrap"): type 11,
// subtype 1, which spawns the object at +4 at the cell's corner plus (5, 8), no sound, then runs
// the event at +0x20 (-1: none) itself.
constexpr uint8_t kScrapMetal = 46, kScrapEnergy = 47, kScrapChunk = 48, kEnergyShield = 50;
static_assert(kScrapEnergy == kStandInItem && kScrapChunk == kStandInProgression,
              "kStandInOwnEvents is in the stand-ins' order");
constexpr uint8_t kSpawnEventType = 11, kFadeEventType = 1;
constexpr ptrdiff_t kEventNext = 0x20;
// The record, but for each item's object (+4), next event (+0x20) and own number (+0x24).
constexpr BYTE kSpawnRecord[event::kSize] = {
    0x0B, 0x00, 0x01, 0x00, 0x00, 0x00, 0xF3, 0x03, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x05, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
// Scrap Chunk's next event, 3552: type 1 (a fade) of layer 7 alone, toward -10 from 8, a step
// every 3 frames; no sound, no next event.
constexpr int16_t kChunkFade = 3552;
constexpr BYTE kChunkFadeRecord[event::kSize] = {
    0x01, 0x00, 0x07, 0x00, 0xF6, 0xFF, 0x08, 0x00, 0x03, 0x00, 0x07, 0x00, 0x00, 0x00,
    0xE9, 0xFF, 0xFD, 0xFF, 0xFC, 0xFF, 0xFC, 0xFF, 0x04, 0x00, 0x04, 0x00, 0xFF, 0xFF,
    0x00, 0x00, 0x80, 0x3F, 0xFF, 0xFF, 0x00, 0x00, 0xE0, 0x0D, 0x00, 0x00};
struct ItemEvent {
    uint8_t item;
    int16_t event, object, next;
};
constexpr ItemEvent kItemEvents[] = {
    {kScrapMetal, 3548, 2010, -1},
    {kScrapEnergy, kStandInOwnEvents[0], 2016, -1},
    {kScrapChunk, kStandInOwnEvents[1], 2017, kChunkFade},
    {kEnergyShield, 3553, 2018, -1},
};
const ItemEvent* item_event(uint8_t item) {
    for (const ItemEvent& e : kItemEvents) {
        if (e.item == item) return &e;
    }
    return nullptr;
}
// A received item whose drop the helper spawns after its popup: the scraps (the shield is the
// client's to ask for, `shield`).
bool has_drop(const Popup& popup) {
    return popup.counted && !popup.bare &&
           (popup.item == kScrapMetal || popup.item == kScrapEnergy || popup.item == kScrapChunk);
}
// A received trap's popup: its name and no icon (it has no record), Scrap Metal's sound.
bool trap_popup(const Popup& popup) { return !popup.bare && trap::is_trap(popup.item); }
constexpr uint64_t kSettledMs = 1000;  // in play in one cell this long: a shield, a drop, a death
// In play this long, in any cell: a trap (not the frame of a load, whose reset would undo it).
constexpr uint64_t kTrapSettledMs = 250;
// The player's death: death_fn(player, 0), as the damage code calls it (docs/re/memory.md,
// "DeathLink").
using DeathFn = void (*)(void* player, char kind);

// The config keys (client/patterns.py target names), in checking order, each with the member it
// fills. Each lies in the image with kKeyBytes there (the offline cache checks a function's first
// 16): functions in code, data in writable data, the CRT's import slots (only read) outside code.
// Every key is required, from a client's config or the offline cache it wrote, but text_cut_code:
// a 1.0.0 cache lacks it, and without it only the cut stays the game's (null: TextCut never
// patches). What adding a key costs a cache written before it: native/README.md, "The pipe".
constexpr uint64_t kKeyBytes = 16;
enum class Held { kCode, kData, kImportSlot };
using Member = BYTE* GameAddresses::*;
struct ConfigKey {
    const char* name;
    Member member;
    Held held;
    uint64_t room;    // bytes from the address that must lie in its section, if not 0
    uint64_t before;  // and bytes before it
    bool optional;    // a config without it configures, the member left null
};
constexpr bool kOptional = true;
constexpr ConfigKey code(const char* name, Member member, bool optional = false) {
    return {name, member, Held::kCode, 0, 0, optional};
}
constexpr ConfigKey data(const char* name, Member member, uint64_t room = 0, uint64_t before = 0,
                         bool optional = false) {
    return {name, member, Held::kData, room, before, optional};
}
constexpr ConfigKey import_slot(const char* name, Member member) {
    return {name, member, Held::kImportSlot, 0, 0, false};
}
using G = GameAddresses;  // for the table's width
constexpr ConfigKey kConfigKeys[] = {
    // The player state the helper touches, all in the inventory's section.
    data("inventory", &G::inventory, kPlayerStateSize - kInventoryInPlayer, kInventoryInPlayer),
    data("save_loaded", &G::save_loaded), data("menu_state", &G::menu_state),
    data("item_records", &G::item_records), data("last_message", &G::last_message),
    data("weapon_strip_gate", &G::weapon_strip_gate), data("sound_system", &G::sound_system),
    code("message_fn", &G::message_fn), code("sound_fn", &G::sound_fn),
    code("hud_number_fn", &G::hud_number_fn), code("hud_layout_fn", &G::hud_layout_fn),
    code("energy_hud_fn", &G::energy_hud_fn), code("weapon_strip_fn", &G::weapon_strip_fn),
    code("suit_color_fn", &G::suit_color_fn), data("rando_blob", &G::rando_blob, kSectionSize),
    data("rando_marker", &G::rando_marker), data("transition_fn", &G::transition_fn),
    data("transition_timer", &G::transition_timer), data("current_slot", &G::current_slot),
    code("newgame_fn", &G::newgame_fn), data("current_menu", &G::current_menu),
    data("randomizer_menu", &G::randomizer_menu, kMenuSize),
    code("widget_handler_fn", &G::widget_handler_fn),
    code("panel_handler_fn", &G::panel_handler_fn), code("rando_menu_fn", &G::rando_menu_fn),
    code("placement_fn", &G::placement_fn), code("rando_newgame_fn", &G::rando_newgame_fn),
    data("event_table", &G::event_table), data("event_count", &G::event_count),
    data("start_count", &G::start_count), data("start_cells", &G::start_cells, 4 * kStartTableMax),
    data("start_flags", &G::start_flags, kStartTableMax),
    data("sprite_texture", &G::sprite_texture), code("popup_width_code", &G::popup_width_code),
    data("item_pool", &G::item_pool), data("palette_table", &G::palette_table),
    data("frame_clock", &G::frame_clock), data("sprite_templates", &G::sprite_templates),
    data("event_handlers", &G::event_handlers), code("death_fn", &G::death_fn),
    data("halt_flag", &G::halt_flag), code("assist_menu_fn", &G::assist_menu_fn),
    // Added after 1.1.0, all three optional (trap.h).
    data("ailments", &G::ailments, sizeof(int16_t), 0, kOptional),
    code("ailments_hit_code", &G::ailments_hit_code, kOptional),
    code("ailments_wave_code", &G::ailments_wave_code, kOptional),
    data("object_pool", &G::object_pool), data("text_table", &G::text_table),
    code("equipment_fn", &G::equipment_fn), code("equip_panel_fn", &G::equip_panel_fn),
    code("widget_move_fn", &G::widget_move_fn), code("text_place_fn", &G::text_place_fn),
    import_slot("malloc_slot", &G::malloc_slot), import_slot("free_slot", &G::free_slot),
    code("widget_new_fn", &G::widget_new_fn), code("sprite_sheet_fn", &G::sprite_sheet_fn),
    code("panel_link_fn", &G::panel_link_fn), code("text_set_fn", &G::text_set_fn),
    data("routine_table", &G::routine_table), data("fade", &G::fade, fade_state::kSize),
    data("fade_level", &G::fade_level), code("keys_branch", &G::keys_branch),
    code("file_panel_fn", &G::file_panel_fn), code("file_entry_fn", &G::file_entry_fn),
    code("copy_entry_fn", &G::copy_entry_fn), code("delete_entry_fn", &G::delete_entry_fn),
    // What the offline look reads whole (offline.h).
    data("map_grid", &G::map_grid, kMapCells), data("flags", &G::flags, kFlagCount),
    data("slot_array", &G::slot_array, slot::kCount * slot::kStride),
    data("rando_slots", &G::rando_slots, slot::kCount * kSectionSize),
    code("text_new_fn", &G::text_new_fn), code("text_link_fn", &G::text_link_fn),
    data("draw_layer", &G::draw_layer), data("map_handler", &G::map_handler),
    code("seen_marker_code", &G::seen_marker_code),
    code("text_cut_code", &G::text_cut_code, kOptional),  // added in 1.1.0
    // Feats and achievements (feats.h). The slot is written while an AP save is loaded.
    code("ending_unlocks", &G::ending_unlocks), code("ending_join", &G::ending_join),
    data("stats_context", &G::stats_context, 3 * sizeof(void*)),
    import_slot("context_init_slot", &G::context_init_slot),
    // The translations (translation.h).
    data("translation", &G::translation, tr::kSize), data("tr_flag", &G::tr_flag),
    data("small_text", &G::small_text), data("small_offsets", &G::small_offsets),
    data("item_names", &G::item_names), data("item_name_offsets", &G::item_name_offsets),
    data("small_count", &G::small_count), data("item_name_count", &G::item_name_count),
    data("hd_messages", &G::hd_messages, hd::kSlots * hd::kSize),
    data("message_slots", &G::message_slots, hd::kSlots * message::kSize),
    code("hd_text_fn", &G::hd_text_fn), code("hd_build_fn", &G::hd_build_fn),
    code("wstring_new_fn", &G::wstring_new_fn),
    // LargeText's count, for the Feats screen's title (feats.h).
    data("large_count", &G::large_count), code("options_back_fn", &G::options_back_fn),
    data("pickup_list", &G::pickup_list, pickups::kSize),
};

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
        return name + " leaves no room in its section";
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
        if (it == rvas.end() && key.optional) continue;  // left null
        std::string why = it == rvas.end() ? std::string("missing ") + key.name
                                           : check_key(image, image_size, key, it->second);
        if (!why.empty()) return why;
        found.*key.member = reinterpret_cast<BYTE*>(image) + it->second;
    }
    base_ = reinterpret_cast<BYTE*>(image);
    // The guard's changes put back at the old addresses before new ones replace them.
    if (configured_ && (found.ending_unlocks != at_.ending_unlocks ||
                        found.ending_join != at_.ending_join || found.flags != at_.flags ||
                        found.stats_context != at_.stats_context ||
                        found.context_init_slot != at_.context_init_slot)) {
        restore_feats();
    }
    feats_.set_image(image,
                     find_import_slot(image, "steam_api64.dll", "SteamInternal_ContextInit"));
    mission_.set_image(image);
    // A client's config: the offline look put back with its own addresses, and the last client's
    // notices withdrawn; the not-connected warning comes again if this client goes this load
    // (as CLIENT CONNECTED. does each time one comes back).
    if (configured_ && active) {
        leave_offline();
        drop_client_notices();
        offline_warned_ = false;
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

bool Game::menu_open() const {
    return configured_ && *at_.save_loaded != kNoSaveLoaded && *at_.menu_state != 0;
}

bool Game::away_from_home() const {
    if (!in_play() || home_row_ < 0 || home_row_ >= kMapSide || home_column_ < 0 ||
        home_column_ >= kMapSide) {
        return false;
    }
    // The raw cell can be -1 (off the map) for a moment: not known, so not away.
    BYTE* p = player();
    const uint8_t at_row = at<uint8_t>(p, kPlayerCellRow);
    const uint8_t at_column = at<uint8_t>(p, kPlayerCellColumn);
    if (at_row >= kMapSide || at_column >= kMapSide) return false;
    return at_row != home_row_ || at_column != home_column_;
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
    // The RETURN HOME ending's cutscenes read as play: the client gives the items next load.
    if (mission_.started()) return "refused RETURN HOME WITH THE DISKS? ending";
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
        // As the pickup routine does (docs/re/memory.md, "Granting each kind of item, and what
        // keeps it").
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
    // Each for this save: kept for its next load, never shown in another (pump_popups), as the
    // client counts them handed over and never sends them again.
    std::shared_ptr<const OfflineSeed> origin;
    if (rando_save_loaded()) {
        origin = std::make_shared<const OfflineSeed>(offline_seed_of(at_.rando_blob));
    }
    for (Popup popup : batch.popups) {
        if (popup.counted) popup.origin = origin;
        popup.intel = batch.intel && !popup.bare && popup.item == kAreaMapItem;
        // A received trap: set off now (pump_traps, this frame), its popup with it: one waiting
        // for another of its kind keeps its popup until it goes off (the owner's choice).
        if (trap_popup(popup) && popup.counted) {
            const uint8_t item = popup.item;
            std::shared_ptr<const OfflineSeed> from = popup.origin;
            traps_.push_back({item, std::move(from), std::move(popup)});
            continue;
        }
        popups_.push_back(std::move(popup));
    }
    // This world's own traps, found in it: set off at once too (pump_traps).
    for (uint8_t item : batch.traps) traps_.push_back({item, origin, std::nullopt});
    return "applied";
}

bool Game::showable(const Popup& popup) const { return origin_loaded(popup.origin); }

bool Game::origin_loaded(const std::shared_ptr<const OfflineSeed>& origin) const {
    return !origin || (rando_save_loaded() && section_is(*origin, at_.rando_blob));
}

bool Game::popup_busy() const { return popup_frames_left() > 1; }

int32_t Game::popup_frames_left() const {
    BYTE* last = at<BYTE*>(at_.last_message, 0);
    if (last == nullptr ||
        at<int32_t>(last, message::kSerial) != at<int32_t>(at_.last_message, kLastSequence)) {
        return 0;
    }
    return at<int32_t>(last, kMessageFramesLeft);
}

void Game::pump_popups() {
    if (frames_since_popup_ < (1 << 20)) ++frames_since_popup_;
    if (popups_.empty() || !in_play()) return;
    if (frames_since_popup_ < kPopupGapFrames || popup_busy()) return;
    // RETURN HOME's fade and prompt; from its YES (the screen kept black or not: watch_mission)
    // and through the ending, whose cutscenes read as play, kept for this save's next load.
    if (mission_.opening() || mission_.ending() || mission_.dark() || mission_.started()) return;
    // The first one for this save (or for any): a client's popups for another save wait for it.
    auto next = std::find_if(popups_.begin(), popups_.end(),
                             [this](const Popup& p) { return showable(p); });
    if (next == popups_.end()) return;
    const Popup& popup = *next;
    // A trap has no record: Scrap Metal's gives its sound (trap_popup).
    const bool is_trap = trap_popup(popup);
    BYTE* rec = record(is_trap ? kScrapMetal : popup.item);
    if (rec == nullptr) {
        popups_.erase(next);
        return;
    }
    // With a translation on, the text goes into a borrowed entry (translation.h), which may wait
    // a frame or two to free; not after hold_translated_popups.
    const std::optional<Loaded> now = loaded_translation(at_);
    const bool translated = translated_popups_ && now && now->on;
    std::wstring hd;
    int where = PopupAreas::kNone;
    if (translated) {
        hd = hd_popup_text(popup, rec, *now);
        where = popup_areas_.reserve(*now, hd.size());
        if (where == PopupAreas::kWait) return;
    }
    int per_line = kGamePopupWidth;
    std::string text;
    if (popup.bare) {
        text = notice_text(in_capitals(popup.text), kGamePopupWidth, &per_line);
    } else {
        std::string head;
        const int stand_in = stand_in_index(popup.item);
        if (stand_in >= 0) {
            // A received stand-in: the game's own name and no icon, not the restyled record's.
            head = kStandInNames[stand_in];
        } else if (is_trap) {
            head = in_capitals(trap::name(popup.item));
        } else if (disk_popup(popup)) {
            head = static_cast<char>(kDiskGlyph) + in_capitals(kDiskName);
        } else {
            if (rec[record::kIcon] != 0) head += static_cast<char>(rec[record::kIcon]);
            head += popup.intel ? in_capitals(kIntelName) : std::string(record_name(rec));
        }
        text = popup_text(head, in_capitals(popup.text), kGamePopupWidth, &per_line);
    }
    BYTE* last_before = nullptr;
    int32_t seq_before = 0;
    last_message(last_before, seq_before);
    void* message = reinterpret_cast<MessageFn>(at_.message_fn)(
        text.c_str(), per_line, kPopupX, kPopupY, 0, 0, kPopupFrames);
    if (message == nullptr) return;  // all four slots busy: try again next frame
    if (popup.bare) fit_notice_box(static_cast<BYTE*>(message), text, per_line);
    // The translation's font; failing that, the pixel text, the slot's last translated mark
    // cleared so it shows (clear_mark checks the flag itself).
    if (!translated || !popup_areas_.show(at_, *now, where, static_cast<BYTE*>(message), hd)) {
        clear_mark(at_, static_cast<BYTE*>(message));
    }
    BYTE* p = player();
    auto sound = at<int16_t>(rec, record::kSound);
    if (popup.counted && sound >= 0) {  // the helper's own notices make no sound
        reinterpret_cast<SoundFn>(at_.sound_fn)(
            at_.sound_system, sound, static_cast<float>(at<double>(p, kX)),
            static_cast<float>(at<double>(p, kY)), at<float>(rec, record::kVolume), -1.0f, 0, 8);
    }
    own_disk_ = disk_popup(popup);
    // A received scrap's drop, as its pickup spawns it: once the player has settled (pump_drops).
    if (has_drop(popup)) drops_.push_back({popup.item, popup.origin});
    popups_.erase(next);
    frames_since_popup_ = 0;
    // The game's last message now, with a new sequence: kept only if this call changed it
    // (docs/re/memory.md, "Game functions the helper calls").
    BYTE* last = nullptr;
    int32_t sequence = 0;
    own_message_ = nullptr;
    if (last_message(last, sequence) && last == message &&
        (last != last_before || sequence != seq_before)) {
        own_message_ = last;
        own_sequence_ = sequence;
    }
}

bool Game::own_popup_up() const {
    BYTE* last = nullptr;
    int32_t sequence = 0;
    return own_message_ != nullptr && last_message(last, sequence) && last == own_message_ &&
           sequence == own_sequence_ && popup_frames_left() > 0;
}

bool Game::last_message(BYTE*& last, int32_t& sequence) const {
    const bool pointer = read(at_.last_message, 0, last);
    return read(at_.last_message, kLastSequence, sequence) && pointer;
}

std::wstring Game::translated_name(int item, bool own, const std::optional<Loaded>& now) const {
    std::optional<std::wstring> held = tr_entries_.original({Table::kItems, item});
    if (own && held) return *held;
    std::wstring name;
    if (now && now->on) read_entry(now->items, item, name, kNameRoom);
    return name;
}

std::wstring Game::hd_label_text(const Label& label, bool first_line, bool icon) const {
    // Another Zexion player's item with no name of the client's: the translation's (ItemNames),
    // the game's own, not a label the helper wrote over a stand-in's (as hd_popup_text).
    // A disguised trap's pickup popup: its own text alone, no icon (the map's cursor, the first
    // line, keeps the disguise).
    if (!first_line && !label.pickup.empty()) {
        std::wstring out = hd_case(at_, label.pickup);
        if (!label.who.empty()) out += L'\n' + hd_case(at_, label.who);
        return out;
    }
    std::wstring first;
    if (label.item >= 0 && label.item_name.empty()) {
        first = translated_name(label.item, true, loaded_translation(at_));
    }
    if (first.empty()) {
        first = hd_case(at_, label.item_name.empty() ? label.first_text() : label.item_name);
    }
    std::wstring out = (icon ? hd_icon(label.glyph) : std::wstring()) + first;
    if (!first_line && !label.who.empty()) out += L'\n' + hd_case(at_, label.who);
    return out;
}

std::wstring Game::hd_popup_text(const Popup& popup, const BYTE* rec, const Loaded& now) const {
    // No line wider than a pixel popup's (8 pixels a character), lines centered (layout_popup).
    FontMetrics metrics;
    const FontMetrics* m = metrics.load(at_) ? &metrics : nullptr;
    constexpr double kCap = kGamePopupWidth * 8.0;
    constexpr size_t kCapCharacters = kGamePopupWidth + kGamePopupWidth / 4;
    if (popup.bare) return layout_popup(L"", hd_case(at_, popup.text), m, kCap, kCapCharacters);
    if (trap_popup(popup)) {
        return layout_popup(hd_case(at_, trap::name(popup.item)), hd_case(at_, popup.text), m,
                            kCap, kCapCharacters);
    }
    if (disk_popup(popup)) {
        return layout_popup(hd_icon(kDiskGlyph) + hd_case(at_, kDiskName),
                            hd_case(at_, popup.text), m, kCap, kCapCharacters);
    }
    const int stand_in = stand_in_index(popup.item);
    // A stand-in's own name, under the label the helper wrote.
    std::wstring name = popup.intel ? hd_case(at_, kIntelName)
                                    : translated_name(popup.item, stand_in >= 0, now);
    if (name.empty()) {
        name = hd_text(stand_in >= 0 ? std::string(kStandInNames[stand_in])
                                     : std::string(record_name(rec)));
    }
    std::wstring head = (stand_in >= 0 ? std::wstring() : hd_icon(rec[record::kIcon])) + name;
    return layout_popup(head, hd_case(at_, popup.text), m, kCap, kCapCharacters);
}

void Game::note_play(uint64_t now_ms) {
    // A shield or a death waits for this, not for a load, a respawn or a room change settling.
    if (!configured_ || !in_play()) {
        play_since_ms_ = 0;
        in_play_since_ms_ = 0;
        return;
    }
    if (in_play_since_ms_ == 0) in_play_since_ms_ = now_ms;
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
    if (mission_.started()) return "refused RETURN HOME WITH THE DISKS? ending";
    std::string why = run_item_event(kEnergyShield);
    return why.empty() ? "spawned" : "refused " + why;
}

std::string Game::kill(uint64_t now_ms) {
    // As the damage code does (docs/re/memory.md, "DeathLink"). death_fn leaves the player alive
    // at 0 while halted, and can fault with every message slot busy: refused while halted or a
    // message shows.
    if (!configured_) return "refused not configured";
    if (!in_play() || play_since_ms_ == 0 || now_ms - play_since_ms_ < kSettledMs) {
        return "refused not settled";
    }
    if (*at_.halt_flag != 0) return "refused halted";
    if (popup_busy()) return "refused message showing";
    // Not between RETURN HOME's fade and its prompt: the prompt would open over the death.
    if (mission_.opening()) return "refused RETURN HOME WITH THE DISKS? opening";
    // Nor in the black after its YES, nor in the ending it started.
    if (mission_.dark() || mission_.started()) return "refused RETURN HOME WITH THE DISKS? ending";
    BYTE* p = player();
    at<int16_t>(p, kEnergy) = 0;
    reinterpret_cast<DeathFn>(at_.death_fn)(p, 0);
    return "died";
}

bool Game::held_for_ending() {
    // The game's ending, whose cutscenes read as play: an ending this load was seen in play
    // before keeps every drop and trap for the next load, as the client holds received items
    // (its goal read false, then true: client/session.py; docs/re/memory.md, "The ending: flag
    // 250"). Read every frame, whether anything waits or not. A save past its ending reads as one
    // throughout, and gets its drops and traps until it plays the ending again (flag 211 set at
    // the ship, or by the escape skip's YES, then cleared as the ending begins). 211 set counts in
    // or out of play: the ship may show no frame in play before the ending.
    const bool ending = ending_flag();
    if (*at_.save_loaded == kNoSaveLoaded) {
        load_before_ending_ = false;
    } else if ((!ending && in_play()) || at_.flags[flag::kFinale] != 0) {
        load_before_ending_ = true;
    }
    return ending && load_before_ending_;
}

void Game::pump_drops(uint64_t now_ms) {
    if (!configured_) return;
    if (held_for_ending() || drops_.empty()) return;
    // As spawn_shield waits: in play in one cell for a second, and not from RETURN HOME's fade
    // through its ending (kept for this save's next load, as the popups are).
    if (!in_play() || play_since_ms_ == 0 || now_ms - play_since_ms_ < kSettledMs) return;
    if (mission_.opening() || mission_.ending() || mission_.dark() || mission_.started()) return;
    auto next = std::find_if(drops_.begin(), drops_.end(),
                             [this](const Drop& d) { return origin_loaded(d.origin); });
    if (next == drops_.end()) return;
    const uint8_t item = next->item;
    std::string why = run_item_event(item);
    if (why == "off the map") return;  // the raw cell can be -1 for a while: tried again
    drops_.erase(next);
    drop_status_ = (why.empty() ? "spawned " : "refused " + why + " ") + std::to_string(item);
}

bool Game::ending_flag() const {
    return *at_.save_loaded != kNoSaveLoaded && at_.flags[flag::kEnding] != 0 &&
           at_.flags[flag::kFinale] == 0;
}

std::string Game::drop_status() const {
    return "queued " + std::to_string(drops_.size()) + "; last " + drop_status_;
}

void Game::pump_traps(uint64_t now_ms) {
    if (!configured_) return;
    if (held_for_ending() || traps_.empty()) return;
    // At once, with its popup (the owner's choice), but in play for a moment (not the frame of a
    // load), not halted, not from RETURN HOME's fade through its ending, and in the save it was
    // for. A state not measured (a cutscene other than the ending's) is not told apart:
    // docs/re/memory.md, "Telling play from menus, the title screen and death".
    if (!in_play() || in_play_since_ms_ == 0 || now_ms - in_play_since_ms_ < kTrapSettledMs) return;
    if (mission_.opening() || mission_.ending() || mission_.dark() || mission_.started()) return;
    if (*at_.halt_flag != 0) return;
    // One of each kind at a time (the owner's choice): a trap waits while one of its kind that a
    // trap started still runs, and starts as it ends; an enemy's own effect does not hold it.
    auto next = std::find_if(traps_.begin(), traps_.end(), [this](const QueuedTrap& t) {
        const bool running = t.item == trap::kDisruption ? trap_hit_ : trap_wave_;
        return origin_loaded(t.origin) && !running;
    });
    if (next == traps_.end()) return;
    const uint8_t item = next->item;
    const std::string outcome = run_trap(item);
    // Its popup next, ahead of the others queued, but after any notices at the head (a disk's
    // count or a chip's hint whose popup has just shown: kept together).
    if (next->popup) {
        auto at = std::find_if(popups_.begin(), popups_.end(),
                               [](const Popup& queued) { return !queued.bare; });
        popups_.insert(at, std::move(*next->popup));
    }
    traps_.erase(next);
    trap_status_ = outcome + " " + std::to_string(item);
    watch_trap_patches();  // before the game's next frame: AILMENTS left out from its first
}

void Game::watch_trap_patches() {
    // Each timer's AILMENTS tests patched while a trap started it and it runs (trap.h), in a
    // save; put back once it has run out, a load reset it, or no save is loaded.
    BYTE* p = player();
    const bool loaded = *at_.save_loaded != kNoSaveLoaded;
    if (!loaded || at<int16_t>(p, trap::kDisruptionTimer) <= 0) trap_hit_ = false;
    if (!loaded || at<int16_t>(p, trap::kDisorientTimer) <= 0) trap_wave_ = false;
    BYTE* hit = at_.ailments_hit_code;
    BYTE* wave = at_.ailments_wave_code;
    const bool hit_reads = trap_hit_ && trap::ailments_reads(hit, trap::kHitJumps, at_.ailments);
    const bool wave_reads =
        trap_wave_ && trap::ailments_reads(wave, trap::kWaveJumps, at_.ailments);
    hit_patch_.watch(hit_reads ? hit : nullptr);
    wave_patch_.watch(wave_reads ? wave : nullptr);
}

std::string Game::trap_status() const {
    return "queued " + std::to_string(traps_.size()) + "; last " + trap_status_;
}

void Game::play_sound(const trap::Sound& sound) {
    BYTE* p = player();
    reinterpret_cast<SoundFn>(at_.sound_fn)(
        at_.sound_system, sound.id, static_cast<float>(at<double>(p, kX)),
        static_cast<float>(at<double>(p, kY)), sound.volume, trap::kPitch, sound.a, sound.b);
}

std::string Game::run_trap(uint8_t item) {
    // The writes of op 4 or op 5 of the type-74 handler (0x47910 to 0x47AE6 on B1), with the
    // state's duration (its random part is 0, so the game's random number is not drawn here),
    // from the player's position rather than the touching object's (docs/re/memory.md,
    // "Ailments: the script step that sets them").
    BYTE* p = player();
    if (item == trap::kDisruption) {
        // The Phazo's contact state plays its sound as it starts (0x43B50), then op 4's timer,
        // with no Energy Shield taking it (the owner's choice) and longer.
        play_sound(trap::kContactSound);
        int16_t& timer = at<int16_t>(p, trap::kDisruptionTimer);
        if (!(trap::kFrames > timer)) return "nothing";
        timer = trap::kFrames;
        trap_hit_ = true;
        return "set";
    }
    // Op 5 (AILMENTS not read: the owner's choice), then the state 2902 chains to, whose sound
    // plays whatever op 5 did.
    const std::string outcome = disorient(p);
    if (outcome == "set") trap_wave_ = true;
    play_sound(trap::kWaveSound);
    return outcome;
}

std::string Game::disorient(BYTE* p) {
    // Nothing while a longer disorientation runs.
    int16_t& timer = at<int16_t>(p, trap::kDisorientTimer);
    if (!(trap::kFrames > timer)) return "nothing";
    if (timer <= 0) {
        int8_t& facing = at<int8_t>(p, trap::kFacing);
        facing = static_cast<int8_t>((facing + 4) % 8);
    }
    int16_t& flash = at<int16_t>(p, trap::kFlash);
    if (flash < trap::kFlashAtLeast) flash = trap::kFlashAtLeast;
    timer = trap::kFrames;
    return "set";
}

std::string Game::run_item_event(uint8_t item) {
    // The item's pickup event, run as the pickup does, its record and every event it runs
    // checked first (docs/re/memory.md, "Received Energy Shields" and "Received scrap").
    const ItemEvent* traced = item_event(item);
    if (traced == nullptr) return "no event traced";
    BYTE* rec = record(item);
    if (rec == nullptr) return "no item record";
    // The stand-ins' records read -1 while the helper restyles them (apitem.cpp, which restyles
    // only a record holding the game's own event).
    int16_t named = at<int16_t>(rec, record::kEvent);
    if (named != traced->event && !(stand_in_index(item) >= 0 && named == -1)) {
        return "event not as traced";
    }
    const int16_t event = traced->event;
    BYTE* table = pointer_at(at_.event_table);
    int16_t events = 0;
    if (table == nullptr || !read(at_.event_count, 0, events) || event >= events ||
        traced->next >= events) {
        return "no event";
    }
    BYTE* ev = table + static_cast<ptrdiff_t>(event) * event::kSize;
    BYTE copy[event::kSize];
    if (!safe_read(ev, copy, sizeof(copy))) return "no event";
    BYTE want[event::kSize];
    std::memcpy(want, kSpawnRecord, sizeof(want));
    std::memcpy(want + event::kArg1, &traced->object, sizeof(int16_t));
    std::memcpy(want + kEventNext, &traced->next, sizeof(int16_t));
    std::memcpy(want + event::kSelf, &event, sizeof(int16_t));
    if (std::memcmp(copy, want, sizeof(copy)) != 0) return "event not as traced";
    BYTE* handler = pointer_at(at_.event_handlers, kSpawnEventType * sizeof(void*));
    if (handler == nullptr) return "no handler";
    if (!in_image_code(reinterpret_cast<HMODULE>(base_), handler)) {
        return "handler not in the game's code";
    }
    if (traced->next == kChunkFade) {
        // The type-11 handler runs it from the table itself (0xF719 to 0xF73F).
        BYTE* fade = table + static_cast<ptrdiff_t>(kChunkFade) * event::kSize;
        if (!safe_read(fade, copy, sizeof(copy)) ||
            std::memcmp(copy, kChunkFadeRecord, sizeof(copy)) != 0) {
            return "event not as traced";
        }
        BYTE* fade_handler = pointer_at(at_.event_handlers, kFadeEventType * sizeof(void*));
        if (fade_handler == nullptr ||
            !in_image_code(reinterpret_cast<HMODULE>(base_), fade_handler)) {
            return "handler not in the game's code";
        }
    }
    BYTE* p = player();
    uint8_t row = at<uint8_t>(p, kPlayerCellRow), column = at<uint8_t>(p, kPlayerCellColumn);
    if (row >= kMapSide || column >= kMapSide) return "off the map";  // the raw cell can be -1
    reinterpret_cast<EventFn>(handler)(static_cast<char>(row), static_cast<char>(column), ev);
    return "";
}

bool Game::arm_newgame(NewGamePlan plan) {
    if (!configured_) return false;
    newgame_ = std::move(plan);
    newgame_armed_ = true;
    newgame_status_ = "armed";
    return true;
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
    if (!newgame_queued()) return;
    if (at<int32_t>(at_.transition_timer, 0) <= 1) return;  // too close to being built
    if (at<uint16_t>(at_.rando_marker, 0) != 1) return;     // a normal new game: not ours
    // From ARCHIPELAGO (starting_): placed and checked already. From RANDOMIZER: it must hold
    // the seed text, then is placed again as the seed.
    if (!starting_) {
        int differs = first_difference(at_.rando_blob, newgame_.identify);
        if (differs >= 0) {
            newgame_status_ = "refused the new game is not this seed (section +" +
                              hex_offset(differs) + " differs)";
            disarm_newgame();
            return;
        }
        // The player's own placement, put back if the seed's is refused (or throws).
        std::vector<BYTE> players(at_.rando_blob, at_.rando_blob + kSectionSize);
        std::string why;
        try {
            why = place_seed();
        } catch (...) {
            std::memcpy(at_.rando_blob, players.data(), kSectionSize);
            disarm_newgame();
            throw;
        }
        if (!why.empty()) {
            std::memcpy(at_.rando_blob, players.data(), kSectionSize);
            newgame_status_ = "refused " + why;
            disarm_newgame();
            return;
        }
    }
    write_edits(at_.rando_blob, newgame_.write);
    newgame_status_ = "applied slot " + std::to_string(at<int32_t>(at_.current_slot, 0) + 1);
    disarm_newgame();
}

void Game::watch_menu(WidgetCallback callback) {
    if (!configured_) return;
    if (newgame_armed_ && newgame_.choice && !starting_) {
        choice_.install(at_, callback);
        if (choice_.installed()) choice_.fit_border(at_);
    } else if (choice_.installed() || choice_.holds_hook()) {
        choice_.restore(at_, callback);
    }
}

void Game::watch_all(WidgetCallback callback) {
    if (!configured_) return;
    translated_popups_ = true;  // an unload refused: popups in the translation's font again
    watch_feats();
    watch_menu(callback);
    watch_discs();
    watch_equipment();
    watch_mission();
    watch_files();
    watch_intel();
    watch_title();
    watch_offline();
    watch_assists();  // after the offline look: a save dressed again is locked the same frame
    watch_map_names();
    watch_ap_items();
    watch_map_marks();
    watch_pickups();
    watch_translation();
    text_cut_.watch(at_.text_cut_code);  // tall popups' lines cut right (text.h)
    watch_trap_patches();                 // AILMENTS left out while a trap runs (trap.h)
}

void Game::watch_pickups() {
    if (!in_play() || !ap_save_loaded()) return;
    BYTE* list = at_.pickup_list;
    BYTE* records = pointer_at(at_.item_records);
    if (records == nullptr) return;
    int8_t& count = at<int8_t>(list, pickups::kCount);
    if (!pickups::count_ok(count)) return;
    for (int item = 0; item < record::kCount && count < pickups::kMax; ++item) {
        // The new game's own starting items: the game never picks them up, so never lists them.
        if (at_.inventory[item] == 0 || rando::started_with(at_.rando_blob, item)) continue;
        uint8_t icon = 0;
        if (!read(records, record::kSize * item + record::kIcon, icon)) return;
        if (!pickups::listed(item, icon)) continue;
        if (std::find(list, list + count, icon) != list + count) continue;
        list[count] = icon;
        ++count;
    }
}

void Game::watch_translation() {
    const std::optional<Loaded> now = loaded_translation(at_);
    popup_areas_.watch(now);
    // The helper's icons in the translation's icon sheets while its text shows (apitem.h).
    if (now && now->on) {
        icon_sheets_.paint(at_.sprite_templates, gl_);
    } else if (icon_sheets_.painted()) {
        icon_sheets_.clear(at_.sprite_templates, gl_);
    }
    std::map<EntryKey, std::wstring> want;
    if (now && now->on) {
        // Each while its record or Text entry is the helper's: the stand-ins' names, INTEL CHIP,
        // the scanner's line (native/README.md, "Translations").
        if (ap_items_.restyled()) {
            for (int i = 0; i < kStandInCount; ++i) {
                if (!stand_in_hd_[i].empty()) {
                    want[{Table::kItems, kStandInRecords[i]}] = stand_in_hd_[i];
                }
            }
        }
        if (intel_.restyled()) want[{Table::kItems, kAreaMapItem}] = hd_case(at_, kIntelName);
        // SmallText 12 matches the Text table's entry 12 (docs/re/memory.md, "The helper's calls
        // in a translation").
        if (scan_.written()) want[{Table::kSmall, kScanTextEntry}] = hd_case(at_, scan_line_);
        hold_stand_in_names(want);
    }
    tr_entries_.watch(now, want);
}

void Game::hold_stand_in_names(std::map<EntryKey, std::wstring>& want) {
    // A stand-in's name held as picked up while its message shows in the AP save, unless another
    // of that record is within reach; with the map open, the game's own (native/README.md,
    // "Translations").
    const bool in_save = *at_.save_loaded != kNoSaveLoaded && ap_save_loaded();
    for (int i = 0; i < kStandInCount; ++i) {
        const EntryKey key{Table::kItems, kStandInRecords[i]};
        NameHold& h = name_holds_[i];
        const std::vector<int32_t> serials = item_name_serials(at_, key.second);
        if (!in_save || stand_in_near_[i] || serials.empty()) {
            h = {};
            continue;
        }
        // Serials count up: the newest is the latest pickup's.
        const int32_t newest = *std::max_element(serials.begin(), serials.end());
        if (h.serial != 0 && newest != h.serial) {
            h = {newest, L""};  // another pickup's: named this frame, held from the next
            continue;
        }
        h.serial = newest;
        if (MapCursorNames::open(at_)) continue;  // the map open: its squares read the game's
        if (h.name.empty()) h.name = tr_entries_.written(key).value_or(L"");
        if (!h.name.empty()) want[key] = h.name;
    }
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

std::string Game::ensure_start_table() {
    // Over a RANDOMIZER game, the game's own table, as START GAME used it (the client's fallback).
    static const std::vector<StartRoom> kNone;
    const std::vector<StartRoom>& rooms = starting_ ? newgame_.start_table : kNone;
    int16_t count = at<int16_t>(at_.start_count, 0);
    // The second target (+0x6CC) is a room's index, whose flag the placement reads: it must be in
    // the table used.
    const size_t rooms_in_table = !rooms.empty() ? rooms.size() : static_cast<size_t>(count);
    constexpr uint16_t kSecondTarget = 0x6CC;
    for (const SectionBytes& e : newgame_.write) {
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
    // Once per press; nothing after the plan was dropped or while a new game is queued.
    if (!configured_ || !newgame_armed_ || !newgame_.choice || starting_) {
        return;
    }
    if (newgame_queued()) return;
    starting_ = true;
    // Refused: RANDOMIZER back, the plan dropped, nothing queued.
    auto refuse = [&](const std::string& why) {
        newgame_status_ = "refused " + why;
        choice_.restore(at_, callback);
        disarm_newgame();
    };
    try {
        std::string why = place_seed();
        if (!why.empty()) return refuse(why);
        // Queued as the play-anyway path does; watch_newgame reports it next frame.
        reinterpret_cast<void (*)()>(at_.rando_newgame_fn)();
        if (!newgame_queued()) {
            return refuse("the game did not queue a new game (is the slot empty?)");
        }
        // ARCHIPELAGO stays through the fade; only RANDOMIZER's callback goes back.
        choice_.release(at_, callback);
    } catch (...) {
        choice_.restore(at_, callback);
        disarm_newgame();
        throw;
    }
}

std::string Game::place_seed() {
    std::string table = ensure_start_table();
    if (!table.empty()) return table;
    // The menu's state is the player's (kept in the preferences): put back however it ends.
    struct MenuBack {
        BYTE* menu;
        BYTE saved[kMenuSize];
        explicit MenuBack(BYTE* m) : menu(m) { std::memcpy(saved, menu, kMenuSize); }
        ~MenuBack() { std::memcpy(menu, saved, kMenuSize); }
        MenuBack(const MenuBack&) = delete;
        MenuBack& operator=(const MenuBack&) = delete;
    };
    int differs;
    {
        MenuBack back(at_.randomizer_menu);
        write_edits(at_.randomizer_menu, newgame_.menu);
        // The plan in place of START GAME's first step (the start), then the placement
        // (docs/re/memory.md, "Starting a new game"). No winnability check: the AP generator made
        // the seed beatable.
        write_edits(at_.rando_blob, newgame_.write);
        reinterpret_cast<void (*)()>(at_.placement_fn)();
        differs = first_difference(at_.rando_blob, newgame_.expect);
    }
    if (differs >= 0) {
        return "the placement is not the seed's (section +" + hex_offset(differs) + " differs)";
    }
    // The seed over the placement now, even if the plan is dropped before the next frame.
    write_edits(at_.rando_blob, newgame_.write);
    return "";
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
        scan_.apply(at_.text_table, in_capitals(scan_line_));
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
    // Also while a received disk's popup waits or shows: after a load the goal may not be on yet.
    const bool disk_popup_due = (own_disk_ && own_popup_up()) ||
                                std::any_of(popups_.begin(), popups_.end(), disk_popup);
    show_glyph(disk_glyph_, !scan_line_.empty() || disk_popup_due);
    if (!scan_line_.empty()) {
        equip_.install(at_);
    } else {
        if (equip_.installed()) equip_.restore(at_);
        if (equip_screen_.holds_hook()) equip_screen_.restore(at_);
    }
}

void Game::options_returned(void* menu) {
    if (configured_) pause_row_.returned(at_, static_cast<BYTE*>(menu), pause_disc_.painted());
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
        if (mission_save_ != -1) {
            mission_.cancel(at_);
            mission_.left_save();  // offered again after an ending started in the last one
        }
        mission_save_ = save;
    }
    // Idle: no showable popup waiting, the helper's last one kPopupGapFrames ago, the game's
    // message down to kFadeMessageLeft frames, the helper's own never cut short. After YES no
    // game message is waited for: the ending ends them (docs/re/memory.md, "The mission prompt");
    // nor queued popups, which wait for the save's next load (pump_popups), as once the ending
    // has started.
    const bool yes = mission_.ending();
    const bool waiting = !yes && std::any_of(popups_.begin(), popups_.end(),
                                             [this](const Popup& p) { return showable(p); });
    const bool own = own_popup_up();
    const int32_t left = own ? 1 : yes ? INT32_MAX : kFadeMessageLeft;
    const bool idle = in_play() && !waiting && popup_frames_left() <= left &&
                      frames_since_popup_ >= kPopupGapFrames;
    mission_.watch(at_, idle, away_from_home());
}

void Game::watch_files() {
    // The AP saves' slots every 16th frame call: a copy or a delete shows on the next look.
    if (file_slot_frames_-- <= 0) {
        file_slot_frames_ = kFileSlotFrames;
        std::set<int> slots = ap_file_slots();
        if (slots != files_.ap_slots()) files_.set_ap_slots(std::move(slots));
    }
    files_.watch(at_);
    for (FontGlyph& glyph : disk_header_) show_glyph(glyph, files_.disks_on());
    // A new game's fade to white leaves the logo alone: it is painted whiter itself (files.h).
    ap_logo_.set_white(new_game_white(newgame_queued(), at<int32_t>(at_.transition_timer, 0)));
    show_glyph(ap_logo_, files_.marks_on());
}

std::set<int> Game::ap_file_slots() const {
    std::set<int> slots;
    for (int n = 1; n <= slot::kCount; ++n) {
        const BYTE* section = slot_section(n);
        if (section != nullptr && known_section(section)) slots.insert(n);
    }
    return slots;
}

const BYTE* Game::slot_section(int n) const {
    // Valid only for a randomizer save in use (docs/re/memory.md, "The file select").
    const BYTE* block = at_.slot_array + static_cast<ptrdiff_t>(n - 1) * slot::kStride;
    if (block[slot::kEmpty] == slot::kEmptyMark ||
        field<uint16_t>(block, slot::kRandoMarker) != 1) {
        return nullptr;
    }
    return at_.rando_slots + static_cast<ptrdiff_t>(n - 1) * kSectionSize;
}

bool Game::known_section(const BYTE* section) const {
    if (labels_seed_ && section_is(*labels_seed_, section)) return true;
    // The offline look's seeds are among the known ones (set_offline adds them).
    for (const OfflineSeed& seed : known_seeds_) {
        if (section_is(seed, section)) return true;
    }
    return false;
}

void Game::file_event(void* widget, intptr_t event) {
    if (configured_) files_.on_event(at_, widget, event);
}

void Game::put_back_files() {
    if (!configured_) return;
    files_.set_ap_slots({});  // found again by the next look
    files_.restore(at_);
    for (FontGlyph& glyph : disk_header_) glyph.clear(at_.sprite_texture, gl_, texture_check_);
    ap_logo_.clear(at_.sprite_texture, gl_, texture_check_);
    file_slot_frames_ = 0;
}

bool Game::rando_save_loaded() const {
    return *at_.save_loaded != kNoSaveLoaded && at<uint16_t>(at_.rando_marker, 0) == 1;
}

bool Game::ap_save_loaded() const {
    return configured_ && rando_save_loaded() && known_section(at_.rando_blob);
}

void Game::add_known_seeds(const std::vector<OfflineSeed>& seeds) {
    constexpr size_t kMaxKnownSeeds = 1024;
    for (const OfflineSeed& seed : seeds) {
        bool known = std::any_of(known_seeds_.begin(), known_seeds_.end(), [&](const auto& k) {
            return offline_matches(k, seed);
        });
        if (known) continue;
        OfflineSeed key;
        key.locations = seed.locations;
        std::memcpy(key.table, seed.table, sizeof(key.table));
        std::memcpy(key.start, seed.start, sizeof(key.start));
        known_seeds_.push_back(std::move(key));
    }
    if (known_seeds_.size() > kMaxKnownSeeds) {
        known_seeds_.erase(known_seeds_.begin(),
                           known_seeds_.end() - static_cast<ptrdiff_t>(kMaxKnownSeeds));
    }
}

void Game::watch_feats() {
    // A load writes the section and the marker in one game call, so a frame call sees both or
    // neither (docs/re/memory.md, "The loaded save's randomizer section is at +0xCDD6D0").
    const bool on = ap_save_loaded();
    // The AP saves' slots for the stub's load test (feats.h): looked for with no save loaded,
    // kept while one loads, dropped once a frame call finds another save.
    if (*at_.save_loaded == kNoSaveLoaded) {
        if (feats_slot_frames_-- <= 0) {
            feats_slot_frames_ = kFileSlotFrames;
            feats_slots_ = ap_file_slots();
        }
    } else {
        feats_slot_frames_ = 0;
        if (!on) feats_slots_.clear();
    }
    const bool was_patched = feats_.ending_patched();
    feats_.watch(at_, on, feats_slots_);
    // An AP ending jumped over: the save left with the patch in place, flag 250 set, 251 clear.
    // Ended by a save loaded, or the Feats of Natu screen gone once seen.
    if (*at_.save_loaded != kNoSaveLoaded) {
        feats_ended_ = false;
        return;
    }
    if (was_patched && !on && at_.flags[flag::kEnding] != 0 && at_.flags[flag::kFeat] == 0) {
        feats_ended_ = true;
        note_seen_ = false;
    }
    if (note_frames_-- > 0) return;
    note_frames_ = kNoteFrames;
    const bool up = feats_screen_note(at_, feats_ended_);
    if (up) {
        note_seen_ = true;
    } else if (note_seen_) {
        feats_ended_ = false;
        note_seen_ = false;
    }
}

void Game::restore_feats() {
    feats_slots_.clear();
    feats_slot_frames_ = 0;
    if (!configured_) return;
    feats_.restore();
    feats_screen_note(at_, false);  // the screen's note blanked, if it is up
}

bool Game::labels_seed_loaded() const {
    return labels_seed_ && rando_save_loaded() && section_is(*labels_seed_, at_.rando_blob);
}

void Game::watch_map_names() {
    // The map is a menu: the save loaded, in play or not.
    map_names_.watch(at_, ap_items_.active() && labels_seed_loaded());
}

void Game::map_after() {
    if (!configured_) return;
    map_names_.after(at_, ap_items_.labels(),
                     [this](const Label& label) { return hd_label_text(label, true, false); });
}

void* Game::map_original(void* child) const {
    void* handler = map_names_.original(child);
    // Not wrapped: the map's own handler, as the game holds it (none before configure).
    if (handler == nullptr) handler = pointer_at(at_.map_handler);
    return handler;
}

void Game::watch_map_marks() {
    constexpr int kMapFrames = 15;
    const bool seed = ap_items_.active() && labels_seed_loaded();
    // The write and the passes put back the first frame out of the seed's save: a plain
    // randomizer game's Scrap Chunks are minors.
    if (!seed) {
        seen_marker_.restore();
        map_seed_passes_ = 0;
    }
    if (!ap_items_.active()) return;
    if (map_frames_-- > 0) return;
    map_frames_ = kMapFrames;
    // Once two passes in a row read the seed's save (a precaution: native/README.md, "The map").
    if (!seed) return;
    if (map_seed_passes_ < 2) ++map_seed_passes_;
    if (map_seed_passes_ < 2) return;
    // Before the raise, so an item on screen stays raised.
    seen_marker_.watch(at_.seen_marker_code);
    mark_important(at_.rando_blob, at_.map_grid, at_.flags[flag::kEnding] != 0);
}

void Game::watch_title() {
    // Only with no save loaded (the title, the file select): in play the draw list is long.
    if (*at_.save_loaded != kNoSaveLoaded) return;
    std::string why = title_.watch(at_, active_, mixed_case(at_));
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

bool Game::request_mission(int column, int row) {
    if (!configured_) return false;
    // A home off the map would never lapse: refused (parse_mission refuses it first).
    if (column < 0 || row < 0 || column >= kMapSide * kCellColumns || row >= kMapSide * kCellRows) {
        return false;
    }
    // Home's cell as the player state counts it: the row then the column of cells.
    home_row_ = row / kCellRows;
    home_column_ = column / kCellColumns;
    mission_.request();
    return true;
}

bool Game::ending_pending() const {
    if (!configured_) return false;
    // The black after YES too, until faded back in (with a menu open, after it closes).
    return mission_.ending() || mission_.dark() ||
           (feats_.ending_patched() && mission_.ending_queued(at_));
}

void Game::mission_after_no() {
    if (configured_) mission_.after_no(at_);
}

void Game::mission_open() {
    if (!configured_) return;
    // Not in play when the opener runs (the player died during the fade): asked again later.
    if (!in_play()) {
        mission_.put_off(at_);
        return;
    }
    mission_.open(at_);
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
    std::map<int, int> items;
    for (const auto& [cell, label] : labels) {
        if (label.item >= 0) items[cell] = label.item;
    }
    looks_.set_items(std::move(items));
    ap_items_.set_labels(std::move(labels));
    resolve_item_labels();
}

void Game::resolve_item_labels() {
    // Another Zexion player's item: its icon and the game's name, once the records are loaded.
    if (!configured_) return;
    BYTE* records = nullptr;
    for (auto& [cell, label] : ap_items_.labels()) {
        if (!label.pending()) continue;
        if (records == nullptr) records = pointer_at(at_.item_records);
        BYTE record[record::kSize];
        if (records == nullptr ||
            !safe_read(records + label.item * record::kSize, record, sizeof(record))) {
            return;
        }
        std::string name;
        if (!label.item_name.empty()) {
            name = label.item_name;  // this player's own, named by the client
        } else if (stand_in_index(label.item) >= 0) {
            // The stand-ins' records are restyled: their own names, as the game loads them.
            name = kStandInNames[stand_in_index(label.item)];
            record[record::kIcon] = 0;
        } else {
            name = record_name(record);
        }
        const size_t room = static_cast<size_t>(kGamePopupWidth) - 1;  // after the icon
        if (name.size() > room) name.resize(room);
        if (name.empty()) name = kApItemName;  // a record with no name
        label.glyph = record[record::kIcon] != 0 ? record[record::kIcon]
                      : label.item == kDiskItem  ? kDiskGlyph  // the helper's (equip.h)
                                                 : kApItemGlyph;
        label.first = std::move(name);
        label.lay_out();
    }
}

void Game::set_popup_width(int width) {
    if (at_.popup_width_code == nullptr) return;
    if (width < 1 || width > kGamePopupWidth) width = kGamePopupWidth;
    if (width == popup_width_) return;  // already so (popup_width_ starts unknown: -1)
    // B1's `mov edx, imm32`, holding 24 or a width a helper wrote. Anything else is not the code
    // traced: left alone, not ours to put back.
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
    std::fill(std::begin(stand_in_near_), std::end(stand_in_near_), false);
    if (!configured_ || !ap_items_.active()) return;
    resolve_item_labels();
    // The player's map cell (docs/re/memory.md, "Other players' items").
    uint8_t row = at<uint8_t>(player(), kPlayerCellRow);
    uint8_t column = at<uint8_t>(player(), kPlayerCellColumn);
    int cell = row < kMapSide && column < kMapSide ? row * kMapSide + column : -1;
    // Restyled only in the world: menus show the records too (the equipment screen Scrap Chunk's
    // frame, the map cursor a name), and nothing is picked up in a menu.
    bool world = *at_.save_loaded != kNoSaveLoaded && *at_.menu_state == 0;
    // One read of the item pool a frame, shared by naming and dressing (world only).
    BYTE* base = ItemLooks::item_frame_base(at_);
    const BYTE* pool = world ? read_item_pool() : nullptr;
    // The cell whose label sets the popup's width: the player's, or the touched stand-in's.
    int width_cell = cell;
    if (world) {
        // Each stand-in named by its object's cell (a drop lands where its enemy died; apitem.h).
        int cells[kStandInCount];
        int touching = name_cells(stand_ins_seen(pool, base), at<double>(player(), kX),
                                  at<double>(player(), kY), cell, ap_items_.labels(), cells,
                                  stand_in_near_);
        if (touching >= 0) width_cell = cells[touching];
        ap_items_.watch(at_.item_records, cells);
        // Each record's translated name (watch_translation), centered where the spaces fit the
        // entry's room, else as it is so no name is cut (native/README.md, "Translations").
        FontMetrics metrics;
        const FontMetrics* m = translation_on(at_) && metrics.load(at_) ? &metrics : nullptr;
        const std::optional<Loaded> now = loaded_translation(at_);
        for (int i = 0; i < kStandInCount; ++i) {
            auto label = ap_items_.labels().find(cells[i]);
            if (label == ap_items_.labels().end()) {
                stand_in_hd_[i] = hd_case(at_, kApItemName);
                continue;
            }
            std::wstring name = hd_label_text(label->second, false, true);
            std::wstring centered = centered_lines(name, m);
            const int32_t room = now ? entry_room(now->items, kStandInRecords[i]) : 0;
            const bool fits = room > 0 && centered.size() + 1 <= static_cast<size_t>(room);
            stand_in_hd_[i] = fits ? std::move(centered) : std::move(name);
        }
    } else {
        ap_items_.release(at_.item_records);
        for (std::wstring& name : stand_in_hd_) name.clear();
    }
    ap_items_.paint(at_.sprite_texture, gl_, texture_check_, GetTickCount64(), world);
    // The pickup popup as wide as width_cell's label while the stand-ins are ours, else 24; from
    // the frame call, so the game is not running the instruction while it changes.
    auto it = ap_items_.labels().find(width_cell);
    set_popup_width(ap_items_.restyled() && it != ap_items_.labels().end() ? it->second.width
                                                                           : kGamePopupWidth);
    // Other Zexion players' items shown as themselves, only while the stand-ins are ours.
    if (ap_items_.restyled()) {
        looks_.scan(at_, base);
        looks_.hide_marker(at_, base);  // whatever the labels: the logo is painted
        looks_.apply(at_, pool, base);
        looks_.own_colors(at_, pool, base);
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
    // Positions from the sprite fields (assumed for the general objects), cells from each pool
    // (docs/re/memory.md, "Live item objects", "Other players' items"); none off the map.
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
        BYTE o[obj::kFirstState + 2];  // through the cell (+0x210) and the first state
        static_assert(obj::kFirstState > obj::kGeneralColumn);
        for (int slot : looks_.flipped_slots()) {
            // Checked again: the list is up to 30 frames old. A marker is no stand-in.
            if (slot < 0 || slot >= ItemLooks::kObjectSlots ||
                !safe_read(at_.object_pool + slot * obj::kGeneralSize, o, sizeof(o)) ||
                !ItemLooks::shows_stand_in(o, base) || looks_.is_marker(slot, o, at_)) {
                continue;
            }
            // A logo frame (shows_stand_in): which stand-in it is.
            int item = kStandInRecords[logo_index(field<int32_t>(o, sprite::kFrameIndex))];
            add(item, o[obj::kGeneralRow], o[obj::kGeneralColumn], o);
        }
    }
    return out;
}

void Game::put_back_ap_items() {
    for (std::wstring& name : stand_in_hd_) name.clear();
    if (!configured_) return;
    looks_.restore(at_);  // the objects first, while their records are still ours
    ap_items_.restore(at_.item_records, at_.sprite_texture, gl_, texture_check_);
    seen_marker_.restore();
    set_popup_width(kGamePopupWidth);
}

void Game::restore_ap_items() {
    put_back_ap_items();
    looks_.set_items({});
    ap_items_.set_labels({});
}

void Game::put_back_save_look() {
    put_back_ap_items();
    if (!configured_) return;
    intel_.restore(at_.item_records);
    restore_assists();
    restore_discs();
}

void Game::drop_save_look() {
    restore_ap_items();
    restore_intel();
    set_assists_locked(false);
    restore_assists();
    set_discs(0, 0, "");
    restore_discs();
}

void Game::put_back_game(WidgetCallback callback) {
    // A prompt asked for or opening, asked for again once put back (not a NO's closed one).
    const bool prompt = mission_.on_its_way();
    put_back_save_look();
    if (configured_) {
        mission_.restore(at_);
        map_names_.restore(at_);
        choice_.restore(at_, callback);
    }
    put_back_files();
    leave_offline();
    text_cut_.restore();
    // A running trap's AILMENTS patches stay: they are the trap's, which outlives the client, as
    // queued popups do; an unload is refused while one is held (unload_refusal), then puts them
    // back (put_back_traps).
    if (configured_) {
        // The translation's entries back; the popup areas emptied once free (unload waits).
        const std::optional<Loaded> now = loaded_translation(at_);
        tr_entries_.restore(now);
        for (NameHold& h : name_holds_) h = {};  // nothing held to write back after
        popup_areas_.restore(now);
        icon_sheets_.clear(at_.sprite_templates, gl_);
        if (prompt) mission_.request();
    }
}

void Game::forget_client() {
    disarm_newgame();
    drop_save_look();  // with the disks, a RETURN HOME prompt asked for (set_discs)
    client_file_counts_.clear();
    apply_file_counts();
}

void Game::put_back_traps() {
    hit_patch_.restore();  // AILMENTS applies again to what is left of a trap (trap.h)
    wave_patch_.restore();
    trap_hit_ = trap_wave_ = false;
}

void Game::client_gone(WidgetCallback callback) {
    put_back_game(callback);
    forget_client();
    drop_client_notices();
    active_ = false;
}

}  // namespace zap
