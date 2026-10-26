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

}  // namespace zap
