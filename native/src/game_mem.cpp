#include "game_mem.h"

#include <algorithm>

namespace zap {

namespace {

bool readable(const BYTE* p, size_t size) {
    const BYTE* end = p + size;
    while (p < end) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(p, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT) {
            return false;
        }
        DWORD protect = info.Protect;
        if ((protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
            (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
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
    return from != nullptr && readable(static_cast<const BYTE*>(from), size) &&
           copy_or_fault(from, to, size) == 1;
}

bool writable(const void* at, size_t size) {
    const BYTE* p = static_cast<const BYTE*>(at);
    const BYTE* end = p + size;
    while (p < end) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(p, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
            (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) == 0) {
            return false;
        }
        p = static_cast<const BYTE*>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

bool live_widget(const BYTE* w, const void* handler) {
    int32_t released = 1;
    void* h = nullptr;
    return read(w, sprite::kReleased, released) && released == 0 &&
           read(w, widget::kHandler, h) && h == handler;
}

bool live_widget(const BYTE* w, const void* handler, const void* callback) {
    void* cb = nullptr;
    return live_widget(w, handler) && read(w, widget::kCallback, cb) && cb == callback;
}

bool matches(const BYTE* r, const RecordShape& s) {
    return word(r, record::kKind) == s.kind && word(r, record::kFrame) == s.frame &&
           word(r, record::kTicks) == s.ticks && word(r, record::kFrames) == s.frames &&
           r[record::kIcon] == s.icon && r[record::kPalette] == s.palette &&
           word(r, record::kInventory) == s.inventory && word(r, record::kRedirect) == s.redirect &&
           word(r, record::kPickupClass) == s.pickup_class;
}

BYTE* writable_record(const BYTE* holder, int item, BYTE (&copy)[record::kSize]) {
    BYTE* records = pointer_at(holder);
    if (records == nullptr || item < 0 || item >= record::kCount) return nullptr;
    BYTE* r = records + item * record::kSize;
    if (!safe_read(r, copy, sizeof(copy)) || !writable(r, record::kSize)) return nullptr;
    return r;
}

void set_record_name(BYTE* r, std::string_view name) {
    char field[record::kNameSize] = {};
    std::memcpy(field, name.data(), (std::min)(name.size(), record::kNameSize - 1));
    if (std::memcmp(r + record::kName, field, sizeof(field)) != 0) {
        std::memcpy(r + record::kName, field, sizeof(field));
    }
}

bool is_open(const BYTE* current_menu, const BYTE* wanted) {
    BYTE* menu = nullptr;
    if (!read(current_menu, 0, menu)) return false;
    for (int i = 0; i < kMaxMenus && menu != nullptr; ++i) {
        if (menu == wanted) return true;
        if (!read(menu, menu::kBelow, menu)) return false;
    }
    return false;
}

int32_t frame_of(const BYTE* s) {
    int32_t f = -1;
    read(s, sprite::kFrameIndex, f);
    return f;
}

bool set_frame(BYTE* s, int32_t frame) {
    int32_t frames = 0;
    BYTE* list = nullptr;
    if (!read(s, sprite::kFrameCount, frames) || frame < 0 || frame >= frames ||
        !read(s, sprite::kFrameList, list) || list == nullptr) {
        return false;
    }
    put<int32_t>(s, sprite::kFrameIndex, frame);
    put(s, sprite::kFrameShown, list + frame * sprite::kFrameSize);
    return true;
}

void look_like(BYTE* s, const BYTE* like) {
    if (s == like) return;
    std::memcpy(s + sprite::kPalette, like + sprite::kPalette, sizeof(void*));
    std::memcpy(s + sprite::kPaletteNumber, like + sprite::kPaletteNumber, sizeof(int16_t));
    std::memcpy(s + sprite::kStyle, like + sprite::kStyle, sizeof(int32_t));
    std::memcpy(s + sprite::kTint, like + sprite::kTint, sprite::kTintSize);
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
