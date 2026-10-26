#include "pause.h"

#include <cstring>

#include "game_mem.h"

namespace zap {

// The disc, as the owner approved it (2026-10-26).
const char kDiscRows[kDiscSize][kDiscSize + 1] = {
    "..........####..........",
    ".......##########.......",
    ".....#######.#..###.....",
    "....#########...#.##....",
    "...#########...#..###...",
    "..##########..#..#####..",
    "..##########.#..###..#..",
    ".########......###..###.",
    ".#######..####..#..#..#.",
    ".######..######...#...#.",
    "#######.###..###.#...###",
    "#######.##....##....#.##",
    "##.#....##....##.#######",
    "###...#.###..###.#######",
    ".#...#...######..######.",
    ".#..#..#..####..#######.",
    ".###..###......########.",
    "..#..###..#.##########..",
    "..#####..#..##########..",
    "...###..#...#########...",
    "....##.#...#########....",
    ".....###..#.#######.....",
    ".......##########.......",
    "..........####..........",
};

namespace {

constexpr uint8_t kJne = 0x75, kJmp = 0xEB, kSkip = 0x2E;
// The KEYS row's panel: its sprites and their count, at the offsets a widget uses for its label
// and record (docs/re/memory.md, "The pause screen's KEYS row"; B1).
constexpr ptrdiff_t kPanelSprites = 0x1F0, kPanelSpriteCount = 0x1F8;
constexpr int kMaxEntries = 16;
constexpr int32_t kPausePanels = 2, kRowSprites = 9, kKeysGlyphs = 4;
constexpr int kKeyIconFirst = 384, kDiskIcon = 829;
// A sprite's drawn rectangle (left, top, right, bottom) and its rectangle about its place.
constexpr ptrdiff_t kWorld = 0x128, kLocal = 0x148;
// Where things go (read live on B1, 2026-10-24): the label's line, and the middle.
constexpr double kRowY = 208.0, kLabelX = 144.0, kIconX = 160.0, kIconY = 184.0;
// The helper's disc about its place, as the quad's corners (top left, top right, bottom right,
// bottom left) and as the rectangle: 24 x 24, centered as the game's 16 x 16 icon is.
constexpr double kHalf = kDiscSize / 2.0;
constexpr double kDiscCorners[8] = {-kHalf, -kHalf, kHalf, -kHalf, kHalf, kHalf, -kHalf, kHalf};
constexpr double kDiscLocal[4] = {-kHalf, -kHalf, kHalf, kHalf};
// A frame-list entry starts with its texture rectangle (u0, v0, u1, v1), over the texture's size
// (docs/re/memory.md, "The pause screen's KEYS row").
constexpr float kTextureSize = 2048.0f;
constexpr float kDiscRect[4] = {kDiscX / kTextureSize, kDiscY / kTextureSize,
                                (kDiscX + kDiscSize) / kTextureSize,
                                (kDiscY + kDiscSize) / kTextureSize};

void place(BYTE* s, double x, double y, const double (&local)[4]) {
    put(s, sprite::kX, x);
    put(s, sprite::kY, y);
    put(s, kWorld + 0, x + local[0]);
    put(s, kWorld + 8, y + local[1]);
    put(s, kWorld + 16, x + local[2]);
    put(s, kWorld + 24, y + local[3]);
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

bool PauseDiskRow::patch(const GameAddresses& a, uint8_t from, uint8_t to) {
    BYTE now[2] = {};
    if (a.keys_branch == nullptr || !safe_read(a.keys_branch, now, sizeof(now)) ||
        now[1] != kSkip || now[0] != from) {
        return false;
    }
    return patch_code(a.keys_branch, &to, 1);
}

void PauseDiskRow::watch(const GameAddresses& a, bool disc) {
    const std::string& text = text_.empty() ? prepared_ : text_;
    if (text.empty()) {
        restore(a);
        return;
    }
    entry_shows(disc);
    if (!patched_ && patch(a, kJne, kJmp)) patched_ = true;
    if (BYTE* top = pointer_at(a.current_menu)) turn(a, top, text, disc);
}

void PauseDiskRow::restore(const GameAddresses& a) {
    entry_shows(false);  // the image is cleared next: a sprite still on the entry shows 829's
    if (!patched_) return;
    patch(a, kJmp, kJne);  // only where it still reads as written
    BYTE now = 0;
    // Still held while it reads as written (put back failed): the helper then refuses to unload.
    if (!safe_read(a.keys_branch, &now, 1) || now != kJmp) patched_ = false;
}

void PauseDiskRow::entry_shows(bool disc) {
    if (entry_ == nullptr) return;
    std::memcpy(entry_, disc ? kDiscRect : game_rect_, sizeof(kDiscRect));
}

bool PauseDiskRow::show_disc(BYTE* s) {
    // Only a sprite that keeps its template: the game frees the frame list of one that does not
    // (0x745C0), and must never free the helper's entry.
    BYTE* list = nullptr;
    void* sprite_template = nullptr;
    int32_t frames = 0;
    if (!read(s, sprite::kTemplate, sprite_template) || sprite_template == nullptr ||
        !read(s, sprite::kFrameCount, frames) || frames <= kDiskIcon ||
        !read(s, sprite::kFrameList, list) || list == nullptr) {
        return false;
    }
    if (entry_ == nullptr) {
        // Made once, from frame 829's entry, and never freed (pause.h).
        BYTE copy[sprite::kFrameSize];
        if (!safe_read(list + kDiskIcon * sprite::kFrameSize, copy, sizeof(copy))) return false;
        auto* page = static_cast<BYTE*>(
            VirtualAlloc(nullptr, sprite::kFrameSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (page == nullptr) return false;
        std::memcpy(page, copy, sizeof(copy));
        std::memcpy(game_rect_, copy, sizeof(game_rect_));
        entry_ = page;
        entry_shows(true);  // watch's ran before the entry was made
    }
    put(s, sprite::kFrameList, entry_);
    put(s, sprite::kFrameShown, entry_);
    put<int32_t>(s, sprite::kFrameCount, 1);
    put<int32_t>(s, sprite::kFrameIndex, 0);
    std::memcpy(s + sprite::kCorners, kDiscCorners, sizeof(kDiscCorners));
    std::memcpy(s + kLocal, kDiscLocal, sizeof(kDiscLocal));
    return true;
}

void PauseDiskRow::turn(const GameAddresses& a, BYTE* menu, const std::string& text, bool disc) {
    // The text fills the row's cells (pause_row_text), read by index below.
    if (text.size() != static_cast<size_t>(kRowCells)) return;
    // The pause screen: two panels, the first holding EQUIPMENT, the second the KEYS row.
    int32_t panels_n = 0, capacity = 0, sprites_n = 0, glyphs_n = 0;
    BYTE* entries = panel_at(menu, 0);
    BYTE* row = panel_at(menu, 1);
    if (!read(menu, menu::kPanelCount, panels_n) || panels_n != kPausePanels) return;
    BYTE** children = nullptr;
    int32_t entry_count = 0;
    if (!read(entries, panel::kCapacity, entry_count) || entry_count < 1 ||
        entry_count > kMaxEntries || !read(entries, panel::kChildren, children)) {
        return;
    }
    BYTE* list[kMaxEntries] = {};
    if (!safe_read(children, list, entry_count * sizeof(BYTE*))) return;
    bool equipment = false;
    for (int i = 0; i < entry_count; ++i) {
        void* cb = nullptr;
        if (read(list[i], widget::kCallback, cb) &&
            (cb == a.equipment_fn || (a.hooks.equipment != nullptr && cb == a.hooks.equipment))) {
            equipment = true;
        }
    }
    BYTE* keys = child_at(row, 0);
    BYTE* label = nullptr;
    BYTE** glyph_list = nullptr;
    BYTE** sprite_list = nullptr;
    if (!equipment || !read(row, panel::kCapacity, capacity) || capacity != 1 ||
        !read(row, kPanelSpriteCount, sprites_n) || sprites_n != kRowSprites ||
        !live_widget(keys, a.widget_handler_fn) ||
        !read(keys, widget::kLabel, label) || !read(label, label::kLength, glyphs_n) ||
        glyphs_n != kKeysGlyphs || !read(label, label::kGlyphs, glyph_list) ||
        !read(row, kPanelSprites, sprite_list)) {
        return;
    }
    BYTE* glyph[kKeysGlyphs] = {};
    BYTE* sprite[kRowSprites] = {};
    if (!safe_read(glyph_list, glyph, sizeof(glyph)) ||
        !safe_read(sprite_list, sprite, sizeof(sprite))) {
        return;
    }
    // Turned already (each frame, as the screen stays up): nothing more to check.
    int32_t icon = -1;
    BYTE* shown = nullptr;
    read(sprite[4], sprite::kFrameIndex, icon);
    read(sprite[4], sprite::kFrameShown, shown);
    if (icon == kDiskIcon || (entry_ != nullptr && shown == entry_)) return;
    for (BYTE* s : sprite) {
        if (!writable(s, sprite::kSize)) return;
    }
    for (BYTE* g : glyph) {
        if (!writable(g, sprite::kSize)) return;
    }
    // Still the game's KEYS row: KEYS, and the first key's icon.
    const char keys_text[] = "KEYS";
    for (int i = 0; i < kKeysGlyphs; ++i) {
        int32_t c = -1;
        if (!read(glyph[i], sprite::kFrameIndex, c) ||
            c != static_cast<unsigned char>(keys_text[i])) {
            return;
        }
    }
    if (icon != kKeyIconFirst) return;
    BYTE* templates = nullptr;
    BYTE* font = nullptr;
    if (!read(a.sprite_templates, 0, templates) || !read(templates, sprite::kFontTemplate, font) ||
        font == nullptr) {
        return;
    }
    // The label's glyphs: the middle four characters (they stay where the game put them).
    for (int i = 0; i < kKeysGlyphs; ++i) {
        set_frame(glyph[i], static_cast<unsigned char>(text[2 + i]));
        *(glyph[i] + sprite::kVisible) = 1;
    }
    // The key slots, given the font: two characters each side of the label.
    const double text_local[4] = {0, 0, 8, 8};
    const int cells[4] = {0, 1, 6, 7};
    for (int k = 0; k < 4; ++k) {
        BYTE* s = sprite[k];
        reinterpret_cast<void (*)(void*, void*)>(a.sprite_sheet_fn)(s, font);
        set_frame(s, static_cast<unsigned char>(text[cells[k]]));
        std::memcpy(s + kLocal, text_local, sizeof(text_local));
        double x = kLabelX + 8.0 * (cells[k] - 2);
        place(s, x, kRowY, text_local);
        look_like(s, glyph[0]);
        *(s + sprite::kVisible) = 1;
    }
    // The disc in the middle, white as the text: the helper's if painted, else the game's small
    // Data Disc. The other key icons and Zexion's hidden.
    if ((disc && show_disc(sprite[4])) || set_frame(sprite[4], kDiskIcon)) {
        double icon_local[4] = {};  // its own size, or the helper's disc's (show_disc)
        safe_read(sprite[4] + kLocal, icon_local, sizeof(icon_local));
        place(sprite[4], kIconX, kIconY, icon_local);
        std::memcpy(sprite[4] + sprite::kTint, glyph[0] + sprite::kTint, sprite::kTintSize);
        *(sprite[4] + sprite::kVisible) = 1;
    } else {
        *(sprite[4] + sprite::kVisible) = 0;
    }
    for (int k = 5; k < kRowSprites; ++k) *(sprite[k] + sprite::kVisible) = 0;
}

}  // namespace zap
