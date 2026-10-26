#include "game_mem.h"

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
