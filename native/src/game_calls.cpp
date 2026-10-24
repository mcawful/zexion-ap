#include "game_calls.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace zap {

const char* const kConfigKeys[] = {
    "inventory",     "save_loaded",   "menu_state",    "item_records",   "last_message",
    "weapon_strip_gate", "sound_system", "message_fn", "sound_fn",       "hud_number_fn",
    "hud_layout_fn", "energy_hud_fn", "weapon_strip_fn", "suit_color_fn",
    "rando_blob",    "rando_marker",  "transition_fn", "transition_timer", "current_slot",
    "newgame_fn",
    "current_menu",  "randomizer_menu", "widget_handler_fn", "panel_handler_fn", "rando_menu_fn",
    "placement_fn",  "rando_newgame_fn", "event_table", "event_count",
    "start_count",   "start_cells",     "start_flags",     "sprite_texture",
    "popup_width_code", "item_pool",   "palette_table",   "frame_clock",     "sprite_templates",
    "event_handlers", "death_fn",      "halt_flag",       "assist_menu_fn",
    "object_pool",   "text_table",
    "equipment_fn",  "equip_panel_fn", "widget_move_fn", "text_place_fn",
    "malloc_slot",   "free_slot",     "widget_new_fn", "sprite_sheet_fn", "panel_link_fn",
    "text_set_fn",   "routine_table", "keys_branch",
};
const size_t kConfigKeyCount = sizeof(kConfigKeys) / sizeof(kConfigKeys[0]);

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
constexpr uint8_t kNoSaveLoaded = 0xFF;
constexpr int kItems = 50;  // inventory bytes the helper may write: 0..49
constexpr uint8_t kEnergyTank = 0, kMissileContainer = 1, kMegaBomb = 2, kMagnusArmor = 25;
constexpr uint8_t kMissileBox = 36, kBombPack = 37;
// Item records: 100 bytes each; icon at +0x08, name at +0x0A (50 bytes), sound at +0x5A,
// its volume at +0x5C (docs/re/memory.md, "Game functions the helper calls").
constexpr size_t kRecordSize = 100, kRecordIcon = 0x08, kRecordName = 0x0A, kNameMax = 50;
constexpr size_t kRecordSound = 0x5A, kRecordVolume = 0x5C;
constexpr size_t kRecordItems = 51;
// A message's sequence number and frames left, and the popup's line length, place and time.
constexpr size_t kMessageSequence = 0x1D8, kMessageFramesLeft = 0x1EC;
constexpr int kPopupPerLine = 0x18, kPopupX = 0xA0, kPopupY = 0x58, kPopupFrames = 0xB4;
constexpr int kPopupGapFrames = 20;
constexpr size_t kSuffixMax = 40;
// A notice's text as sent, and its laid-out text (padding included): the game copies a
// message's text into a 256-byte slot with no check, overrunning the next slot past that
// (docs/re/memory.md, "Game functions the helper calls"), so a notice is cut to whole lines
// that fit, the NUL included.
constexpr size_t kNoticeMax = 200;
constexpr size_t kMessageMax = 255;
// The popup message's box: its centre's y (a double) and its half-height (docs/re/memory.md,
// "Game functions the helper calls").
constexpr ptrdiff_t kMessageY = 0x48, kMessageHalfHeight = 0x1FC;

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
constexpr ptrdiff_t kRecordEvent = 0x40;
constexpr ptrdiff_t kEventRecordSize = 0x28;
constexpr uint8_t kSpawnEventType = 11;
constexpr int16_t kShieldObject = 2018;
constexpr uint64_t kSettledMs = 1000;  // in play, in one cell, this long before a shield
// The player's death (docs/re/memory.md, "DeathLink"): death_fn(player, 0), as the game's
// damage code calls it once energy is 0 or below.
using DeathFn = void (*)(void* player, char kind);

template <typename T>
T& at(BYTE* p, ptrdiff_t offset) {
    return *reinterpret_cast<T*>(p + offset);
}

bool parse_number(const std::string& text, uint64_t& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    out = std::strtoull(text.c_str(), &end, text.rfind("0x", 0) == 0 ? 16 : 10);
    return end != nullptr && *end == '\0';
}

// The section of `module` holding `rva`, or nullptr.
const IMAGE_SECTION_HEADER* section_of(HMODULE module, uint64_t rva) {
    auto* base = reinterpret_cast<const BYTE*>(module);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        uint64_t size = (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
        if (rva >= section->VirtualAddress && rva < section->VirtualAddress + size) {
            return section;
        }
    }
    return nullptr;
}

// The keys that name game functions (in code); every other key names data. transition_fn is
// data: it holds the address of the routine the game runs next.
bool is_function(const std::string& key) {
    static const char* const functions[] = {
        "message_fn",    "sound_fn",        "hud_number_fn", "hud_layout_fn",
        "energy_hud_fn", "weapon_strip_fn", "suit_color_fn", "newgame_fn",
        "widget_handler_fn", "panel_handler_fn", "rando_menu_fn", "placement_fn",
        "rando_newgame_fn", "popup_width_code", "death_fn", "assist_menu_fn",
        "equipment_fn", "equip_panel_fn", "widget_move_fn", "text_place_fn",
        "widget_new_fn", "sprite_sheet_fn", "panel_link_fn", "text_set_fn", "keys_branch",
    };
    for (const char* f : functions) {
        if (key == f) return true;
    }
    return false;
}

}  // namespace

std::string parse_config(const std::string& request, std::map<std::string, uint64_t>& out) {
    std::istringstream words(request);
    std::string word;
    words >> word;
    if (word != "config") return "not a config request";
    while (words >> word) {
        size_t eq = word.find('=');
        uint64_t rva = 0;
        if (eq == std::string::npos || !parse_number(word.substr(eq + 1), rva)) {
            return "bad entry " + word;
        }
        out[word.substr(0, eq)] = rva;
    }
    return "";
}

std::string parse_batch(const std::string& request, Batch& out) {
    std::istringstream lines(request);
    std::string line;
    if (!std::getline(lines, line) || line != "apply") return "not an apply request";
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string op;
        words >> op;
        if (op == "cas") {
            unsigned item = 0, expected = 0, value = 0;
            if (!(words >> item >> expected >> value) || item >= kItems || expected > 255 ||
                value > 255) {
                return "bad line " + line;
            }
            out.cas.push_back({static_cast<uint8_t>(item), static_cast<uint8_t>(expected),
                               static_cast<uint8_t>(value)});
        } else if (op == "refresh") {
            out.refresh = true;
        } else if (op == "popup" || op == "notice") {
            // "popup ITEM SUFFIX": the item's icon and name, then the suffix. "notice ITEM
            // TEXT": the text alone (the item's sound); it also keeps colons.
            const bool bare = op == "notice";
            const char* kept = bare ? " .,'!?-:" : " .,'!?-";
            const size_t most = bare ? kNoticeMax : kSuffixMax;
            unsigned item = 0;
            if (!(words >> item) || item >= kRecordItems) return "bad line " + line;
            std::string rest;
            std::getline(words >> std::ws, rest);
            std::string suffix;
            for (char c : rest) {
                auto u = static_cast<unsigned char>(c);
                if (std::isalpha(u)) {
                    suffix += static_cast<char>(std::toupper(u));
                } else if (std::isdigit(u) || (c != 0 && std::strchr(kept, c) != nullptr)) {
                    suffix += c;
                }
                if (suffix.size() >= most) break;
            }
            while (!suffix.empty() && suffix.back() == ' ') suffix.pop_back();
            if (bare && suffix.empty()) return "bad line " + line;
            out.popups.push_back({static_cast<uint8_t>(item), suffix, bare});
        } else if (!op.empty()) {
            return "unknown line " + line;
        }
    }
    return "";
}

namespace {

bool parse_hex(const std::string& text, std::vector<uint8_t>& out) {
    if (text.empty() || text.size() % 2 != 0) return false;
    for (size_t i = 0; i < text.size(); i += 2) {
        unsigned value = 0;
        for (size_t j = i; j < i + 2; ++j) {
            char c = text[j];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        out.push_back(static_cast<uint8_t>(value));
    }
    return true;
}

}  // namespace

std::string parse_newgame(const std::string& request, NewGamePlan& out) {
    std::istringstream lines(request);
    std::string line;
    if (!std::getline(lines, line) || line != "ap_newgame") return "not an ap_newgame request";
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string op, offset_text, hex;
        if (!(words >> op)) continue;
        if (op == "start_table") {
            std::vector<uint8_t> raw;
            if (!(words >> hex) || !parse_hex(hex, raw) || raw.size() % 5 != 0 ||
                raw.size() / 5 > kStartTableMax || !out.start_table.empty()) {
                return "bad line " + line;
            }
            for (size_t i = 0; i < raw.size(); i += 5) {
                StartRoom room;
                std::memcpy(&room.x, raw.data() + i, 2);
                std::memcpy(&room.y, raw.data() + i + 2, 2);
                room.flag = raw[i + 4];
                if (room.x < 0 || room.x >= 54 || room.y < 0 || room.y >= 54 || room.flag > 1) {
                    return "bad line " + line;
                }
                out.start_table.push_back(room);
            }
            continue;
        }
        if (op != "expect" && op != "write" && op != "menu") return "unknown line " + line;
        uint64_t offset = 0;
        SectionBytes edit;
        size_t room = op == "menu" ? kMenuSize : kSectionSize;
        if (!(words >> offset_text >> hex) || !parse_number(offset_text, offset) ||
            !parse_hex(hex, edit.bytes) || offset + edit.bytes.size() > room) {
            return "bad line " + line;
        }
        edit.offset = static_cast<uint16_t>(offset);
        (op == "expect" ? out.expect : op == "write" ? out.write : out.menu)
            .push_back(std::move(edit));
    }
    if (out.write.empty()) return "nothing to write";
    return "";
}

std::string Game::configure(HMODULE image, const std::map<std::string, uint64_t>& rvas) {
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        reinterpret_cast<const BYTE*>(image) +
        reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
    const uint64_t image_size = nt->OptionalHeader.SizeOfImage;
    std::map<std::string, BYTE*> found;
    for (size_t i = 0; i < kConfigKeyCount; ++i) {
        std::string key = kConfigKeys[i];
        auto it = rvas.find(key);
        if (it == rvas.end()) return "missing " + key;
        uint64_t rva = it->second;
        if (rva == 0 || rva + 16 > image_size) return key + " is outside the game";
        const IMAGE_SECTION_HEADER* section = section_of(image, rva);
        if (section == nullptr) return key + " is in no section";
        bool code = (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        if (code != is_function(key)) {
            return key + (code ? " is in code, not data" : " is not in code");
        }
        // The C runtime's import slots are only read, in the read-only data they live in.
        bool import_slot = key == "malloc_slot" || key == "free_slot";
        if (!code && !import_slot && (section->Characteristics & IMAGE_SCN_MEM_WRITE) == 0) {
            return key + " is in read-only data";
        }
        if (key == "rando_blob") {
            uint64_t end = section->VirtualAddress +
                           (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
            if (rva + kSectionSize > end) return key + " leaves no room for the section";
        }
        if (key == "randomizer_menu") {
            uint64_t end = section->VirtualAddress +
                           (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
            if (rva + kMenuSize > end) return key + " leaves no room for the menu's state";
        }
        if (key == "start_cells" || key == "start_flags") {
            uint64_t end = section->VirtualAddress +
                           (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
            uint64_t size = key == "start_cells" ? 4 * kStartTableMax : kStartTableMax;
            if (rva + size > end) return key + " leaves no room for 64 rooms";
        }
        if (key == "inventory") {
            // The whole player state the helper touches must lie in the same section.
            uint64_t end = section->VirtualAddress +
                           (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
            if (rva < section->VirtualAddress + static_cast<uint64_t>(kInventoryInPlayer) ||
                rva - kInventoryInPlayer + kPlayerStateSize > end) {
                return key + " leaves no room for the player state";
            }
        }
        found[key] = reinterpret_cast<BYTE*>(image) + rva;
    }
    base_ = reinterpret_cast<BYTE*>(image);
    inventory_ = found["inventory"];
    save_loaded_ = found["save_loaded"];
    menu_state_ = found["menu_state"];
    item_records_ = found["item_records"];
    last_message_ = found["last_message"];
    weapon_strip_gate_ = found["weapon_strip_gate"];
    sound_system_ = found["sound_system"];
    message_fn_ = found["message_fn"];
    sound_fn_ = found["sound_fn"];
    hud_number_fn_ = found["hud_number_fn"];
    hud_layout_fn_ = found["hud_layout_fn"];
    energy_hud_fn_ = found["energy_hud_fn"];
    weapon_strip_fn_ = found["weapon_strip_fn"];
    suit_color_fn_ = found["suit_color_fn"];
    rando_blob_ = found["rando_blob"];
    rando_marker_ = found["rando_marker"];
    transition_fn_ = found["transition_fn"];
    transition_timer_ = found["transition_timer"];
    current_slot_ = found["current_slot"];
    newgame_fn_ = found["newgame_fn"];
    menu_.current_menu = found["current_menu"];
    menu_.panel_handler = found["panel_handler_fn"];
    menu_.panel_hook = panel_hook_;
    menu_.widget_handler = found["widget_handler_fn"];
    menu_.rando_menu = found["rando_menu_fn"];
    menu_.assist_menu = found["assist_menu_fn"];
    menu_.palette_table = found["palette_table"];
    randomizer_menu_ = found["randomizer_menu"];
    placement_fn_ = found["placement_fn"];
    rando_newgame_fn_ = found["rando_newgame_fn"];
    event_table_ = found["event_table"];
    event_count_ = found["event_count"];
    text_table_ = found["text_table"];
    equip_at_.current_menu = found["current_menu"];
    equip_at_.widget_handler = found["widget_handler_fn"];
    equip_at_.equipment_fn = found["equipment_fn"];
    equip_at_.equip_panel_fn = found["equip_panel_fn"];
    equip_at_.widget_move_fn = found["widget_move_fn"];
    equip_at_.text_place_fn = found["text_place_fn"];
    equip_at_.widget_new_fn = found["widget_new_fn"];
    equip_at_.sprite_sheet_fn = found["sprite_sheet_fn"];
    equip_at_.panel_link_fn = found["panel_link_fn"];
    equip_at_.text_set_fn = found["text_set_fn"];
    equip_at_.malloc_slot = found["malloc_slot"];
    equip_at_.free_slot = found["free_slot"];
    equip_at_.sprite_templates = found["sprite_templates"];
    equip_at_.item_records = found["item_records"];
    equip_at_.hook = equip_hook_;
    equip_at_.panel_hook = equip_panel_hook_;
    mission_at_.current_menu = found["current_menu"];
    mission_at_.transition_fn = found["transition_fn"];
    mission_at_.transition_timer = found["transition_timer"];
    mission_at_.routine_table = found["routine_table"];
    mission_at_.widget_handler = found["widget_handler_fn"];
    mission_at_.text_set_fn = found["text_set_fn"];
    mission_at_.open_hook = mission_open_hook_;
    pause_at_.current_menu = found["current_menu"];
    pause_at_.equipment_fn = found["equipment_fn"];
    pause_at_.equipment_hook = equip_hook_;
    pause_at_.widget_handler = found["widget_handler_fn"];
    pause_at_.sprite_templates = found["sprite_templates"];
    pause_at_.sprite_sheet_fn = found["sprite_sheet_fn"];
    pause_at_.keys_branch = found["keys_branch"];
    mission_at_.yes_hook = mission_yes_hook_;
    start_count_ = found["start_count"];
    start_cells_ = found["start_cells"];
    start_flags_ = found["start_flags"];
    sprite_texture_ = found["sprite_texture"];
    popup_width_code_ = found["popup_width_code"];
    item_pool_ = found["item_pool"];
    palette_table_ = found["palette_table"];
    frame_clock_ = found["frame_clock"];
    sprite_templates_ = found["sprite_templates"];
    event_handlers_ = found["event_handlers"];
    death_fn_ = found["death_fn"];
    object_pool_ = found["object_pool"];
    halt_flag_ = found["halt_flag"];
    gl_ = load_gl();
    configured_ = true;
    return "";
}

BYTE* Game::player() const { return inventory_ - kInventoryInPlayer; }

bool Game::in_play() const {
    return configured_ && *save_loaded_ != kNoSaveLoaded && *menu_state_ == 0 &&
           at<int16_t>(player(), kEnergy) > 0;
}

BYTE* Game::record(uint8_t item) const {
    BYTE* records = at<BYTE*>(item_records_, 0);
    if (records == nullptr || item >= kRecordItems) return nullptr;
    return records + kRecordSize * item;
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
            at<int16_t>(p, kMissiles) += static_cast<int16_t>(missiles - at<int16_t>(p, kMissilesMax));
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

bool Game::popup_busy() const {
    BYTE* last = at<BYTE*>(last_message_, 0);
    if (last == nullptr) return false;
    return at<int32_t>(last, kMessageSequence) == at<int32_t>(last_message_, 8) &&
           at<int32_t>(last, kMessageFramesLeft) > 1;
}

std::vector<std::string> wrap_words(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        while (word.size() > width) {  // longer than a line: cut it
            if (!line.empty()) lines.push_back(line);
            line.clear();
            lines.push_back(word.substr(0, width));
            word.erase(0, width);
        }
        if (word.empty()) continue;
        if (!line.empty() && line.size() + 1 + word.size() > width) {
            lines.push_back(line);
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += word;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::string center_lines(const std::vector<std::string>& lines, int per_line) {
    // The message routine lays text out per_line characters a line and cuts wherever a line
    // ends, with no line-break code, and sizes a one-line box to its text (docs/re/memory.md,
    // "Other players' items"). So with more than one line, each is centered by spaces before it
    // and padded with spaces to the end of its line; one line stays as it is.
    if (lines.size() == 1 || per_line <= 0) return lines.empty() ? std::string() : lines[0];
    const size_t width = static_cast<size_t>(per_line);
    std::string text;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.size() < width) text.append((width - line.size()) / 2, ' ');
        text += line;
        if (i + 1 < lines.size() && text.size() % width != 0) {
            text.append(width - text.size() % width, ' ');
        }
    }
    return text;
}

std::string popup_text(const std::string& head, const std::string& suffix, int per_line,
                       int* width) {
    std::vector<std::string> lines{head};
    if (per_line > 0) {
        for (std::string& line : wrap_words(suffix, static_cast<size_t>(per_line))) {
            lines.push_back(std::move(line));
        }
    }
    size_t widest = 0;
    for (const std::string& line : lines) widest = (std::max)(widest, line.size());
    int w = (std::min)(static_cast<int>(widest), per_line);
    if (w <= 0) w = per_line;
    if (width != nullptr) *width = w;
    return center_lines(lines, w);
}

std::vector<std::string> wrap_hyphenated(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        // A word that fits a line starts a new one if it does not fit this one; a longer one
        // starts where it is and is split, a hyphen ending each line it fills.
        if (word.size() <= width && !line.empty() && line.size() + 1 + word.size() > width) {
            lines.push_back(line);
            line.clear();
        }
        while (word.size() > width && width > 1) {
            std::string head = line.empty() ? std::string() : line + ' ';
            if (head.size() + 2 > width) {  // no room for a letter and the hyphen: a new line
                lines.push_back(line);
                line.clear();
                continue;
            }
            const size_t room = width - head.size();
            lines.push_back(head + word.substr(0, room - 1) + '-');
            line.clear();
            word.erase(0, room - 1);
        }
        if (!line.empty()) line += ' ';
        line += word;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::string notice_text(const std::string& text, int per_line, int* width) {
    if (per_line <= 0) {
        if (width != nullptr) *width = per_line;
        return text;
    }
    std::vector<std::string> lines = wrap_hyphenated(text, static_cast<size_t>(per_line));
    size_t widest = 0;
    for (const std::string& line : lines) widest = (std::max)(widest, line.size());
    // Never wider than asked: the game is only ever given the widths it uses itself.
    const size_t w = widest == 0 ? static_cast<size_t>(per_line)
                                 : (std::min)(widest, static_cast<size_t>(per_line));
    if (width != nullptr) *width = static_cast<int>(w);
    // The game shows a popup of n lines through a window from y = 88 - 4n to 88 + 3n while its
    // text runs to 88 + 4n (measured, docs/re/memory.md, "Game functions the helper calls"), so
    // from 3 lines on the last line is cut. One blank line more (a space past a full last line)
    // makes the window reach far enough for up to 7 lines of text. Lines past what the game's
    // 256-byte message slot holds, the NUL and that space included, are left out.
    const bool blank_line = lines.size() >= 3;
    size_t fit = (kMessageMax - (blank_line ? 1 : 0)) / w;
    if (lines.size() > fit) lines.resize(fit);
    // Left-aligned: each line but the last padded with spaces to the box's width, where the
    // game starts the next line (it cuts every `w` characters, with no line-break code).
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size() || blank_line) out.append(w - lines[i].size(), ' ');
    }
    if (blank_line) out += ' ';
    return out;
}

void fit_notice_box(BYTE* message, const std::string& text, int per_line) {
    // A notice ending in its blank line (notice_text): the game sized its box for that line too,
    // a line's worth of empty box under the text. The box is the message's own: centred on its
    // y (+0x48), opening to a half-height (+0x1FC) of 4 per line past the first plus 7, set from
    // the text's length (0x182A90). Given the half-height for its real lines plus one, and its
    // centre moved 4 up onto them, it fits them with about 4 pixels above and below, the least
    // that still shows every line (docs/re/memory.md, "Game functions the helper calls"). Only a
    // message holding what the game wrote for that text is changed.
    if (message == nullptr || per_line <= 0) return;
    const size_t w = static_cast<size_t>(per_line);
    if (text.size() % w != 1 || text.size() / w < 3) return;
    const int lines = static_cast<int>(text.size() / w);
    int32_t half = 0;
    double y = 0;
    std::memcpy(&half, message + kMessageHalfHeight, sizeof(half));
    std::memcpy(&y, message + kMessageY, sizeof(y));
    if (half != 4 * lines + 7 || !(y == kPopupY - 1)) return;
    half = 4 * lines + 4;
    y -= 4;
    std::memcpy(message + kMessageHalfHeight, &half, sizeof(half));
    std::memcpy(message + kMessageY, &y, sizeof(y));
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
    int per_line = kPopupPerLine;
    std::string text;
    if (popup.bare) {
        text = notice_text(popup.suffix, kPopupPerLine, &per_line);
    } else {
        std::string head;
        if (rec[kRecordIcon] != 0) head += static_cast<char>(rec[kRecordIcon]);
        const char* name = reinterpret_cast<const char*>(rec + kRecordName);
        head.append(name, strnlen(name, kNameMax));
        text = popup_text(head, popup.suffix, kPopupPerLine, &per_line);
    }
    void* message = reinterpret_cast<MessageFn>(message_fn_)(
        text.c_str(), per_line, kPopupX, kPopupY, 0, 0, kPopupFrames);
    if (message == nullptr) return;  // all four slots busy: try again next frame
    if (popup.bare) fit_notice_box(static_cast<BYTE*>(message), text, per_line);
    BYTE* p = player();
    auto sound = at<int16_t>(rec, kRecordSound);
    if (sound >= 0) {
        reinterpret_cast<SoundFn>(sound_fn_)(
            sound_system_, sound, static_cast<float>(at<double>(p, kX)),
            static_cast<float>(at<double>(p, kY)), at<float>(rec, kRecordVolume), -1.0f, 0, 8);
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
    int16_t event = at<int16_t>(rec, kRecordEvent);
    BYTE* table = nullptr;
    int16_t events = 0;
    if (event < 0 || !safe_read(event_table_, &table, sizeof(table)) || table == nullptr ||
        !safe_read(event_count_, &events, sizeof(events)) || event >= events) {
        return "no event";
    }
    BYTE* ev = table + static_cast<ptrdiff_t>(event) * kEventRecordSize;
    BYTE copy[kEventRecordSize];
    if (!safe_read(ev, copy, sizeof(copy))) return "no event";
    int16_t subtype, object, self;
    std::memcpy(&subtype, copy + 2, 2);
    std::memcpy(&object, copy + 4, 2);
    std::memcpy(&self, copy + 0x24, 2);
    if (copy[0] != kSpawnEventType || copy[1] != 0 || subtype != 1 || object != kShieldObject ||
        self != event) {
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
    if (row >= 54 || column >= 54) return "off the map";  // the raw cell can be -1
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
    // After ARCHIPELAGO (starting_), the section was checked before the plan went in, and the
    // plan's own writes (the starting amounts) now differ from the mode's.
    for (const SectionBytes& e : newgame_.expect) {
        if (starting_) break;
        if (std::memcmp(rando_blob_ + e.offset, e.bytes.data(), e.bytes.size()) != 0) {
            char where[16];
            std::snprintf(where, sizeof(where), "0x%X", e.offset);
            newgame_status_ = std::string("refused the new game is not the seed's mode (section +") +
                              where + " differs)";
            disarm_newgame();
            return;
        }
    }
    for (const SectionBytes& e : newgame_.write) {
        std::memcpy(rando_blob_ + e.offset, e.bytes.data(), e.bytes.size());
    }
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
        lock_glyph_.paint(sprite_texture_, gl_);  // the padlock in the font while armed
        assists_.install(menu_);
        return;
    }
    if (assists_.installed()) assists_.restore(menu_);
    if (lock_glyph_.painted()) lock_glyph_.clear(sprite_texture_, gl_);
}

void Game::restore_assists() {
    if (!configured_) return;
    assists_.restore(menu_);
    lock_glyph_.clear(sprite_texture_, gl_);
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
    try {
        std::string table = ensure_start_table();
        if (!table.empty()) {
            newgame_status_ = "refused " + table;
            choice_.restore(menu_, callback);
            disarm_newgame();
            return;
        }
        int differs = -1;
        {
            // The menu's state is the player's own (the game keeps it in its preferences): set
            // to the seed's mode for the placement, then put back.
            MenuStateGuard guard(randomizer_menu_);
            for (const SectionBytes& e : newgame_.menu) {
                std::memcpy(randomizer_menu_ + e.offset, e.bytes.data(), e.bytes.size());
            }
            // START GAME's first step would pick a start; the placement reads the AP seed's
            // instead, so the plan goes in first. Then the placement, which builds the rest of
            // the section from the menu (the location list, the settings, the map tables).
            // START GAME's winnability check is skipped: the AP generator made the seed
            // beatable.
            for (const SectionBytes& e : newgame_.write) {
                std::memcpy(rando_blob_ + e.offset, e.bytes.data(), e.bytes.size());
            }
            reinterpret_cast<void (*)()>(placement_fn_)();
            for (const SectionBytes& e : newgame_.expect) {
                if (std::memcmp(rando_blob_ + e.offset, e.bytes.data(), e.bytes.size()) != 0) {
                    differs = e.offset;
                    break;
                }
            }
        }
        if (differs >= 0) {
            char where[16];
            std::snprintf(where, sizeof(where), "0x%X", static_cast<unsigned>(differs));
            newgame_status_ = std::string("refused the placement is not the seed's mode (section +") +
                              where + " differs)";
            choice_.restore(menu_, callback);
            disarm_newgame();
            return;
        }
        // The AP seed over the placement now, so the game is the seed's even if the plan is
        // dropped before the next frame; then the new game in the chosen slot, as the
        // play-anyway path queues it. watch_newgame reports it in the next frame.
        for (const SectionBytes& e : newgame_.write) {
            std::memcpy(rando_blob_ + e.offset, e.bytes.data(), e.bytes.size());
        }
        reinterpret_cast<void (*)()>(rando_newgame_fn_)();
        if (at<void*>(transition_fn_, 0) != newgame_fn_) {
            newgame_status_ = "refused the game did not queue a new game (is the slot empty?)";
            choice_.restore(menu_, callback);
            disarm_newgame();
            return;
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
    if (!configured_) return;
    if (skip_frames_-- > 0) return;
    skip_frames_ = 30;
    skip_.apply(event_table_, event_count_);
}

void Game::restore_skip() {
    if (configured_) skip_.restore(event_table_);
}

std::string parse_discs(const std::string& request, int& count, int& required, std::string& line,
                        bool& told) {
    count = required = 0;
    told = false;
    line.clear();
    if (request == "discs") return "";
    if (request.rfind("discs ", 0) != 0) return "not a discs request";
    std::istringstream words(request.substr(6));
    std::string hex, extra;
    int told_word = 1;
    std::vector<uint8_t> raw;
    if (!(words >> count >> required >> hex) || count < 0 || count > 255 || required < 1 ||
        required > 255 || !parse_hex(hex, raw)) {
        count = required = 0;
        return "bad discs request";
    }
    if ((words >> told_word && (told_word < 0 || told_word > 1)) || (words >> extra)) {
        count = required = 0;
        return "bad discs request";
    }
    told = told_word == 1;
    std::string text(raw.begin(), raw.end());
    if (!scan_line_ok(text)) {
        count = required = 0;
        return "the scanner's line is not one line of the game's font";
    }
    line = std::move(text);
    return "";
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
    scan_frames_ = 30;
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
    disk_glyph_.clear(sprite_texture_, gl_);
}

void Game::watch_equipment() {
    if (!configured_) return;
    equip_screen_.tidy(equip_at_);
    pause_row_.watch(pause_at_);
    if (!scan_line_.empty()) {
        disk_glyph_.paint(sprite_texture_, gl_);  // the disk's popup icon while the count is on
        equip_.install(equip_at_);
    } else {
        if (disk_glyph_.painted()) disk_glyph_.clear(sprite_texture_, gl_);
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

void Game::mission_open() {
    if (configured_) mission_.open(mission_at_);
}

void Game::restore_mission() {
    if (configured_) mission_.restore(mission_at_);
}

void Game::equipment_event(void* widget, intptr_t event) {
    if (configured_) equip_screen_.on_event(equip_at_, widget, event);
}

Label layout_label(const std::string& first, const std::string& who) {
    std::vector<std::string> shown{first};
    if (!who.empty()) shown.push_back(who);
    size_t widest = 0;
    for (const std::string& part : shown) widest = (std::max)(widest, part.size());
    Label label;
    label.width = (std::min)(static_cast<int>(widest), kPopupPerLine);
    label.name = center_lines(shown, label.width);
    if (label.name.size() > kItemNameMax) label.name.resize(kItemNameMax);
    while (!label.name.empty() && label.name.back() == ' ') label.name.pop_back();
    return label;
}

std::string parse_labels(const std::string& request, std::map<int, Label>& out) {
    std::istringstream lines(request);
    std::string line;
    if (!std::getline(lines, line) || line != "ap_labels") return "not an ap_labels request";
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string op, hex;
        unsigned x = 0, y = 0;
        if (!(words >> op)) continue;
        std::vector<uint8_t> raw;
        if (op != "label" || !(words >> x >> y >> hex) || x >= 54 || y >= 54 ||
            !parse_hex(hex, raw)) {
            return "bad line " + line;
        }
        // Up to two lines (the item, then whom it is for), each kept to the font's characters
        // and to its line: the first after the logo glyph. A first line "@N" names item N of
        // another Zexion player, made from its record later (Game::set_labels); "@N NAME" gives
        // it this name instead of the record's (this player's own OS Install Disk).
        std::vector<std::string> parts{std::string()};
        int item = -1;
        size_t at = 0;
        std::string item_name;
        if (!raw.empty() && raw[0] == '@') {
            int n = 0;
            size_t i = 1;
            while (i < raw.size() && i < 4 && std::isdigit(raw[i])) n = n * 10 + (raw[i++] - '0');
            size_t end = i;
            if (i > 1 && i < raw.size() && raw[i] == ' ') {
                for (end = i + 1; end < raw.size() && raw[end] != '\n'; ++end) {
                    uint8_t c = raw[end];
                    if (std::isalpha(c)) {
                        item_name += static_cast<char>(std::toupper(c));
                    } else if (std::isdigit(c) || (c != 0 && std::strchr(" .,'!?-:()/&",
                                                                          static_cast<char>(c)))) {
                        item_name += static_cast<char>(c);
                    }
                }
            }
            if (i > 1 && n < 51 && (end == raw.size() || raw[end] == '\n')) {
                item = n;
                at = end;
            } else {
                item_name.clear();
            }
        }
        for (size_t k = at; k < raw.size(); ++k) {
            uint8_t c = raw[k];
            if (c == '\n') {
                if (parts.size() == 2) break;
                parts.emplace_back();
                continue;
            }
            std::string& part = parts.back();
            if (std::isalpha(c)) {
                part += static_cast<char>(std::toupper(c));
            } else if (std::isdigit(c) ||
                       (c != 0 && std::strchr(" .,'!?-:()/&", static_cast<char>(c)) != nullptr)) {
                part += static_cast<char>(c);
            }
        }
        std::string first, who;
        {
            std::vector<std::string> wrapped =
                wrap_words(parts[0], static_cast<size_t>(kPopupPerLine) - 1);
            first = wrapped.empty() ? std::string(kApItemName) : wrapped.front();
            first.insert(first.begin(), static_cast<char>(kApItemGlyph));
        }
        if (parts.size() > 1) {
            std::vector<std::string> wrapped =
                wrap_words(parts[1], static_cast<size_t>(kPopupPerLine));
            if (!wrapped.empty()) who = wrapped.front();
        }
        Label label = layout_label(item >= 0 ? std::string(1, static_cast<char>(kApItemGlyph)) +
                                                   kApItemName
                                             : first,
                                   who);
        label.item = item;
        label.who = who;
        label.item_name = item_name;
        out[static_cast<int>(y * 54 + x)] = label;
    }
    return "";
}

void Game::set_labels(std::map<int, Label> labels) {
    if (labels.empty()) {
        restore_ap_items();  // no labels: Scrap Energy is Scrap Energy again
        return;
    }
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
        BYTE record[kRecordSize];
        if (!safe_read(records + pending.item * kRecordSize, record, sizeof(record))) return;
        std::string name;
        if (!pending.item_name.empty()) {
            name = pending.item_name;  // this player's own, named by the client
        } else if (pending.item == kStandInItem || pending.item == kStandInProgression) {
            // The stand-ins' records are restyled: their own names, as the game loads them.
            name = pending.item == kStandInItem ? "SCRAP ENERGY" : "SCRAP CHUNK";
            record[kRecordIcon] = 0;
        } else {
            for (size_t i = 0; i < kNameMax && record[kRecordName + i] != 0; ++i) {
                name += static_cast<char>(record[kRecordName + i]);
            }
        }
        if (name.size() > static_cast<size_t>(kPopupPerLine) - 1) name.resize(kPopupPerLine - 1);
        if (name.empty()) name = kApItemName;  // a record with no name
        uint8_t icon = record[kRecordIcon] != 0 ? record[kRecordIcon]
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
    int cell = row < 54 && column < 54 ? row * 54 + column : -1;
    // Restyled only in the world: menus show the records too (the equipment screen draws Scrap
    // Chunk's frame, the map cursor a record's name), and nothing is picked up in a menu.
    bool world = *save_loaded_ != kNoSaveLoaded && *menu_state_ == 0;
    // The cell whose label sets the popup's width: the player's, or the one the stand-in about to
    // be picked up was named for.
    int width_cell = cell;
    if (world) {
        // Each stand-in named for the location of one of its kind within reach, by the cell the
        // object carries: a drop lands wherever its enemy dies, and a room can span several
        // cells (apitem.h). The general objects among them as of the last scan, below.
        int cells[kStandInCount];
        int touching = name_cells(stand_ins_seen(), at<double>(player(), kX),
                                  at<double>(player(), kY), cell, ap_items_.labels(), cells);
        if (touching >= 0) width_cell = cells[touching];
        ap_items_.watch(item_records_, cells);
    } else {
        ap_items_.release(item_records_);
    }
    ap_items_.paint(sprite_texture_, gl_, GetTickCount64(), world);
    // The pickup popup as wide as that label (width_cell's), if the stand-ins are ours; the game's 24
    // elsewhere. Called from the frame call, on the game's thread, so the game is not running
    // the instruction while it changes.
    auto it = label_widths_.find(width_cell);
    set_popup_width(ap_items_.restyled() && it != label_widths_.end() ? it->second
                                                                      : kGamePopupWidth);
    // Other Zexion players' items shown as themselves, only while the stand-ins are ours.
    if (ap_items_.restyled()) {
        looks_.scan(looks_places());
        looks_.apply(looks_places());
    } else {
        looks_.restore(looks_places());
    }
}

std::vector<StandInSeen> Game::stand_ins_seen() const {
    // Item objects keep their position at +0x40 and +0x48 (doubles, pixels, as the player's),
    // and the general objects are assumed to, sharing the sprite fields; the item pool's cell is
    // at +0x3AC and +0x3AD, the general pool's at +0x210 and +0x211 (docs/re/memory.md, "Live
    // item objects"). A position off the map is not used.
    constexpr ptrdiff_t kObjX = 0x40, kObjY = 0x48, kSpawn = 0x1D8, kRecord = 0x1E8;
    constexpr ptrdiff_t kItemRow = 0x3AC, kItemColumn = 0x3AD;
    constexpr ptrdiff_t kGeneralRow = 0x210, kGeneralColumn = 0x211, kFrameIndex = 0x1C4;
    constexpr size_t kItemSize = 0x3B0, kGeneralSize = 0x5C8;
    constexpr double kMapWidth = 54 * 320.0, kMapHeight = 54 * 224.0 + 8;
    std::vector<StandInSeen> out;
    BYTE* base = ItemLooks::item_frame_base(looks_places());
    auto add = [&](int item, uint8_t row, uint8_t column, const BYTE* o) {
        double x = 0, y = 0;
        std::memcpy(&x, o + kObjX, sizeof(x));
        std::memcpy(&y, o + kObjY, sizeof(y));
        if (row >= 54 || column >= 54 || !(x >= 0 && x < kMapWidth) || !(y >= 0 && y < kMapHeight)) {
            return;
        }
        out.push_back({item, row * 54 + column, x, y});
    };
    if (item_pool_ != nullptr) {
        static BYTE pool[kItemSize * ItemLooks::kSlots];  // the game thread's only
        if (!safe_read(item_pool_, pool, sizeof(pool))) return out;
        for (int slot = 0; slot < ItemLooks::kSlots; ++slot) {
            const BYTE* o = pool + slot * kItemSize;
            int32_t spawn = 0;
            int16_t record = 0;
            std::memcpy(&spawn, o + kSpawn, sizeof(spawn));
            std::memcpy(&record, o + kRecord, sizeof(record));
            if (spawn == 0 || (record != kStandInItem && record != kStandInProgression)) continue;
            add(record, o[kItemRow], o[kItemColumn], o);
        }
    }
    if (object_pool_ != nullptr && base != nullptr) {
        BYTE o[kGeneralRow + 2];
        for (int slot : looks_.flipped_slots()) {
            // Checked again: the list is from the last scan, up to 30 frames ago.
            if (slot < 0 || slot >= ItemLooks::kObjectSlots ||
                !safe_read(object_pool_ + slot * kGeneralSize, o, sizeof(o)) ||
                !ItemLooks::shows_stand_in(o, base)) {
                continue;
            }
            int32_t frame = 0;
            std::memcpy(&frame, o + kFrameIndex, sizeof(frame));
            int item = frame == kStandInFrame ? kStandInItem : kStandInProgression;
            add(item, o[kGeneralRow], o[kGeneralColumn], o);
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
    ap_items_.restore(item_records_, sprite_texture_, gl_);
    set_popup_width(kGamePopupWidth);
    label_widths_.clear();
    item_labels_.clear();
}

}  // namespace zap
