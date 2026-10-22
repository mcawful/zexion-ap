#include "game_calls.h"

#include <cctype>
#include <cstring>
#include <sstream>

namespace zap {

const char* const kConfigKeys[] = {
    "inventory",     "save_loaded",   "menu_state",    "item_records",   "last_message",
    "weapon_strip_gate", "sound_system", "message_fn", "sound_fn",       "hud_number_fn",
    "hud_layout_fn", "energy_hud_fn", "weapon_strip_fn", "suit_color_fn",
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

bool is_function(const std::string& key) {
    return key.size() > 3 && key.compare(key.size() - 3, 3, "_fn") == 0;
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
    if (!batch.cas.empty()) {
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
    std::string text;
    if (rec[kRecordIcon] != 0) text += static_cast<char>(rec[kRecordIcon]);
    const char* name = reinterpret_cast<const char*>(rec + kRecordName);
    text.append(name, strnlen(name, kNameMax));
    if (!popup.suffix.empty()) text += " " + popup.suffix;
    void* message = reinterpret_cast<MessageFn>(message_fn_)(
        text.c_str(), kPopupPerLine, kPopupX, kPopupY, 0, 0, kPopupFrames);
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

}  // namespace zap
