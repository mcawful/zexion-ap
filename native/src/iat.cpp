#include "iat.h"

#include <cstdint>
#include <cstring>

#include "game_mem.h"  // nt_headers

namespace zap {

void** find_import_slot(HMODULE module, const char* dll, const char* function) {
    auto* base = reinterpret_cast<BYTE*>(module);
    const IMAGE_NT_HEADERS64* nt = nt_headers(module);
    if (nt == nullptr || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0) return nullptr;
    for (auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
         desc->Name != 0; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), dll) != 0) continue;
        if (desc->OriginalFirstThunk == 0) continue;  // no names to match against
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
        for (size_t i = 0; names[i].u1.AddressOfData != 0; ++i) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal)) continue;
            auto* by_name =
                reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), function) == 0) {
                return reinterpret_cast<void**>(&slots[i].u1.Function);
            }
        }
    }
    return nullptr;
}

bool swap_slot(void** slot, void* expected, void* replacement) {
    DWORD old_protect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect)) return false;
    void* previous = InterlockedCompareExchangePointer(slot, replacement, expected);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), old_protect, &ignored);
    return previous == expected;
}

namespace {

// Two pages: code (made read-and-execute) and data (read-write), so no page is writable and
// executable at once. Data: the target pointer at +0, the in-flight count at +8.
constexpr size_t kPage = 0x1000;
constexpr int32_t kTarget = kPage + 0;
constexpr int32_t kCount = kPage + 8;
// In the code page, after the code: unwind information and the function table entry.
constexpr DWORD kUnwindInfo = 0x40;
constexpr DWORD kRuntimeFunction = 0x60;

}  // namespace

void put_rel32(BYTE* code, size_t at, const BYTE* next, const BYTE* to) {
    const auto rel = static_cast<int32_t>(to - next);
    std::memcpy(code + at, &rel, sizeof(rel));
}

bool make_trampoline(void* target, Trampoline& out) {
    auto* page = static_cast<BYTE*>(
        VirtualAlloc(nullptr, 2 * kPage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (page == nullptr) return false;
    // lock inc dword [rip+count]   ; F0 FF 05 rel32            offsets 0..6
    // sub rsp, 0x28                ; 48 83 EC 28               7..10 (shadow space, alignment)
    // call qword [rip+target]      ; FF 15 rel32               11..16
    // lock dec dword [rip+count]   ; F0 FF 0D rel32            17..23
    // add rsp, 0x28                ; 48 83 C4 28               24..27 (a standard epilog,
    // ret                          ; C3                        28      so unwinding sees it)
    // Decrementing before the epilog is safe: the code after it is the trampoline's own.
    const BYTE code[] = {0xF0, 0xFF, 0x05, 0, 0, 0, 0, 0x48, 0x83, 0xEC, 0x28, 0xFF, 0x15, 0, 0,
                         0,    0,    0xF0, 0xFF, 0x0D, 0, 0, 0, 0, 0x48, 0x83, 0xC4, 0x28, 0xC3};
    std::memcpy(page, code, sizeof(code));
    put_rel32(page, 3, page + 7, page + kCount);
    put_rel32(page, 13, page + 17, page + kTarget);
    put_rel32(page, 20, page + 24, page + kCount);
    // Unwind information, so exceptions and stack walks pass through the trampoline: a prolog
    // of 11 bytes whose one effect on the stack is allocating 0x28 bytes.
    const BYTE unwind[] = {0x01, 11, 0x01, 0x00,  // version 1, prolog 11 bytes, 1 code, no frame
                           11, 0x42, 0, 0};        // at 11: UWOP_ALLOC_SMALL, (0x28 / 8) - 1
    std::memcpy(page + kUnwindInfo, unwind, sizeof(unwind));
    auto* function = reinterpret_cast<RUNTIME_FUNCTION*>(page + kRuntimeFunction);
    function->BeginAddress = 0;
    function->EndAddress = sizeof(code);
    function->UnwindData = kUnwindInfo;
    *reinterpret_cast<void**>(page + kTarget) = target;
    *reinterpret_cast<LONG*>(page + kCount) = 0;
    DWORD old_protect = 0;
    if (!VirtualProtect(page, kPage, PAGE_EXECUTE_READ, &old_protect)) {
        VirtualFree(page, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), page, kPage);
    // Registered for good, like the pages themselves.
    RtlAddFunctionTable(function, 1, reinterpret_cast<DWORD64>(page));
    out.entry = page;
    out.target = reinterpret_cast<void* volatile*>(page + kTarget);
    out.in_flight = reinterpret_cast<volatile LONG*>(page + kCount);
    return true;
}

}  // namespace zap
