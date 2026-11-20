// game_mem.h's operating-system parts for the game's Linux build (the Windows ones are in
// game_mem.cpp): fault-safe reads through the kernel, writability from the process's memory
// map, the main program's code from its program headers, and code patches with mprotect.
#include <link.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <mutex>  // call_once, the map's lock
#include <vector>

#include "game_mem.h"

namespace zap {

HMODULE main_image() {
    static const char kMainImage = 0;  // any address: on Linux the handle only stands for it
    return &kMainImage;
}

uint64_t ticks_ms() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<uint64_t>(now.tv_sec) * 1000 + static_cast<uint64_t>(now.tv_nsec) / 1000000;
}

void* alloc_pages(size_t size) {
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}

namespace {

struct Region {
    uintptr_t start, end;
    int prot;      // PROT_ flags
    bool settled;  // the main program's own segments or [heap]: a protection that does not change
};

// The main program's segments (all its PT_LOADs, page-aligned), as dl_iterate_phdr lists it
// first, and its executable ones: found once.
struct Span {
    uintptr_t start = 0, end = 0;
};
// Read by the helper's serving thread, which its library constructor starts: built before that
// constructor runs (init_priority puts them ahead of its default priority), whether or not this
// compiler's library builds them at load time, so none is built afresh over what a thread holds.
__attribute__((init_priority(101))) std::vector<Span> g_loads;
__attribute__((init_priority(101))) std::vector<Span> g_code;
__attribute__((init_priority(101))) std::once_flag g_spans_once;

int main_program_spans(dl_phdr_info* info, size_t, void*) {
    const auto page = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD) continue;
        uintptr_t start = info->dlpi_addr + ph.p_vaddr;
        g_loads.push_back({start & ~(page - 1), (start + ph.p_memsz + page - 1) & ~(page - 1)});
        if (ph.p_flags & PF_X) g_code.push_back({start, start + ph.p_memsz});
    }
    return 1;
}

void find_spans() {
    std::call_once(g_spans_once, [] { dl_iterate_phdr(main_program_spans, nullptr); });
}

// Whether [start, end) of a mapping is the main program's own memory or its [heap]: a protection
// that does not change under a running game, so a cached answer for it does not go stale.
bool in_main_program(uintptr_t start, uintptr_t end) {
    find_spans();
    for (const Span& span : g_loads) {
        if (start >= span.start && end <= span.end) return true;
    }
    return false;
}

// The process's memory map, as /proc/self/maps says now (read on each query, as Windows'
// VirtualQuery asks each time: no snapshot to go stale).
std::vector<Region> read_map() {
    std::vector<Region> map;
    FILE* maps = std::fopen("/proc/self/maps", "re");
    if (maps == nullptr) return map;
    char line[512];
    while (std::fgets(line, sizeof line, maps) != nullptr) {
        unsigned long start = 0, end = 0;
        char perms[5] = {};
        int name = 0;
        if (std::sscanf(line, "%lx-%lx %4s %*s %*s %*s %n", &start, &end, perms, &name) < 3) {
            continue;
        }
        int prot = (perms[0] == 'r' ? PROT_READ : 0) | (perms[1] == 'w' ? PROT_WRITE : 0) |
                   (perms[2] == 'x' ? PROT_EXEC : 0);
        const char* path = name > 0 ? line + name : "";
        map.push_back({start, end, prot,
                       std::strncmp(path, "[heap]", 6) == 0 || in_main_program(start, end)});
    }
    std::fclose(maps);
    return map;
}

// The last map read, kept a moment: writable() runs at every frame call (the item records, while
// labels are held), and a read parses every mapping of the process. Reused while younger than
// kMapCacheMs, for a range whose pages are all still mapped (mincore fails for an unmapped one)
// and all in the main program's own segments or [heap], whose protection does not change under
// the game; any other range (a large allocation, a stack, a library) is read afresh each time, as
// Windows' VirtualQuery asks each time: another mapping may have taken its addresses with another
// protection, and the write that follows writable() is a plain store. The helper's own mprotect
// drops it.
constexpr uint64_t kMapCacheMs = 250;
__attribute__((init_priority(101))) std::mutex g_map_mutex;  // built first, as g_loads above
__attribute__((init_priority(101))) std::vector<Region> g_map;
uint64_t g_map_at = 0;
bool g_map_kept = false;

// Whether every byte of [p, end) lies in a region of g_map that is settled.
bool settled_range(uintptr_t p, uintptr_t end) {
    uintptr_t q = p;
    for (const Region& r : g_map) {  // in address order
        if (q >= r.start && q < r.end) {
            if (!r.settled) return false;
            q = r.end;
            if (q >= end) return true;
        }
    }
    return false;
}

bool still_mapped(uintptr_t p, size_t size) {
    const auto page = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    uintptr_t first = p & ~(page - 1);
    size_t length = ((p + size - first) + page - 1) & ~(page - 1);
    std::vector<unsigned char> resident(length / page);
    return mincore(reinterpret_cast<void*>(first), length, resident.data()) == 0;
}

void forget_map() {
    std::lock_guard<std::mutex> lock(g_map_mutex);
    g_map_kept = false;
}

// Whether every byte of [at, at + size) is mapped with all of `prot`.
bool mapped_with(const void* at, size_t size, int prot) {
    auto p = reinterpret_cast<uintptr_t>(at);
    if (size == 0) size = 1;  // the byte at `at`
    if (p == 0 || p + size < p) return false;
    uintptr_t q = p, end = p + size;
    std::lock_guard<std::mutex> lock(g_map_mutex);
    const uint64_t now = ticks_ms();
    if (!g_map_kept || now - g_map_at >= kMapCacheMs || !settled_range(p, end) ||
        !still_mapped(p, size)) {
        g_map = read_map();
        g_map_at = now;
        g_map_kept = true;
    }
    for (const Region& r : g_map) {  // in address order
        if (q >= r.start && q < r.end) {
            if ((r.prot & prot) != prot) return false;
            q = r.end;
            if (q >= end) return true;
        }
    }
    return false;
}

// The protection of the page holding `at`, or -1 if it isn't mapped.
int protection_of(uintptr_t at) {
    for (const Region& r : read_map()) {
        if (at >= r.start && at < r.end) return r.prot;
    }
    return -1;
}

}  // namespace

bool safe_read(const void* from, void* to, size_t size) {
    if (from == nullptr) return false;
    if (size == 0) return true;
    iovec local{to, size};
    iovec remote{const_cast<void*>(from), size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(size);
}

bool writable(const void* at, size_t size) { return mapped_with(at, size, PROT_READ | PROT_WRITE); }

bool in_image_code(const void* p) {
    find_spans();
    auto address = reinterpret_cast<uintptr_t>(p);
    for (const Span& span : g_code) {
        if (address >= span.start && address < span.end) return true;
    }
    return false;
}

bool patch_code(BYTE* at, const void* bytes, size_t size) {
    if (at == nullptr || size == 0 || !in_image_code(at) || !in_image_code(at + size - 1)) {
        return false;
    }
    const auto page = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    uintptr_t first = reinterpret_cast<uintptr_t>(at) & ~(page - 1);
    uintptr_t last = (reinterpret_cast<uintptr_t>(at) + size - 1) & ~(page - 1);
    // Its pages' own protection, put back after (both pages the same: one code segment's).
    int original = protection_of(first);
    if (original < 0 || protection_of(last) != original) return false;
    size_t span = last - first + page;
    auto* start = reinterpret_cast<void*>(first);
    forget_map();  // its pages' protection changes: read afresh next time
    if (mprotect(start, span, original | PROT_WRITE) != 0) return false;
    std::memcpy(at, bytes, size);
    __builtin___clear_cache(reinterpret_cast<char*>(at), reinterpret_cast<char*>(at) + size);
    // Written: the caller holds the patch, to put back, whatever the pages' protection now (a
    // failed put-back leaves them writable as well: nothing else to do with it here).
    mprotect(start, span, original);
    forget_map();
    return true;
}

}  // namespace zap
