// Reading and writing the game's memory: fault-safe reads, the write check, the walks every
// module shares (the open menus, a menu's panels and widgets, a sprite's frame), and the image's
// headers and code. Layouts are in game_layout.h.
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string_view>

#include "game_layout.h"

namespace zap {

// Copies `size` bytes from `from` into `to`, or returns false if they are not committed,
// readable memory (or reading them faults).
bool safe_read(const void* from, void* to, size_t size);

// Whether `size` bytes at `at` are committed memory the game may write (no guard page).
bool writable(const void* at, size_t size);

// A T at base + offset, fault-safe; false for a null base or memory that cannot be read.
template <typename T>
bool read(const BYTE* base, ptrdiff_t offset, T& out) {
    return base != nullptr && safe_read(base + offset, &out, sizeof(T));
}

// The pointer held at base + offset, fault-safe; null if it cannot be read.
inline BYTE* pointer_at(const BYTE* base, ptrdiff_t offset = 0) {
    BYTE* p = nullptr;
    return read(base, offset, p) ? p : nullptr;
}

// Writes a T at base + offset, unaligned; the caller has checked the place.
template <typename T>
void put(BYTE* base, ptrdiff_t offset, T value) {
    std::memcpy(base + offset, &value, sizeof(value));
}

// A T at p + offset, unaligned and unchecked: for a copy, or memory known good.
template <typename T>
T field(const BYTE* p, ptrdiff_t offset) {
    T v;
    std::memcpy(&v, p + offset, sizeof(v));
    return v;
}
inline int16_t word(const BYTE* p, ptrdiff_t offset) { return field<int16_t>(p, offset); }

// A T in place at p + offset, unchecked: for memory the caller owns or has checked.
template <typename T>
T& at(BYTE* p, ptrdiff_t offset) {
    return *reinterpret_cast<T*>(p + offset);
}

// Panel `i` of a menu (its list at menu::kPanels) and child `i` of a panel (panel::kChildren),
// fault-safe; null if a pointer on the way cannot be read. The caller checks the counts.
inline BYTE* panel_at(const BYTE* menu, int i) {
    return pointer_at(pointer_at(menu, menu::kPanels), i * 8);
}
inline BYTE* child_at(const BYTE* panel, int i) {
    return pointer_at(pointer_at(panel, panel::kChildren), i * 8);
}
// A menu's panel count and a panel's child count (panel::kCapacity), or -1 if unreadable.
int32_t panel_count(const BYTE* menu);
int32_t child_count(const BYTE* panel);
// A panel's first children, up to `max`, copied into `out`: its child count, or -1 (nothing
// copied) if that or its list cannot be read. The caller checks the count.
int32_t children_of(const BYTE* panel, BYTE** out, int32_t max);
// The `count` glyphs of a text label (label::), into `glyphs`, if it has that many (count > 0).
bool label_glyphs(const BYTE* label, int32_t count, BYTE** glyphs);
// Whether `w` is a live widget (not released) with the widget handler `handler`, and with the
// callback `callback` if one is given (null included).
bool live_widget(const BYTE* w, const void* handler);
bool live_widget(const BYTE* w, const void* handler, const void* callback);

// Points the slot at base + offset (a handler or callback) at `to` if it holds `from` (not null)
// and can be written: whether it did.
bool swap_pointer(BYTE* base, ptrdiff_t offset, const void* from, const void* to);

// An item record's fields the helper checks before it writes one (record::), as the game loads
// them; matches compares them with a record's copy.
struct RecordShape {
    int16_t kind = 0, frame = 0, ticks = 0, frames = 0;
    uint8_t icon = 0, palette = 0;
    int16_t inventory = 0, redirect = 0, pickup_class = 0;
};
bool matches(const BYTE* r, const RecordShape& shape);
// Record `item` behind [records_holder], copied into `copy`, where it is: null if it cannot be
// read or the game could not write it there.
BYTE* writable_record(const BYTE* records_holder, int item, BYTE (&copy)[record::kSize]);
// A record's name (r a record or its copy), up to its NUL.
inline std::string_view record_name(const BYTE* r) {
    const char* name = reinterpret_cast<const char*>(r + record::kName);
    return {name, strnlen(name, record::kNameSize)};
}
// A record's name is `name` (r a record or its copy).
inline bool record_named(const BYTE* r, std::string_view name) { return record_name(r) == name; }
// Writes `name` as the record's name, cut to fit and NUL-padded, if it is not that already.
void set_record_name(BYTE* r, std::string_view name);

// Whether `wanted` is still on the list of open menus, from [current_menu] down (menu::kBelow).
// The walk stops after kMaxMenus, far more than the game ever stacks.
constexpr int kMaxMenus = 64;
bool is_open(const BYTE* current_menu, const BYTE* wanted);

// A sprite's frame (sprite::kFrameIndex), or -1 if it cannot be read.
int32_t frame_of(const BYTE* sprite);
// Shows frame `frame` of a sprite's frame list (its index and its frame pointer), if the list
// has that many; false, with nothing written, otherwise.
bool set_frame(BYTE* sprite, int32_t frame);
// Whether each of `glyphs` shows its character of `text` (its frame), one glyph a character.
bool glyphs_show(BYTE* const* glyphs, std::string_view text);
// Drawn as `like` is: its palette, style and tint copied (both checked writable by the caller).
void look_like(BYTE* sprite, const BYTE* like);

// The NT headers of a loaded image, or null if `module` does not start with a PE's headers.
const IMAGE_NT_HEADERS64* nt_headers(HMODULE module);
// The RVA just past a section, as loaded; the section of `module` holding `rva`, or null.
uint64_t section_end(const IMAGE_SECTION_HEADER* section);
const IMAGE_SECTION_HEADER* section_of(HMODULE module, uint64_t rva);
// Whether `p` lies in an executable section of the loaded image `image`.
bool in_image_code(HMODULE image, const void* p);
// Writes `size` bytes over code at `at`, its page made writable for the write and then put back
// as it was, and flushes them from the instruction cache: false, nothing written, if the page
// could not be made writable. Game thread, so the game is not running the code meanwhile.
bool patch_code(BYTE* at, const void* bytes, size_t size);

}  // namespace zap
