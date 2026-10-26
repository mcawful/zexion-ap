// The layouts of the game's objects that several modules read: menus, panels, widgets, text
// labels, the sprite fields every drawn object shares, live item objects and item records, as
// traced on B1. Fields only one module reads stay in that module.
//
// The same offset can mean different things on different objects, which the namespaces keep
// apart: +0x1E0 is a panel's children but a widget's index, +0x1F0 a widget's label but a
// panel's sprites (pause.cpp), and +0x1D8 a label's length but a live object's spawn.
#pragma once

#include <cstddef>
#include <cstdint>

namespace zap {

// The map: 54 x 54 cells, a cell's index y * 54 + x (docs/re/memory.md, "The live map grid
// flips at the moment of pickup").
constexpr int kMapSide = 54;
constexpr int kMapCells = kMapSide * kMapSide;

// The loaded game: `save_loaded` is 0xFF with no save loaded (loading one copies the slot's
// empty marker into it and then sets 0; docs/re/memory.md,
// "Telling play from menus, the title screen and death"), and its flags are 0x9C4 bytes
// (client/game.py, FLAGS).
constexpr uint8_t kNoSaveLoaded = 0xFF;
constexpr uint64_t kFlagCount = 0x9C4;

// The flags the helper reads, one byte each: 39, set once the scanner has said how many disks
// there are (docs/re/memory.md, "The scanner"); 250, set only by the ending ("The ending: flag
// 250").
namespace flag {
constexpr int kScannerTold = 39;
constexpr int kEnding = 250;
}  // namespace flag

// An area event in the loaded table, [event_table] + event * 0x28 (docs/re/memory.md, "The
// escape skip", "Received Energy Shields"): its type at +0 (the byte after it 0), 16-bit
// arguments from +2 whose meaning depends on the type, and the event's own number at +0x24.
namespace event {
constexpr size_t kSize = 0x28;
constexpr ptrdiff_t kType = 0, kTypeHigh = 1;
constexpr ptrdiff_t kArg0 = 2, kArg1 = 4, kArg2 = 6, kArg3 = 8;
constexpr ptrdiff_t kSelf = 0x24;
}  // namespace event

// The save slots in memory (docs/re/memory.md, "The file select"): 30 blocks of 0x9408 bytes in
// the slot array, each 0xFF at +0x935E when empty (the file's 0x9360, as each block starts at
// the file's offset 2), its randomizer marker at +0x9400 and its map grid at the file's 0x877F,
// and a copy of each slot's randomizer section (kSectionSize, requests.h) in the slots' table.
namespace slot {
constexpr int kCount = 30;
constexpr ptrdiff_t kStride = 0x9408, kEmpty = 0x935E, kRandoMarker = 0x9400;
constexpr ptrdiff_t kMap = 0x877F - 2;
constexpr uint8_t kEmptyMark = 0xFF;
}  // namespace slot

// The open menus: the top one at [current_menu], each one's next below at +0x78; a menu's panel
// list at +0 and their count at +8 (docs/re/memory.md, "The game-mode panel").
namespace menu {
constexpr ptrdiff_t kPanels = 0x00;
constexpr ptrdiff_t kPanelCount = 0x08;  // 32-bit
constexpr ptrdiff_t kBelow = 0x78;
}  // namespace menu

// A panel: its children (widgets), their count, the first so many the cursor reaches, and its
// handler (docs/re/memory.md, "The game-mode panel", "The assist menu").
namespace panel {
constexpr ptrdiff_t kChildren = 0x1E0;
constexpr ptrdiff_t kCapacity = 0x1E8;    // 32-bit
constexpr ptrdiff_t kSelectable = 0x1EC;  // 32-bit
constexpr ptrdiff_t kHandler = 0x2F8;
}  // namespace panel

// A widget, a panel's child: its place among the panel's children, its label, the callback the
// generic widget handler calls and its handler (docs/re/memory.md, "The game-mode panel").
namespace widget {
constexpr ptrdiff_t kIndex = 0x1E0;  // 32-bit
constexpr ptrdiff_t kLabel = 0x1F0;
constexpr ptrdiff_t kCallback = 0x2C0;
constexpr ptrdiff_t kHandler = 0x2C8;
}  // namespace widget

// A text label (a widget's, or a text object on its own): its glyph count, its columns and its
// glyph list (docs/re/memory.md, "The game-mode panel", "The title screen").
namespace label {
constexpr ptrdiff_t kLength = 0x1D8;   // 32-bit
constexpr ptrdiff_t kColumns = 0x1DC;  // 32-bit
constexpr ptrdiff_t kGlyphs = 0x228;
}  // namespace label

// The fields every drawn object shares (glyphs, widgets, item objects): its place, color tint,
// whether shown, palette, frame list and the frame shown (docs/re/memory.md, "The game-mode
// panel", "Live item objects").
namespace sprite {
constexpr ptrdiff_t kX = 0x40, kY = 0x48;  // doubles, pixels
constexpr ptrdiff_t kTint = 0x68;
constexpr size_t kTintSize = 0x20;
constexpr ptrdiff_t kVisible = 0x120;  // byte
constexpr ptrdiff_t kPalette = 0x198, kPaletteNumber = 0x1A0;  // the object, its 16-bit number
constexpr ptrdiff_t kFrameList = 0x1A8, kFrameShown = 0x1B0;   // pointers
constexpr ptrdiff_t kFrameCount = 0x1BC, kFrameIndex = 0x1C4;  // 32-bit
constexpr ptrdiff_t kReleased = 0x1C8;  // 32-bit, nonzero once released (widgets too)
constexpr ptrdiff_t kStyle = 0x1D0;     // 32-bit
constexpr ptrdiff_t kFrameSize = 0x38;  // one entry of a frame list
constexpr size_t kSize = 0x1D8;         // a glyph, through its last field
// The sprite definitions' templates: definition n's at [sprite_templates] + 8 * n.
constexpr ptrdiff_t kFontTemplate = 1 * 8, kItemTemplate = 34 * 8;
}  // namespace sprite

// Live objects past their sprite fields: the item pool's (128 of 0x3B0 bytes), and the general
// pool's (1000 of 0x5C8) that show items in some rooms; each with its spawn (0 when free) and
// record, and its map cell, the row then the column (docs/re/memory.md, "Live item objects").
namespace obj {
constexpr ptrdiff_t kSpawn = 0x1D8;   // 32-bit
constexpr ptrdiff_t kRecord = 0x1E8;  // 16-bit
constexpr size_t kItemSize = 0x3B0;
constexpr ptrdiff_t kItemRow = 0x3AC, kItemColumn = 0x3AD;
constexpr size_t kGeneralSize = 0x5C8;
constexpr ptrdiff_t kGeneralRow = 0x210, kGeneralColumn = 0x211;
}  // namespace obj

// Item records: 51 of 100 bytes behind [item_records] (docs/re/memory.md, "Other players'
// items", "Game functions the helper calls").
namespace record {
constexpr ptrdiff_t kSize = 100;
constexpr int kCount = 51;
constexpr ptrdiff_t kKind = 0x00, kFrame = 0x02, kTicks = 0x04, kFrames = 0x06;  // 16-bit
constexpr ptrdiff_t kIcon = 0x08, kPalette = 0x09;                              // bytes
constexpr ptrdiff_t kName = 0x0A;
constexpr size_t kNameSize = 50;  // NUL-terminated
constexpr ptrdiff_t kInventory = 0x3C, kEvent = 0x40, kRedirect = 0x54, kPickupClass = 0x58;
constexpr ptrdiff_t kSound = 0x5A, kVolume = 0x5C;  // 16-bit, float
}  // namespace record

}  // namespace zap
