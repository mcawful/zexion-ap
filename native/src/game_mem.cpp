#include "game_mem.h"

#include <algorithm>

namespace zap {

int32_t panel_count(const BYTE* menu) {
    int32_t n = -1;
    return read(menu, menu::kPanelCount, n) ? n : -1;
}

int32_t child_count(const BYTE* panel) {
    int32_t n = -1;
    return read(panel, panel::kCapacity, n) ? n : -1;
}

int32_t children_of(const BYTE* panel, BYTE** out, int32_t max) {
    const int32_t count = child_count(panel);
    const size_t n = static_cast<size_t>((std::min)(count, max));
    return count >= 0 && safe_read(pointer_at(panel, panel::kChildren), out, n * sizeof(BYTE*))
               ? count
               : -1;
}

bool label_glyphs(const BYTE* label, int32_t count, BYTE** glyphs) {
    int32_t n = 0;
    return count > 0 && read(label, label::kLength, n) && n == count &&
           safe_read(pointer_at(label, label::kGlyphs), glyphs,
                     static_cast<size_t>(count) * sizeof(BYTE*));
}

bool swap_pointer(BYTE* base, ptrdiff_t offset, const void* from, const void* to) {
    void* now = nullptr;
    if (from == nullptr || !read(base, offset, now) || now != from ||
        !writable(base + offset, sizeof(void*))) {
        return false;
    }
    put(base, offset, to);
    return true;
}

bool live_widget(const BYTE* w, const void* handler) {
    int32_t released = 1;
    void* h = nullptr;
    return read(w, sprite::kReleased, released) && released == 0 &&
           read(w, widget::kHandler, h) && h == handler;
}

bool live_widget(const BYTE* w, const void* handler, const void* callback) {
    void* cb = nullptr;
    return live_widget(w, handler) && read(w, widget::kCallback, cb) && cb == callback;
}

bool matches(const BYTE* r, const RecordShape& s) {
    return word(r, record::kKind) == s.kind && word(r, record::kFrame) == s.frame &&
           word(r, record::kTicks) == s.ticks && word(r, record::kFrames) == s.frames &&
           r[record::kIcon] == s.icon && r[record::kPalette] == s.palette &&
           word(r, record::kInventory) == s.inventory && word(r, record::kRedirect) == s.redirect &&
           word(r, record::kPickupClass) == s.pickup_class;
}

BYTE* writable_record(const BYTE* holder, int item, BYTE (&copy)[record::kSize]) {
    BYTE* records = pointer_at(holder);
    if (records == nullptr || item < 0 || item >= record::kCount) return nullptr;
    BYTE* r = records + item * record::kSize;
    if (!safe_read(r, copy, sizeof(copy)) || !writable(r, record::kSize)) return nullptr;
    return r;
}

void set_record_name(BYTE* r, std::string_view name) {
    char field[record::kNameSize] = {};
    std::memcpy(field, name.data(), (std::min)(name.size(), record::kNameSize - 1));
    if (std::memcmp(r + record::kName, field, sizeof(field)) != 0) {
        std::memcpy(r + record::kName, field, sizeof(field));
    }
}

bool is_open(const BYTE* current_menu, const BYTE* wanted) {
    BYTE* menu = nullptr;
    if (!read(current_menu, 0, menu)) return false;
    for (int i = 0; i < kMaxMenus && menu != nullptr; ++i) {
        if (menu == wanted) return true;
        if (!read(menu, menu::kBelow, menu)) return false;
    }
    return false;
}

int32_t frame_of(const BYTE* s) {
    int32_t f = -1;
    read(s, sprite::kFrameIndex, f);
    return f;
}

BYTE* frame_list(const BYTE* s, int32_t frame) {
    int32_t frames = 0;
    return read(s, sprite::kFrameCount, frames) && frame >= 0 && frame < frames
               ? pointer_at(s, sprite::kFrameList)
               : nullptr;
}

bool set_frame(BYTE* s, int32_t frame) {
    BYTE* list = frame_list(s, frame);
    if (list == nullptr) return false;
    put<int32_t>(s, sprite::kFrameIndex, frame);
    put(s, sprite::kFrameShown, list + frame * sprite::kFrameSize);
    return true;
}

bool glyphs_show(BYTE* const* glyphs, std::string_view text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (frame_of(glyphs[i]) != static_cast<unsigned char>(text[i])) return false;
    }
    return true;
}

void look_like(BYTE* s, const BYTE* like) {
    if (s == like) return;
    std::memcpy(s + sprite::kPalette, like + sprite::kPalette, sizeof(void*));
    std::memcpy(s + sprite::kPaletteNumber, like + sprite::kPaletteNumber, sizeof(int16_t));
    std::memcpy(s + sprite::kStyle, like + sprite::kStyle, sizeof(int32_t));
    std::memcpy(s + sprite::kTint, like + sprite::kTint, sprite::kTintSize);
}

bool code_reads(const BYTE* at, const void* bytes, size_t size) {
    BYTE now[kMaxCodePatch];
    return size <= sizeof(now) && safe_read(at, now, size) && std::memcmp(now, bytes, size) == 0;
}

bool put_back_code(BYTE* at, const void* written, const void* original, size_t size) {
    if (size > kMaxCodePatch) return false;  // cannot be checked: held, never let go unread
    if (code_reads(at, written, size)) patch_code(at, original, size);
    return !code_reads(at, written, size);
}

}  // namespace zap
