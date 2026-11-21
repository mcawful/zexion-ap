// The Linux build's side of a translation (native/src/translation.cpp): the label builder,
// generateText, given a std::wstring of the helper's own by reference (the game's callers build
// and destroy theirs: docs/re/linux.md, `hd_build_fn`), from rebuild_label on a stand-in label;
// and the font's glyph records as the Linux build lays them out (0x40 bytes, 64-bit fields at
// +0x10 and +0x20: "Mapped by disassembly: the helper's translated text"), read by FontMetrics.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../../tests/check.h"
#include "translation.h"

namespace {

std::wstring g_built;
float g_x = 0, g_y = 0;
int g_builds = 0;

// generateText's signature on Linux: x and y in xmm0 and xmm1, the string by reference in rdi.
void stand_in_builder(float x, float y, std::wstring* text, void*, BYTE*, bool) {
    g_built = *text;
    g_x = x;
    g_y = y;
    ++g_builds;
}

alignas(16) BYTE g_translation[zap::tr::kSize];
alignas(16) BYTE g_label[zap::hdlabel::kSize];

void test_a_label_is_built_from_the_helpers_own_string() {
    zap::GameAddresses a;
    a.translation = g_translation;
    a.tr_flag = g_translation + zap::tr::kOn;
    a.small_text = g_translation + zap::tr::kSmallText;
    a.small_offsets = g_translation + zap::tr::kSmallOffsets;
    a.item_names = g_translation + zap::tr::kItemNames;
    a.item_name_offsets = g_translation + zap::tr::kItemNameOffsets;
    a.hd_build_fn = reinterpret_cast<BYTE*>(&stand_in_builder);
    g_translation[zap::tr::kOn] = 1;
    g_label[zap::hdlabel::kBuilt] = 1;
    const int32_t capacity = 300;
    std::memcpy(g_label + zap::hdlabel::kCapacity, &capacity, sizeof capacity);
    const double x = 100.5, y = 40.0;
    std::memcpy(g_label + zap::hdlabel::kX, &x, sizeof x);
    std::memcpy(g_label + zap::hdlabel::kY, &y, sizeof y);
    // Longer than libstdc++'s 3 characters held in place: on the heap, freed by the helper.
    const std::wstring text = L"Archipelago \xE03C";
    check(zap::rebuild_label(a, g_label, text, {}), "rebuilt");
    check(g_builds == 1, "built once (not centered, not attached)");
    check(g_built == text, "with the text, as a std::wstring");
    check(g_x == 100.5f && g_y == 40.0f, "where the last build was given");
    g_translation[zap::tr::kOn] = 0;
    check(!zap::rebuild_label(a, g_label, text, {}) && g_builds == 1, "nothing with the flag off");
}

void test_a_glyphs_advance_is_read_from_the_linux_record() {
    zap::GameAddresses a;
    a.translation = g_translation;
    a.tr_flag = g_translation + zap::tr::kOn;
    a.small_text = g_translation + zap::tr::kSmallText;
    a.small_offsets = g_translation + zap::tr::kSmallOffsets;
    a.item_names = g_translation + zap::tr::kItemNames;
    a.item_name_offsets = g_translation + zap::tr::kItemNameOffsets;
    BYTE* font = g_translation + zap::tr::kFont;
    // Two records: 'A' the second, 10 pixels wide at the HD font's scale (4.8214 a pixel, in
    // 64ths), from 2 to 12. The first is junk where a 0x20-byte layout would read 'A'.
    alignas(16) static BYTE records[2 * 0x40];
    std::memset(records, 0x7F, 0x40);
    const int64_t from = 617, to = 3703;  // 2 and 12 pixels: x 4.8214 x 64
    std::memcpy(records + 0x40 + 0x10, &from, 8);
    std::memcpy(records + 0x40 + 0x20, &to, 8);
    BYTE* list = records;
    std::memcpy(font + 0x08, &list, sizeof list);
    const int32_t index = 1;
    std::memcpy(font + 0x30 + 4 * static_cast<int>(L'A'), &index, 4);
    const float space = 5 * 4.8214f;
    const double factor = 1.0;
    std::memcpy(font + 0x40040, &space, 4);
    std::memcpy(font + 0x40060, &factor, 8);
    zap::FontMetrics metrics;
    check(metrics.load(a), "the font's metrics read");
    const double w = metrics.width(L"A");
    check(w > 9.99 && w < 10.01, "'A' 10 pixels wide, from its record's 64-bit fields");
    check(metrics.space() > 4.99 && metrics.space() < 5.01, "the space 5 pixels");
}

}  // namespace

int main() {
    test_a_glyphs_advance_is_read_from_the_linux_record();
    test_a_label_is_built_from_the_helpers_own_string();
    return report("translation");
}
