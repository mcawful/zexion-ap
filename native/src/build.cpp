#include "build.h"

namespace zap {

namespace {

struct KnownBuild {
    const char* name;
    DWORD time_date_stamp;
    DWORD size_of_image;
};

// Keep in step with KNOWN_BUILDS in client/patterns.py.
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
    auto* base = reinterpret_cast<const BYTE*>(exe);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return identify_build(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
}

}  // namespace zap
