#include "build.h"

#include "game_mem.h"  // nt_headers

namespace zap {

namespace {

struct KnownBuild {
    const char* name;
    DWORD time_date_stamp;
    DWORD size_of_image;
};

// Keep in step with KNOWN_BUILDS in client/patterns.py; tests/test_cross_checks.py checks it.
constexpr KnownBuild kKnownBuilds[] = {
    {"B1", 0x6A3F82DE, 0x1204000},
};

}  // namespace

const char* identify_build(DWORD time_date_stamp, DWORD size_of_image) {
    for (const auto& build : kKnownBuilds) {
        if (build.time_date_stamp == time_date_stamp && build.size_of_image == size_of_image) {
            return build.name;
        }
    }
    return nullptr;
}

const char* identify_build(HMODULE exe) {
    const IMAGE_NT_HEADERS64* nt = nt_headers(exe);
    if (nt == nullptr) return nullptr;
    return identify_build(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
}

}  // namespace zap
