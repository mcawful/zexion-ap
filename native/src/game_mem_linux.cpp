// game_mem.h's operating-system parts for the game's Linux build (the Windows ones are in
// game_mem.cpp): fault-safe reads through the kernel, writability from the process's memory
// map, the main program's code from its program headers, and code patches with mprotect.
#include <link.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "game_mem.h"

namespace zap {

namespace {

struct Region {
    uintptr_t start, end;
    int prot;  // PROT_ flags
};

// The process's memory map, as /proc/self/maps says: read again at most once a frame (the game
// frees nothing while the helper's frame work runs on its thread) and on a query it can't answer.
std::mutex g_map_lock;
std::vector<Region> g_map;
uint64_t g_map_frame = ~uint64_t{0};
uint64_t g_frame = 0;

void read_map() {
    g_map.clear();
    FILE* maps = std::fopen("/proc/self/maps", "re");
    if (maps == nullptr) return;
    char line[512];
    while (std::fgets(line, sizeof line, maps) != nullptr) {
        unsigned long start = 0, end = 0;
        char perms[5] = {};
        if (std::sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3) continue;
        int prot = (perms[0] == 'r' ? PROT_READ : 0) | (perms[1] == 'w' ? PROT_WRITE : 0) |
                   (perms[2] == 'x' ? PROT_EXEC : 0);
        g_map.push_back({start, end, prot});
    }
    std::fclose(maps);
}

// Whether every byte of [at, at + size) is mapped with all of `prot`.
bool mapped_with(const void* at, size_t size, int prot) {
    auto p = reinterpret_cast<uintptr_t>(at);
    if (size == 0) size = 1;  // the byte at `at`
    if (p == 0 || p + size < p) return false;
    std::lock_guard<std::mutex> lock(g_map_lock);
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1 || g_map_frame != g_frame) {
            read_map();
            g_map_frame = g_frame;
        }
        uintptr_t q = p, end = p + size;
        for (const Region& r : g_map) {
            if (q >= r.start && q < r.end) {
                if ((r.prot & prot) != prot) break;
                q = r.end;
                if (q >= end) return true;
            }
        }
    }
    return false;
}

// The main program's executable segments: dl_iterate_phdr lists it first.
struct CodeSpan {
    uintptr_t start = 0, end = 0;
};
std::vector<CodeSpan> g_code;
std::once_flag g_code_once;

int main_program_code(dl_phdr_info* info, size_t, void*) {
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_LOAD && (ph.p_flags & PF_X)) {
            uintptr_t start = info->dlpi_addr + ph.p_vaddr;
            g_code.push_back({start, start + ph.p_memsz});
        }
    }
    return 1;
}

}  // namespace

void memory_map_new_frame() {
    std::lock_guard<std::mutex> lock(g_map_lock);
    ++g_frame;
}

bool safe_read(const void* from, void* to, size_t size) {
    if (from == nullptr) return false;
    if (size == 0) return true;
    iovec local{to, size};
    iovec remote{const_cast<void*>(from), size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(size);
}

bool writable(const void* at, size_t size) { return mapped_with(at, size, PROT_READ | PROT_WRITE); }

bool in_image_code(const void* p) {
    std::call_once(g_code_once, [] { dl_iterate_phdr(main_program_code, nullptr); });
    auto address = reinterpret_cast<uintptr_t>(p);
    for (const CodeSpan& span : g_code) {
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
    size_t span = last - first + page;
    auto* start = reinterpret_cast<void*>(first);
    if (mprotect(start, span, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return false;
    std::memcpy(at, bytes, size);
    mprotect(start, span, PROT_READ | PROT_EXEC);  // the code segment's own protection
    __builtin___clear_cache(reinterpret_cast<char*>(at), reinterpret_cast<char*>(at) + size);
    memory_map_new_frame();  // its pages' protection changed and back: read the map again
    return true;
}

}  // namespace zap
