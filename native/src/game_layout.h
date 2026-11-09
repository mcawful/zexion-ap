// The layouts of the game's objects that several modules read, as traced on B1; fields only one
// module reads stay in that module. One offset can mean different things on different objects
// (+0x1E0 a panel's children but a widget's index): the namespaces keep them apart.
#pragma once

#include <cstddef>
#include <cstdint>

namespace zap {

// The map: 54 x 54 cells, a cell's index y * 54 + x (docs/re/memory.md, "The live map grid
// flips at the moment of pickup").
constexpr int kMapSide = 54;
constexpr int kMapCells = kMapSide * kMapSide;
// A map cell in tiles, along this repo's x and y (docs/re/save-format.md, "The continue point and
// the seed's start").
constexpr int kCellColumns = 14, kCellRows = 20;
// Sprites.png, 2048 x 2048 RGBA (docs/re/memory.md, "The pause screen's KEYS row").
constexpr int kTextureSize = 2048;

// The loaded randomizer section (docs/re/save-format.md): location cells at +0 (x then y, 16-bit
// each, in list order), then the item table, the start record (the start candidate's index, then
// its tile column and row, 16-bit each) and setting t at kSettings + t. 155 locations with Secret
// Shuffle (t37) (docs/re/randocells.md, "The pool").
namespace rando {
constexpr ptrdiff_t kItemTable = 0x26C, kStart = 0x310, kSettings = 0x87B;
constexpr int kLocations = 154, kMaxLocations = 155, kSecretShuffle = 37, kStartSize = 6;
// The start-with array: 1 for each item the new game starts with, in the order of the menu's
// Starting Items (docs/re/randocells.md, "The randomizer menu's first screen: items and
// amounts"; client/newgame.py, START_WITH_ORDER).
constexpr ptrdiff_t kStartWith = 0x689;
constexpr int kStartWithOrder[] = {16, 19, 21, 32, 3,  4,  5,  6,  7,  11, 22, 20, 17, 9,
                                   14, 15, 10, 12, 34, 24, 26, 25, 13, 33, 8,  23, 18};
constexpr int kStartWithCount = sizeof(kStartWithOrder) / sizeof(kStartWithOrder[0]);
static_assert(kStartWithCount == 27 && kStartWith + kStartWithCount <= kSettings, "in this order");
// Whether the loaded section's new game started with `item`.
inline bool started_with(const uint8_t* section, int item) {
    for (int i = 0; i < kStartWithCount; ++i) {
        if (kStartWithOrder[i] == item) return section[kStartWith + i] != 0;
    }
    return false;
}
static_assert(kItemTable + kMaxLocations <= kStart && kStart + kStartSize <= kSettings,
              "in this order");
inline int locations(const uint8_t* section) {
    return kLocations + (section[kSettings + kSecretShuffle] != 0 ? 1 : 0);
}
}  // namespace rando

// save_loaded with no save loaded (docs/re/memory.md, "Telling play from menus, the title screen
// and death"); the flags' size (client/game.py, FLAGS).
constexpr uint8_t kNoSaveLoaded = 0xFF;
constexpr uint64_t kFlagCount = 0x9C4;

// Flags the helper reads, one byte each (docs/re/memory.md, "The scanner", "The ending: flag
// 250", "The escape skip"); looks.cpp has its own kMarkerFlag.
namespace flag {
constexpr int kScannerTold = 39;  // the scanner has said how many disks there are
constexpr int kEnding = 250;      // set only by the ending
// Set through the escape (and by the escape skip's YES, with 250), cleared as the ending begins:
// 250 set and 211 clear is the client's goal, the ending begun (client/session.py).
constexpr int kFinale = 211;
constexpr int kFeat = 251;  // set with each Feat the ending gives (feats.h)
}  // namespace flag

// An area event in the loaded table, [event_table] + event * kSize (docs/re/memory.md, "The
// escape skip" and "Received Energy Shields").
namespace event {
constexpr size_t kSize = 0x28;
constexpr ptrdiff_t kType = 0, kTypeHigh = 1;  // the type, and the byte after it (0)
constexpr ptrdiff_t kArg0 = 2, kArg1 = 4, kArg2 = 6, kArg3 = 8;  // 16-bit, by the type
constexpr ptrdiff_t kSelf = 0x24;  // the event's own number
}  // namespace event

// The loaded save's major-item list, [pickup_list]: up to 26 icon codes in pickup order, then
// their count, a signed byte (docs/re/memory.md, "The pickup record holds the major-item list").
namespace pickups {
constexpr int kMax = 26;
constexpr ptrdiff_t kCount = 0x1A;
constexpr size_t kSize = kCount + 1;
// Whether the pickup lists an item (record = item ID) with this icon (its record's +0x08), as at
// 0x1F761 to 0x1F7AC.
constexpr bool listed(int item, uint8_t icon) {
    if (icon == 0 || icon == 0x6E || icon == 0xE8 || item <= 2) return false;
    return item <= 26 || (item >= 32 && item <= 44);
}
// A count the game can have written (it appends only below kMax): another means the list is not
// the game's, and nothing is read or written by it.
constexpr bool count_ok(int count) { return count >= 0 && count <= kMax; }
}  // namespace pickups

// The save slots in memory, each block a save file from its offset 2, so file offset F is at
// F - 2 (docs/re/memory.md, "Save data lives in Zexion.exe's static data", "The file select").
namespace slot {
constexpr int kCount = 30;
constexpr ptrdiff_t kStride = 0x9408, kEmpty = 0x935E, kRandoMarker = 0x9400;
constexpr ptrdiff_t kMap = 0x877F - 2;
constexpr uint8_t kEmptyMark = 0xFF;
}  // namespace slot

// The open menus, the top one at [current_menu] (docs/re/memory.md, "The game-mode panel").
namespace menu {
constexpr ptrdiff_t kPanels = 0x00;
constexpr ptrdiff_t kPanelCount = 0x08;  // 32-bit
// Called with the menu when the one above it closes ("The pause screen's KEYS row").
constexpr ptrdiff_t kOnReturn = 0x68;
constexpr ptrdiff_t kBelow = 0x78;  // the next menu down
}  // namespace menu

// A panel (docs/re/memory.md, "The game-mode panel", "The assist menu", "Feats and
// achievements").
namespace panel {
constexpr ptrdiff_t kY = 0x1D8, kX = 0x1DA;  // 16-bit, pixels
constexpr ptrdiff_t kChildren = 0x1E0;
constexpr ptrdiff_t kCapacity = 0x1E8;    // 32-bit; the child count, read and written as such
constexpr ptrdiff_t kSelectable = 0x1EC;  // 32-bit: the first so many the cursor reaches
constexpr ptrdiff_t kHandler = 0x2F8;
}  // namespace panel

// A widget, a panel's child (docs/re/memory.md, "The game-mode panel").
namespace widget {
constexpr ptrdiff_t kIndex = 0x1E0;  // 32-bit: its place among the panel's children
constexpr ptrdiff_t kLabel = 0x1F0;
constexpr ptrdiff_t kCallback = 0x2C0;  // called by the generic widget handler
constexpr ptrdiff_t kHandler = 0x2C8;
}  // namespace widget

// A text label, a widget's or on its own (docs/re/memory.md, "The game-mode panel", "The title
// screen").
namespace label {
constexpr ptrdiff_t kLength = 0x1D8;   // 32-bit: the glyph count
constexpr ptrdiff_t kColumns = 0x1DC;  // 32-bit
constexpr ptrdiff_t kGlyphs = 0x228;
}  // namespace label

// The fields every drawn object shares: glyphs, widgets, item objects (docs/re/memory.md, "The
// game-mode panel", "Live item objects").
namespace sprite {
constexpr ptrdiff_t kTemplate = 0x30;  // the definition's template, null for none
constexpr ptrdiff_t kX = 0x40, kY = 0x48;  // doubles, pixels
constexpr ptrdiff_t kTint = 0x68;
constexpr size_t kTintSize = 0x20;
constexpr ptrdiff_t kCorners = 0x98;  // 8 doubles: the drawn quad's corners about the place
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

// Live objects past their sprite fields, of the item pool and of the general pool, which shows
// items in some rooms (docs/re/memory.md, "Live item objects").
namespace obj {
constexpr ptrdiff_t kSpawn = 0x1D8;   // 32-bit, 0 when free
constexpr ptrdiff_t kRecord = 0x1E8;  // 16-bit
constexpr size_t kItemSize = 0x3B0;
constexpr ptrdiff_t kItemRow = 0x3AC, kItemColumn = 0x3AD;  // its map cell
constexpr size_t kGeneralSize = 0x5C8;
constexpr ptrdiff_t kGeneralRow = 0x210, kGeneralColumn = 0x211;
// A general object's first script state, 16-bit (docs/re/memory.md, "Other players' items").
constexpr ptrdiff_t kFirstState = 0x32E;
}  // namespace obj

// Item records behind [item_records] (docs/re/memory.md, "Other players' items", "Game functions
// the helper calls").
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
