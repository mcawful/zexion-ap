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
    "popup_width_code",
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

using MessageFn = void* (*)(const char* text, int per_line, int x, int y, int, int, int frames);
using SoundFn = void (*)(void* system, short id, float x, float y, float volume, float pitch,
                         short, short category);
using HudNumberFn = void (*)(void* hud_object, double value);
using PlayerFn = void (*)(void* player);
using EnergyHudFn = void (*)(void* player, char force);
using SuitColorFn = void (*)(int slot, short palette);

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
        "rando_newgame_fn", "popup_width_code",
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
        } else if (op == "popup") {
            unsigned item = 0;
            if (!(words >> item) || item >= kRecordItems) return "bad line " + line;
            std::string rest;
            std::getline(words >> std::ws, rest);
            std::string suffix;
            for (char c : rest) {
                auto u = static_cast<unsigned char>(c);
                if (std::isalpha(u)) {
                    suffix += static_cast<char>(std::toupper(u));
                } else if (std::isdigit(u) || std::strchr(" .,'!?-", c) != nullptr) {
                    suffix += c;
                }
                if (suffix.size() >= kSuffixMax) break;
            }
            while (!suffix.empty() && suffix.back() == ' ') suffix.pop_back();
            out.popups.push_back({static_cast<uint8_t>(item), suffix});
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
        if (!code && (section->Characteristics & IMAGE_SCN_MEM_WRITE) == 0) {
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
    menu_.widget_handler = found["widget_handler_fn"];
    menu_.rando_menu = found["rando_menu_fn"];
    randomizer_menu_ = found["randomizer_menu"];
    placement_fn_ = found["placement_fn"];
    rando_newgame_fn_ = found["rando_newgame_fn"];
    event_table_ = found["event_table"];
    event_count_ = found["event_count"];
    start_count_ = found["start_count"];
    start_cells_ = found["start_cells"];
    start_flags_ = found["start_flags"];
    sprite_texture_ = found["sprite_texture"];
    popup_width_code_ = found["popup_width_code"];
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
    std::string head;
    if (rec[kRecordIcon] != 0) head += static_cast<char>(rec[kRecordIcon]);
    const char* name = reinterpret_cast<const char*>(rec + kRecordName);
    head.append(name, strnlen(name, kNameMax));
    int per_line = kPopupPerLine;
    std::string text = popup_text(head, popup.suffix, kPopupPerLine, &per_line);
    void* message = reinterpret_cast<MessageFn>(message_fn_)(
        text.c_str(), per_line, kPopupX, kPopupY, 0, 0, kPopupFrames);
    if (message == nullptr) return;  // all four slots busy: try again next frame
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
    } else if (choice_.installed()) {
        choice_.restore(menu_, callback);
    }
}

void Game::restore_menu(WidgetCallback callback) {
    if (configured_) choice_.restore(menu_, callback);
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
        // and to its line: the first after the logo glyph.
        std::vector<std::string> parts{std::string()};
        for (uint8_t c : raw) {
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
        std::vector<std::string> shown;
        for (size_t i = 0; i < parts.size(); ++i) {
            size_t width = static_cast<size_t>(kPopupPerLine) - (i == 0 ? 1 : 0);
            std::vector<std::string> wrapped = wrap_words(parts[i], width);
            std::string text = wrapped.empty() ? std::string() : wrapped.front();
            if (i == 0 && text.empty()) text = kApItemName;
            if (i == 0) text.insert(text.begin(), static_cast<char>(kApItemGlyph));
            if (!text.empty()) shown.push_back(text);
        }
        size_t widest = 0;
        for (const std::string& part : shown) widest = (std::max)(widest, part.size());
        int width = static_cast<int>(widest);
        std::string name = center_lines(shown, width);
        if (name.size() > kItemNameMax) name.resize(kItemNameMax);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        out[static_cast<int>(y * 54 + x)] = {name, width};
    }
    return "";
}

void Game::set_labels(std::map<int, Label> labels) {
    if (labels.empty()) {
        restore_ap_items();  // no labels: Scrap Energy is Scrap Energy again
        return;
    }
    std::map<int, std::string> names;
    label_widths_.clear();
    for (auto& [cell, label] : labels) {
        label_widths_[cell] = label.width;
        names[cell] = std::move(label.name);
    }
    ap_items_.set_labels(std::move(names));
}

void Game::set_popup_width(int width) {
    if (width == popup_width_ || popup_width_code_ == nullptr) return;
    if (width < 1 || width > kGamePopupWidth) width = kGamePopupWidth;
    // `mov edx, imm32`: B1's instruction, holding what this helper last wrote (24 at first);
    // anything else is not the code traced, and is left alone.
    BYTE now[5] = {};
    if (!safe_read(popup_width_code_, now, sizeof(now)) || now[0] != 0xBA) return;
    int32_t imm = 0;
    std::memcpy(&imm, now + 1, sizeof(imm));
    if (imm != popup_width_) return;
    DWORD old = 0;
    if (!VirtualProtect(popup_width_code_ + 1, sizeof(int32_t), PAGE_EXECUTE_READWRITE, &old)) {
        return;
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
    // The player's map cell, as the player update keeps it: the row then the column, the
    // grid's index row * 54 + column (docs/re/memory.md, "Other players' items").
    uint8_t row = at<uint8_t>(player(), kPlayerCellRow);
    uint8_t column = at<uint8_t>(player(), kPlayerCellColumn);
    int cell = row < 54 && column < 54 ? row * 54 + column : -1;
    ap_items_.watch(item_records_, cell);
    ap_items_.paint(sprite_texture_, gl_, GetTickCount64());
    // The pickup popup as wide as this cell's label, if the stand-ins are ours; the game's 24
    // elsewhere. Called from the frame call, on the game's thread, so the game is not running
    // the instruction while it changes.
    auto it = label_widths_.find(cell);
    set_popup_width(ap_items_.restyled() && it != label_widths_.end() ? it->second
                                                                      : kGamePopupWidth);
}

void Game::restore_ap_items() {
    if (!configured_) return;
    ap_items_.restore(item_records_, sprite_texture_, gl_);
    set_popup_width(kGamePopupWidth);
    label_widths_.clear();
}

}  // namespace zap
