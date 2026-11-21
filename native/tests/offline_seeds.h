// The seed the offline tests share (offline_tests.cpp's file and folder tests, offline_look_tests.cpp's
// stand-in game): a table and start, and the text of a seed file made of them.
#pragma once

#include <cstring>
#include <string>

#include "check.h"
#include "offline.h"

inline uint8_t g_table[154];
inline const uint8_t g_start[6] = {6, 0, 191, 0, 0x49, 1};

inline std::string seed_text(const std::string& discs = "discs 3 12 434F4C4C454354",
                      const std::string& unfound = "unfound 1312,40") {
    return "zexion-ap offline 1\n"
           "table " + hex(g_table, sizeof(g_table)) + "\n"
           "start " + hex(g_start, sizeof(g_start)) + "\n"
           "intel 1\n"
           "assists lock\n" +
           discs + "\n" + unfound + "\n"
           "ap_labels\n"
           "label 23 14 4150204954454D0A464F5220414C494345\n";
}

// A seed of a table of `n` entries and a start, as offline_seed_of reads a section.
inline zap::OfflineSeed seed_of(const uint8_t* table, const uint8_t* start, int n = 154) {
    zap::OfflineSeed s;
    s.locations = n;
    std::memcpy(s.table, table, static_cast<size_t>(n));
    std::memcpy(s.start, start, sizeof(s.start));
    return s;
}

// This seed's table and start written into a randomizer section (the loaded save's).
inline void put_seed(uint8_t* section) {
    std::memcpy(section + 0x26C, g_table, sizeof(g_table));
    std::memcpy(section + 0x310, g_start, sizeof(g_start));
}
