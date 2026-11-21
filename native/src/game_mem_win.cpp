// What game_mem.h takes from Windows: tick count, pages, fault-safe reads, the image's
// sections and code patches. The Linux build's own is game_mem_linux.cpp.

#include "game_mem.h"

#include <algorithm>
#include <cstring>

namespace zap {

uint64_t ticks_ms() { return GetTickCount64(); }

void* alloc_pages(size_t size) {
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

namespace {

// Whether `size` bytes at `at` are committed, with no guard page, and every page allows one of
// `access` (PAGE_ flags).
bool committed(const void* at, size_t size, DWORD access) {
    for (auto *p = static_cast<const BYTE*>(at), *end = p + size; p < end;) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(p, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 || (info.Protect & access) == 0) {
            return false;
        }
        p = static_cast<const BYTE*>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

int copy_or_fault(const void* from, void* to, size_t size) {
    __try {
        std::memcpy(to, from, size);
        return 1;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                 : EXCEPTION_CONTINUE_SEARCH) {
        return 0;
    }
}

}  // namespace

bool safe_read(const void* from, void* to, size_t size) {
    // The query keeps the read off guard pages (another thread's stack), which an exception
    // handler would disarm; the handler covers memory freed in between.
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                PAGE_EXECUTE_WRITECOPY;
    return from != nullptr && committed(from, size, kReadable) &&
           copy_or_fault(from, to, size) == 1;
}

bool writable(const void* at, size_t size) {
    return committed(at, size, PAGE_READWRITE | PAGE_EXECUTE_READWRITE);
}

const IMAGE_NT_HEADERS64* nt_headers(HMODULE module) {
    auto* base = reinterpret_cast<const BYTE*>(module);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

uint64_t section_end(const IMAGE_SECTION_HEADER* section) {
    return section->VirtualAddress + (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
}

const IMAGE_SECTION_HEADER* section_of(HMODULE module, uint64_t rva) {
    const IMAGE_NT_HEADERS64* nt = nt_headers(module);
    if (nt == nullptr) return nullptr;
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (rva >= section->VirtualAddress && rva < section_end(section)) return section;
    }
    return nullptr;
}

bool in_image_code(HMODULE image, const void* p) {
    auto address = reinterpret_cast<uintptr_t>(p), base = reinterpret_cast<uintptr_t>(image);
    if (p == nullptr || address < base) return false;
    const IMAGE_SECTION_HEADER* section = section_of(image, address - base);
    return section != nullptr && (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
}

bool patch_code(BYTE* at, const void* bytes, size_t size) {
    DWORD old = 0;
    if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(at, bytes, size);
    DWORD unused = 0;
    VirtualProtect(at, size, old, &unused);
    FlushInstructionCache(GetCurrentProcess(), at, size);
    return true;
}

}  // namespace zap
