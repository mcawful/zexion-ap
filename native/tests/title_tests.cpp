// Host tests for the title screen's mod lines (title.h): a stand-in draw list holding the
// game's copyright line, and stand-ins for the game's text constructor, layout and glyph link
// that build and link objects with the fields the helper reads, as traced on B1.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "title.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

template <typename T>
T get(const BYTE* b, ptrdiff_t o) {
    T v;
    std::memcpy(&v, b + o, sizeof(v));
    return v;
}

template <typename T>
void set(BYTE* b, ptrdiff_t o, T v) {
    std::memcpy(b + o, &v, sizeof(v));
}

constexpr int kFrames = 256;
std::vector<std::unique_ptr<BYTE[]>> g_blocks;
BYTE g_frames[kFrames * 0x38];
BYTE g_list[0x30];
BYTE* g_layer = g_list;
int g_made = 0;
bool g_client = false;
BYTE g_palette[8], g_tag[8];

BYTE* block(size_t size) {
    g_blocks.push_back(std::make_unique<BYTE[]>(size));
    std::memset(g_blocks.back().get(), 0, size);
    return g_blocks.back().get();
}

void frame(BYTE* g, int32_t f) {
    set<int32_t>(g, 0x1C4, f);
    set<BYTE*>(g, 0x1B0, g_frames + f * 0x38);
}

BYTE* text_new(const char* chars, int length, int columns) {
    ++g_made;
    BYTE* t = block(0x268);
    set<int32_t>(t, 0x1D8, length);
    set<int32_t>(t, 0x1DC, columns);
    set<int32_t>(t, 0x1EC, length);
    auto** glyphs = reinterpret_cast<BYTE**>(block(sizeof(BYTE*) * length));
    for (int i = 0; i < length; ++i) {
        BYTE* g = block(0x1D8);
        set<BYTE*>(g, 0x1A8, g_frames);
        set<int32_t>(g, 0x1BC, kFrames);
        set<int32_t>(g, 0x1D0, 0x15);
        frame(g, static_cast<uint8_t>(chars[i]));
        glyphs[i] = g;
    }
    set<BYTE**>(t, 0x228, glyphs);
    return t;
}

void text_place(void* text, int x, int y) {
    auto* t = static_cast<BYTE*>(text);
    set<double>(t, 0x40, x);
    set<double>(t, 0x48, y);
}

// As 0xA8620: each glyph not in a list right after the text.
void text_link(void* text) {
    auto* t = static_cast<BYTE*>(text);
    auto** glyphs = get<BYTE**>(t, 0x228);
    BYTE* list = get<BYTE*>(t, 0);
    for (int i = 0; i < get<int32_t>(t, 0x1D8); ++i) {
        BYTE* g = glyphs[i];
        if (get<BYTE*>(g, 0) != nullptr) continue;
        BYTE* next = get<BYTE*>(t, 0x10);
        if (next != nullptr) set<BYTE*>(next, 0x18, g);
        set<BYTE*>(g, 0x10, next);
        set<BYTE*>(t, 0x10, g);
        set<BYTE*>(g, 0x18, t);
        if (next == nullptr) set<BYTE*>(list, 0x20, g);
        set<BYTE*>(g, 0, list);
    }
}

void append(BYTE* t) {
    BYTE* tail = get<BYTE*>(g_list, 0x20);
    if (tail != nullptr) {
        set<BYTE*>(tail, 0x10, t);
    } else {
        set<BYTE*>(g_list, 0x18, t);
    }
    set<BYTE*>(t, 0x18, tail);
    set<BYTE*>(t, 0x10, nullptr);
    set<BYTE*>(g_list, 0x20, t);
    set<BYTE*>(t, 0, g_list);
}

BYTE* glyph(BYTE* t, int i) { return get<BYTE**>(t, 0x228)[i]; }

// The title as the game's routine leaves it: the copyright line, after something else.
BYTE* reset_title() {
    std::memset(g_list, 0, sizeof(g_list));
    g_made = 0;
    BYTE* other = text_new("PUSH ANY KEY", 12, 32);
    append(other);
    text_link(other);
    BYTE* line = text_new("C2025 GALLANT LEAF", 18, 32);
    append(line);
    text_link(line);
    for (int i = 0; i < 18; ++i) {
        BYTE* g = glyph(line, i);
        g[0x120] = 1;
        set<BYTE*>(g, 0x198, g_palette);
        set<int16_t>(g, 0x1A0, 0x1A);
    }
    frame(glyph(line, 0), 0xA9);
    set<BYTE*>(line, 0x178, g_tag);
    set<int16_t>(line, 0x180, 0x79);
    g_made = 0;
    return line;
}

zap::TitleAddresses addresses() {
    zap::TitleAddresses a;
    a.draw_layer = reinterpret_cast<BYTE*>(&g_layer);
    a.text_new_fn = reinterpret_cast<void*>(&text_new);
    a.text_place_fn = reinterpret_cast<void*>(&text_place);
    a.text_link_fn = reinterpret_cast<void*>(&text_link);
    return a;
}

// Runs the watch past its frame count, so it looks once.
std::string look(zap::TitleLines& lines) {
    std::string why;
    for (int i = 0; i < 9; ++i) {
        std::string w = lines.watch(addresses(), g_client);
        if (!w.empty()) why = w;
    }
    return why;
}

// The objects in the list, in order.
std::vector<BYTE*> objects() {
    std::vector<BYTE*> out;
    for (BYTE* o = get<BYTE*>(g_list, 0x18); o != nullptr; o = get<BYTE*>(o, 0x10)) {
        out.push_back(o);
    }
    return out;
}

bool shows(BYTE* t, const std::string& text, bool copyright) {
    if (get<int32_t>(t, 0x1D8) != static_cast<int32_t>(text.size())) return false;
    for (size_t i = 0; i < text.size(); ++i) {
        int32_t want = copyright && i == 0 ? 0xA9 : static_cast<uint8_t>(text[i]);
        if (get<int32_t>(glyph(t, static_cast<int>(i)), 0x1C4) != want) return false;
    }
    return true;
}

BYTE* find(const std::string& text, bool copyright) {
    for (BYTE* o : objects()) {
        if (get<int32_t>(o, 0x1D8) > 0 && get<int32_t>(o, 0x1DC) >= get<int32_t>(o, 0x1D8) &&
            shows(o, text, copyright)) {
            return o;
        }
    }
    return nullptr;
}

bool all_visible(BYTE* t, bool visible) {
    for (int i = 0; i < get<int32_t>(t, 0x1D8); ++i) {
        if ((glyph(t, i)[0x120] != 0) != visible) return false;
    }
    return true;
}

void test_lines_are_made_under_the_copyright() {
    reset_title();
    zap::TitleLines lines;
    lines.set_version("0.22.0");
    check(look(lines).empty(), "no error");
    check(g_made == 3 && lines.shown(), "three lines made");
    BYTE* mod = find("ARCHIPELAGO MOD 0.22.0", false);
    BYTE* credit = find("C2026 MCAWFUL", true);
    check(mod != nullptr && credit != nullptr, "both in the list, the credit with the ©");
    if (mod == nullptr || credit == nullptr) return;
    check(get<double>(mod, 0x40) == 160 - 4 * 22 && get<double>(mod, 0x48) == 202,
          "the version line centered at y 202");
    check(get<double>(credit, 0x40) == 160 - 4 * 13 && get<double>(credit, 0x48) == 214,
          "the credit centered at y 214");
    BYTE* hint = find("START THE AP CLIENT TO PLAY A SEED", false);
    check(hint != nullptr && get<double>(hint, 0x40) == 160 - 4 * 34 &&
              get<double>(hint, 0x48) == 190 && all_visible(hint, true),
          "the hint centered at y 190, shown with no client");
    check(hint != nullptr && get<int32_t>(hint, 0x1DC) == 34, "the hint in one row of 34");
    check(get<int32_t>(mod, 0x1DC) == 32, "a shorter line in the game's 32 columns");
    check(all_visible(mod, true) && all_visible(credit, true), "glyphs shown");
    check(mod[0x120] == 0 && credit[0x120] == 0, "the text objects themselves not drawn");
    check(get<BYTE*>(glyph(mod, 3), 0x198) == g_palette &&
              get<int16_t>(glyph(mod, 3), 0x1A0) == 0x1A &&
              get<int32_t>(glyph(mod, 3), 0x1D0) == 0x15,
          "the game line's palette and style");
    check(get<BYTE*>(credit, 0x178) == g_tag && get<int16_t>(credit, 0x180) == 0x79,
          "the game line's tag");
    check(get<int32_t>(mod, 0x1E4) == -1, "as the routine sets +0x1E4");
    // Every object in the list owned by it, linked both ways.
    std::vector<BYTE*> all = objects();
    bool linked = get<BYTE*>(g_list, 0x20) == all.back();
    for (size_t i = 0; i < all.size(); ++i) {
        linked = linked && get<BYTE*>(all[i], 0) == g_list &&
                 get<BYTE*>(all[i], 0x18) == (i == 0 ? nullptr : all[i - 1]);
    }
    check(linked, "the list intact, the lines and their glyphs at its tail");
    look(lines);
    check(g_made == 3, "not made again");
}

void test_nothing_without_the_copyright_line() {
    std::memset(g_list, 0, sizeof(g_list));
    g_made = 0;
    BYTE* other = text_new("PUSH ANY KEY", 12, 32);
    append(other);
    g_made = 0;
    zap::TitleLines lines;
    lines.set_version("0.22.0");
    look(lines);
    check(g_made == 0 && !lines.shown(), "no copyright line: nothing made");
    zap::TitleLines unversioned;
    reset_title();
    look(unversioned);
    check(g_made == 0, "no version set: nothing made");
}

void test_hidden_and_shown_with_the_copyright() {
    BYTE* line = reset_title();
    zap::TitleLines lines;
    lines.set_version("0.22.0");
    look(lines);
    BYTE* mod = find("ARCHIPELAGO MOD 0.22.0", false);
    BYTE* credit = find("C2026 MCAWFUL", true);
    if (mod == nullptr || credit == nullptr) {
        check(false, "lines made");
        return;
    }
    lines.hide(addresses());
    check(all_visible(mod, false) && all_visible(credit, false) && !lines.shown(),
          "hidden (before an unload)");
    look(lines);
    check(all_visible(mod, true) && all_visible(credit, true), "shown again (a refused unload)");
    glyph(line, 0)[0x120] = 0;
    look(lines);
    check(all_visible(mod, false) && all_visible(credit, false), "hidden with the ©");
    check(g_made == 3, "never made twice");
}

void test_a_new_title_gets_new_lines() {
    reset_title();
    zap::TitleLines lines;
    lines.set_version("0.22.0");
    look(lines);
    reset_title();  // left the title (the game freed the list) and came back
    look(lines);
    check(g_made == 3 && find("ARCHIPELAGO MOD 0.22.0", false) != nullptr, "made for the new title");
}

}  // namespace

void test_the_hint_only_without_a_client() {
    reset_title();
    zap::TitleLines lines;
    lines.set_version("0.22.0");
    g_client = true;
    look(lines);
    BYTE* hint = find("START THE AP CLIENT TO PLAY A SEED", false);
    BYTE* mod = find("ARCHIPELAGO MOD 0.22.0", false);
    check(hint != nullptr && mod != nullptr && all_visible(hint, false) && all_visible(mod, true),
          "a client: the hint hidden, the version shown");
    g_client = false;
    look(lines);
    check(hint != nullptr && all_visible(hint, true), "the client gone: the hint shown");
}

int main() {
    test_the_hint_only_without_a_client();
    test_lines_are_made_under_the_copyright();
    test_nothing_without_the_copyright_line();
    test_hidden_and_shown_with_the_copyright();
    test_a_new_title_gets_new_lines();
    if (g_failures == 0) std::printf("title tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
