#include "pause.h"

#include <cstring>

#include "menu.h"  // safe_read, writable

namespace zap {

namespace {

constexpr uint8_t kJne = 0x75, kJmp = 0xEB, kSkip = 0x2E;
constexpr ptrdiff_t kMenuPanelCount = 0x08;
constexpr ptrdiff_t kPanelChildren = 0x1E0, kPanelCapacity = 0x1E8;
constexpr ptrdiff_t kPanelSprites = 0x1F0, kPanelSpriteCount = 0x1F8;
constexpr ptrdiff_t kReleased = 0x1C8, kChildLabel = 0x1F0, kChildCallback = 0x2C0;
constexpr ptrdiff_t kChildHandler = 0x2C8;
constexpr ptrdiff_t kLabelCapacity = 0x1D8, kLabelGlyphs = 0x228;
constexpr int kMaxEntries = 16;
constexpr int32_t kPausePanels = 2, kRowSprites = 9, kKeysGlyphs = 4;
constexpr int kKeyIconFirst = 384, kDiskIcon = 829;
// Sprite fields.
constexpr ptrdiff_t kX = 0x40, kY = 0x48, kTint = 0x68, kTintSize = 0x20, kVisible = 0x120;
constexpr ptrdiff_t kWorld = 0x128, kLocal = 0x148;
constexpr ptrdiff_t kPalettePtr = 0x198, kPaletteNumber = 0x1A0, kStyle = 0x1D0;
constexpr ptrdiff_t kFrameBase = 0x1A8, kFrameShown = 0x1B0, kFrameCount = 0x1BC;
constexpr ptrdiff_t kFrameIndex = 0x1C4, kFrameSize = 0x38;
constexpr size_t kSpriteSize = 0x1D8;
constexpr ptrdiff_t kFontTemplate = 1 * 8;
// Where things go (read live on B1, 2026-10-24): the label's line, and the middle.
constexpr double kRowY = 208.0, kLabelX = 144.0, kIconX = 160.0, kIconY = 184.0;

template <typename T>
bool read(const BYTE* base, ptrdiff_t offset, T& out) {
    return base != nullptr && safe_read(base + offset, &out, sizeof(T));
}

template <typename T>
void put(BYTE* base, ptrdiff_t offset, T value) {
    std::memcpy(base + offset, &value, sizeof(value));
}

bool set_frame(BYTE* s, int frame) {
    int32_t frames = 0;
    BYTE* base = nullptr;
    if (!read(s, kFrameCount, frames) || frame < 0 || frame >= frames ||
        !read(s, kFrameBase, base) || base == nullptr) {
        return false;
    }
    put<int32_t>(s, kFrameIndex, frame);
    put(s, kFrameShown, base + frame * kFrameSize);
    return true;
}

void place(BYTE* s, double x, double y, const double (&local)[4]) {
    put(s, kX, x);
    put(s, kY, y);
    put(s, kWorld + 0, x + local[0]);
    put(s, kWorld + 8, y + local[1]);
    put(s, kWorld + 16, x + local[2]);
    put(s, kWorld + 24, y + local[3]);
}

// Drawn as `like` is: its palette, style and tint.
void look_like(BYTE* s, const BYTE* like) {
    std::memcpy(s + kPalettePtr, like + kPalettePtr, sizeof(void*));
    std::memcpy(s + kPaletteNumber, like + kPaletteNumber, sizeof(int16_t));
    std::memcpy(s + kStyle, like + kStyle, sizeof(int32_t));
    std::memcpy(s + kTint, like + kTint, kTintSize);
}

}  // namespace

std::string pause_row_text(int count, int required) {
    std::string text = std::to_string(count) + " OF " +
                       (required > 0 ? std::to_string(required) : std::string("?"));
    if (text.size() > static_cast<size_t>(kRowCells)) return "";
    size_t left = (kRowCells - text.size() + 1) / 2;
    std::string out(left, ' ');
    out += text;
    out.resize(kRowCells, ' ');
    return out;
}

bool PauseDiskRow::patch(const PauseAddresses& a, uint8_t from, uint8_t to) {
    BYTE now[2] = {};
    if (a.keys_branch == nullptr || !safe_read(a.keys_branch, now, sizeof(now)) ||
        now[1] != kSkip || now[0] != from) {
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(a.keys_branch, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
    *a.keys_branch = to;
    DWORD unused = 0;
    VirtualProtect(a.keys_branch, 1, old, &unused);
    FlushInstructionCache(GetCurrentProcess(), a.keys_branch, 2);
    return true;
}

void PauseDiskRow::watch(const PauseAddresses& a) {
    if (text_.empty()) {
        restore(a);
        return;
    }
    if (!patched_ && patch(a, kJne, kJmp)) patched_ = true;
    BYTE* top = nullptr;
    if (read(a.current_menu, 0, top) && top != nullptr) turn(a, top);
}

void PauseDiskRow::restore(const PauseAddresses& a) {
    if (!patched_) return;
    patch(a, kJmp, kJne);  // only where it still reads as written
    BYTE now = 0;
    // Still held while it reads as written (put back failed): the helper then refuses to unload.
    if (!safe_read(a.keys_branch, &now, 1) || now != kJmp) patched_ = false;
}

void PauseDiskRow::turn(const PauseAddresses& a, BYTE* menu) {
    // The pause screen: two panels, the first holding EQUIPMENT, the second the KEYS row.
    int32_t panels_n = 0, capacity = 0, sprites_n = 0, glyphs_n = 0, released = 1;
    BYTE** panels = nullptr;
    BYTE* entries = nullptr;
    BYTE* row = nullptr;
    if (!read(menu, kMenuPanelCount, panels_n) || panels_n != kPausePanels ||
        !read(menu, 0, panels) || !read(reinterpret_cast<BYTE*>(panels), 0, entries) ||
        !read(reinterpret_cast<BYTE*>(panels), 8, row)) {
        return;
    }
    BYTE** children = nullptr;
    int32_t entry_count = 0;
    if (!read(entries, kPanelCapacity, entry_count) || entry_count < 1 ||
        entry_count > kMaxEntries || !read(entries, kPanelChildren, children)) {
        return;
    }
    BYTE* list[kMaxEntries] = {};
    if (!safe_read(children, list, entry_count * sizeof(BYTE*))) return;
    bool equipment = false;
    for (int i = 0; i < entry_count; ++i) {
        void* cb = nullptr;
        if (read(list[i], kChildCallback, cb) &&
            (cb == a.equipment_fn || (a.equipment_hook != nullptr && cb == a.equipment_hook))) {
            equipment = true;
        }
    }
    BYTE** row_children = nullptr;
    BYTE* keys = nullptr;
    BYTE* label = nullptr;
    BYTE** glyph_list = nullptr;
    BYTE** sprite_list = nullptr;
    void* handler = nullptr;
    if (!equipment || !read(row, kPanelCapacity, capacity) || capacity != 1 ||
        !read(row, kPanelSpriteCount, sprites_n) || sprites_n != kRowSprites ||
        !read(row, kPanelChildren, row_children) ||
        !read(reinterpret_cast<BYTE*>(row_children), 0, keys) || !read(keys, kReleased, released) ||
        released != 0 || !read(keys, kChildHandler, handler) || handler != a.widget_handler ||
        !read(keys, kChildLabel, label) || !read(label, kLabelCapacity, glyphs_n) ||
        glyphs_n != kKeysGlyphs || !read(label, kLabelGlyphs, glyph_list) ||
        !read(row, kPanelSprites, sprite_list)) {
        return;
    }
    BYTE* glyph[kKeysGlyphs] = {};
    BYTE* sprite[kRowSprites] = {};
    if (!safe_read(glyph_list, glyph, sizeof(glyph)) ||
        !safe_read(sprite_list, sprite, sizeof(sprite))) {
        return;
    }
    for (BYTE* s : sprite) {
        if (!writable(s, kSpriteSize)) return;
    }
    for (BYTE* g : glyph) {
        if (!writable(g, kSpriteSize)) return;
    }
    int32_t icon = -1;
    read(sprite[4], kFrameIndex, icon);
    if (icon == kDiskIcon) return;  // turned already
    // Still the game's KEYS row: KEYS, and the first key's icon.
    const char keys_text[] = "KEYS";
    for (int i = 0; i < kKeysGlyphs; ++i) {
        int32_t c = -1;
        if (!read(glyph[i], kFrameIndex, c) || c != static_cast<unsigned char>(keys_text[i])) {
            return;
        }
    }
    if (icon != kKeyIconFirst) return;
    BYTE* templates = nullptr;
    BYTE* font = nullptr;
    if (!read(a.sprite_templates, 0, templates) || !read(templates, kFontTemplate, font) ||
        font == nullptr) {
        return;
    }
    // The label's glyphs: the middle four characters (they stay where the game put them).
    const std::string& t = text_;
    for (int i = 0; i < kKeysGlyphs; ++i) {
        set_frame(glyph[i], static_cast<unsigned char>(t[2 + i]));
        *(glyph[i] + kVisible) = 1;
    }
    // The key slots, given the font: two characters each side of the label.
    const double text_local[4] = {0, 0, 8, 8};
    const int cells[4] = {0, 1, 6, 7};
    for (int k = 0; k < 4; ++k) {
        BYTE* s = sprite[k];
        reinterpret_cast<void (*)(void*, void*)>(a.sprite_sheet_fn)(s, font);
        set_frame(s, static_cast<unsigned char>(t[cells[k]]));
        std::memcpy(s + kLocal, text_local, sizeof(text_local));
        double x = kLabelX + 8.0 * (cells[k] - 2);
        place(s, x, kRowY, text_local);
        look_like(s, glyph[0]);
        *(s + kVisible) = 1;
    }
    // The disk in the middle, white as the text; the other key icons and Zexion's hidden.
    double icon_local[4] = {};
    safe_read(sprite[4] + kLocal, icon_local, sizeof(icon_local));
    if (set_frame(sprite[4], kDiskIcon)) {
        place(sprite[4], kIconX, kIconY, icon_local);
        std::memcpy(sprite[4] + kTint, glyph[0] + kTint, kTintSize);
        *(sprite[4] + kVisible) = 1;
    } else {
        *(sprite[4] + kVisible) = 0;
    }
    for (int k = 5; k < kRowSprites; ++k) *(sprite[k] + kVisible) = 0;
}

}  // namespace zap
