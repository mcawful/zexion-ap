// libzexion_ap.so: the Linux helper, preloaded into Zexion's Linux build (native/README.md,
// "The Linux helper"). In any other program, or another build, it does nothing but pass
// SDL_GL_SwapWindow on.

#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core.h"
#include "game_mem.h"  // in_image_code, memory_map_new_frame (native/src)
#include "mailbox.h"
#include "newgame.h"
#include "requests.h"  // parse_newgame

namespace {

using namespace zap::linux_helper;

constexpr int kClientSilenceSeconds = 30;  // a client that sends nothing this long is dropped

using Swap = int (*)(void*);
Swap g_next_swap = nullptr;  // the SDL_GL_SwapWindow after ours, set by the constructor
std::atomic<bool> g_active{false};
std::atomic<uint64_t> g_frames{0};

// The frame calls by call site (return address), claimed lock-free on a site's first call; calls
// from sites past kCallSites are only counted together.
constexpr int kCallSites = 8;
std::atomic<uintptr_t> g_site[kCallSites]{};
std::atomic<uint64_t> g_site_calls[kCallSites]{};
std::atomic<uint64_t> g_other_calls{0};

void count_call_site(uintptr_t site) {
    for (int i = 0; i < kCallSites; ++i) {
        uintptr_t held = g_site[i].load(std::memory_order_acquire);
        if (held == 0) {
            uintptr_t empty = 0;
            if (g_site[i].compare_exchange_strong(empty, site, std::memory_order_acq_rel)) {
                held = site;
            } else {
                held = empty;  // another thread claimed it first: this site or another
            }
        }
        if (held == site) {
            g_site_calls[i].fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    g_other_calls.fetch_add(1, std::memory_order_relaxed);
}
const char* g_build = nullptr;
bool g_folder = false;

// The zero-filled part of the main program's writable segments (PT_LOAD with PF_W, from
// p_filesz to p_memsz: its .bss), as [start, end): the only memory a `write` may change. Not
// its code, nor its initialized data: .data's function pointers and .got.plt's import slots,
// which the game calls through, stay the game's.
constexpr int kMaxSegments = 4;
uintptr_t g_writable[kMaxSegments][2] = {};
int g_writable_count = 0;

// The game's thread's work (native/src/mailbox.h), and the new games it watches each frame once
// a client's `config` gave their addresses (native/src/newgame.h).
constexpr DWORD kFrameWaitMs = 1000;  // as the Windows helper's
zap::Mailbox g_mailbox;
zap::NewGames g_newgames;
zap::NewGameAddresses g_newgame_at;
bool g_configured = false;  // game thread only

// The main program's GNU build id, and its writable segments: dl_iterate_phdr lists it first.
int main_program_build_id(dl_phdr_info* info, size_t, void* data) {
    auto* out = static_cast<std::optional<std::string>*>(data);
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_LOAD && (ph.p_flags & PF_W) && g_writable_count < kMaxSegments) {
            uintptr_t start = info->dlpi_addr + ph.p_vaddr;
            if (ph.p_memsz > ph.p_filesz) {
                g_writable[g_writable_count][0] = start + ph.p_filesz;
                g_writable[g_writable_count][1] = start + ph.p_memsz;
                ++g_writable_count;
            }
        }
        if (ph.p_type != PT_NOTE || *out) continue;
        auto* notes = reinterpret_cast<const unsigned char*>(info->dlpi_addr + ph.p_vaddr);
        *out = note_build_id(notes, ph.p_memsz);
    }
    return 1;  // the first object only
}

std::string folder_of(const char* path) {
    char resolved[PATH_MAX];
    if (path == nullptr || realpath(path, resolved) == nullptr) return std::string();
    std::string text(resolved);
    size_t slash = text.rfind('/');
    return slash == std::string::npos ? std::string() : text.substr(0, slash);
}

std::string own_folder() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&main_program_build_id), &info) == 0) return std::string();
    return folder_of(info.dli_fname);
}

[[maybe_unused]] bool looks_like_zexion() {  // unused in the hooktest build
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return false;
    exe[n] = '\0';
    const char* name = std::strrchr(exe, '/');
    return std::strcmp(name ? name + 1 : exe, "Zexion") == 0;
}

// This process's memory, copied by the kernel: an address not mapped is EFAULT, not a fault.
bool read_own(uint64_t address, size_t size, std::string& out) {
    out.assign(size, '\0');
    iovec local{out.data(), size};
    iovec remote{reinterpret_cast<void*>(static_cast<uintptr_t>(address)), size};
    ssize_t n = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    if (n == static_cast<ssize_t>(size)) return true;
    out = n < 0 ? std::string(std::strerror(errno)) : "read " + std::to_string(n) + " of " +
                                                           std::to_string(size) + " bytes";
    for (char& c : out) {
        if (c == ' ') c = '_';  // one word, as the Windows helper's errors' fields
    }
    return false;
}

// Whether [address, address + size) lies in the game binary's .bss (g_writable).
bool in_game_data(uint64_t address, size_t size) {
    for (int i = 0; i < g_writable_count; ++i) {
        if (address >= g_writable[i][0] && address + size >= address &&
            address + size <= g_writable[i][1]) {
            return true;
        }
    }
    return false;
}

// Into this process's memory, by the kernel (a page not writable is EFAULT, not a fault), and
// only inside the main program's .bss (g_writable).
bool write_own(uint64_t address, const std::string& data, std::string& out) {
    if (!in_game_data(address, data.size())) {
        out = "not_in_the_game's_writable_data";
        return false;
    }
    iovec local{const_cast<char*>(data.data()), data.size()};
    iovec remote{reinterpret_cast<void*>(static_cast<uintptr_t>(address)), data.size()};
    ssize_t n = process_vm_writev(getpid(), &local, 1, &remote, 1, 0);
    if (n == static_cast<ssize_t>(data.size())) return true;
    out = n < 0 ? std::string(std::strerror(errno)) : "wrote " + std::to_string(n) + " of " +
                                                           std::to_string(data.size()) + " bytes";
    for (char& c : out) {
        if (c == ' ') c = '_';
    }
    return false;
}

State state() {
    State s;
    s.build = g_build;
    s.hooked = g_active.load();
    s.folder = g_folder;
    s.frames = g_frames.load();
    for (int i = 0; i < kCallSites; ++i) {
        uintptr_t site = g_site[i].load(std::memory_order_acquire);
        if (site != 0) s.callers.emplace_back(site, g_site_calls[i].load());
    }
    s.other_callers = g_other_calls.load();
    return s;
}

// `config name=ADDRESS ...`: the new games' addresses, the game's own by its symbols (the client
// resolves them: client/linux_targets.py). Each data one must lie in the game's .bss at its full
// size and each function in its code; all of them, or nothing changes.
struct ConfigKey {
    const char* name;
    uint8_t* zap::NewGameAddresses::*field;
    size_t size;  // 0: a function
};
constexpr ConfigKey kConfigKeys[] = {
    {"rando_blob", &zap::NewGameAddresses::rando_blob, zap::kSectionSize},
    {"randomizer_menu", &zap::NewGameAddresses::randomizer_menu, zap::kMenuSize},
    {"transition_fn", &zap::NewGameAddresses::transition_fn, sizeof(void*)},
    {"transition_timer", &zap::NewGameAddresses::transition_timer, sizeof(int32_t)},
    {"newgame_fn", &zap::NewGameAddresses::newgame_fn, 0},
    {"placement_fn", &zap::NewGameAddresses::placement_fn, 0},
    {"rando_marker", &zap::NewGameAddresses::rando_marker, sizeof(uint16_t)},
    {"current_slot", &zap::NewGameAddresses::current_slot, sizeof(int32_t)},
    {"start_count", &zap::NewGameAddresses::start_count, sizeof(int16_t)},
    {"start_cells", &zap::NewGameAddresses::start_cells, 4 * zap::kStartTableMax},
    {"start_flags", &zap::NewGameAddresses::start_flags, zap::kStartTableMax},
};

std::string parse_config(std::string_view request, zap::NewGameAddresses& out) {
    std::map<std::string, uint64_t> given;
    std::istringstream words{std::string(request.substr(std::string_view("config").size()))};
    std::string word;
    while (words >> word) {
        size_t eq = word.find('=');
        if (eq == std::string::npos) return "config takes name=address pairs";
        char* end = nullptr;
        uint64_t address = std::strtoull(word.c_str() + eq + 1, &end, 0);
        if (end == nullptr || *end != '\0' || address == 0) return "a bad address for " + word;
        given[word.substr(0, eq)] = address;  // a name the helper doesn't use: ignored
    }
    for (const ConfigKey& key : kConfigKeys) {
        auto found = given.find(key.name);
        if (found == given.end()) return std::string("missing ") + key.name;
        auto* at = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(found->second));
        bool ok = key.size == 0 ? zap::in_image_code(at) : in_game_data(found->second, key.size);
        if (!ok) {
            return std::string(key.name) +
                   (key.size == 0 ? " is not in the game's code" : " is not in the game's data");
        }
        out.*key.field = at;
    }
    return "";
}

// `work` run on the game's thread at its next frame call; "refused not configured" first when
// `needs_config` and no config came; "refused no frame" if no frame took it in time (it then
// never runs); "error <why>" at once for a request not parsed. As the Windows helper's on_game.
std::string on_game(const std::function<std::string()>& work, bool needs_config,
                    const std::string& parse_error = "") {
    if (!parse_error.empty()) return "error " + parse_error;
    std::string result;
    bool ran = g_mailbox.call(
        [&] {
            if (needs_config && !g_configured) return std::string("refused not configured");
            return work();
        },
        kFrameWaitMs, result);
    return ran ? result : "refused no frame";
}

// The requests that act on the game (native/README.md, "The Linux helper"), or nothing for one
// core.cpp answers.
std::optional<std::string> game_request(std::string_view request) {
    if (request == "config" || request.starts_with("config ")) {
        zap::NewGameAddresses found;
        std::string error = parse_config(request, found);
        return on_game(
            [&] {
                g_newgame_at = found;
                g_configured = true;
                return std::string("ok");
            },
            false, error);
    }
    if (request.starts_with("ap_newgame\n") || request == "ap_newgame") {
        zap::NewGamePlan plan;
        std::string error = zap::parse_newgame(std::string(request), plan);
        return on_game(
            [&] {
                g_newgames.arm(std::move(plan));
                return std::string("armed");
            },
            true, error);
    }
    if (request == "disarm") {
        return on_game([] { g_newgames.disarm(); return std::string("disarmed"); }, false);
    }
    if (request == "newgame_status") {
        return on_game([] { return g_newgames.status(); }, false);
    }
    return std::nullopt;
}

// One client: a request a message, a reply a message, until it goes, sends too much, or says
// nothing for kClientSilenceSeconds.
void serve_client(int client) {
    timeval silence{kClientSilenceSeconds, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &silence, sizeof silence);
    std::vector<char> buffer(kMaxRequest);
    for (;;) {
        ssize_t n = recv(client, buffer.data(), buffer.size(), MSG_TRUNC);
        if (n <= 0 || static_cast<size_t>(n) > buffer.size()) return;
        std::string_view request(buffer.data(), static_cast<size_t>(n));
        std::optional<std::string> game = game_request(request);
        std::string answer = game ? *game : reply(request, state(), read_own, write_own);
        if (send(client, answer.data(), answer.size(), MSG_NOSIGNAL) < 0) return;
    }
}

void* serve(void* arg) {
    int listener = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    for (;;) {
        int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno != EINTR && errno != ECONNABORTED) sleep(1);
            continue;
        }
        ucred cred{};
        socklen_t size = sizeof cred;
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &size) == 0 &&
            cred.uid == geteuid()) {
            serve_client(client);
        }
        close(client);
    }
    return nullptr;
}

// The socket, bound and listening, or -1 (said why on stderr).
int listen_socket() {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::string name = socket_name(static_cast<long>(getpid()));
    std::memcpy(address.sun_path + 1, name.data(), name.size());  // abstract: a NUL first
    auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size());
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), length) != 0 || listen(fd, 1) != 0) {
        std::fprintf(stderr, "zexion-ap %s: socket %s not opened: %s\n", kVersion, name.c_str(),
                     std::strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

// The serving thread, with every signal blocked from its first instruction: the game's signals
// stay the game's threads'.
bool start_thread(int fd) {
    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_SETMASK, &all, &old);
    pthread_t thread;
    int failed = pthread_create(&thread, nullptr, serve,
                                reinterpret_cast<void*>(static_cast<intptr_t>(fd)));
    pthread_sigmask(SIG_SETMASK, &old, nullptr);
    if (failed != 0) return false;
    pthread_detach(thread);
    return true;
}

__attribute__((constructor)) void start() {
    // Every library the program needs is mapped before any constructor runs.
    g_next_swap = reinterpret_cast<Swap>(dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
    std::string folder = own_folder();
    if (!folder.empty() && access((folder + "/" + kDisabledName).c_str(), F_OK) == 0) return;
    std::optional<std::string> id;
    dl_iterate_phdr(main_program_build_id, &id);
#ifdef ZAP_HOOKTEST
    (void)id;
    g_build = "test";  // tests/test_native_linux.py's stand-in game: any program
#else
    g_build = id ? identify_build(*id) : nullptr;
    if (g_build == nullptr) {
        if (looks_like_zexion()) {
            std::fprintf(stderr, "zexion-ap %s: not a known build of Zexion (build id %s)\n",
                         kVersion, id ? id->c_str() : "none");
        }
        return;
    }
#endif
    if (g_next_swap == nullptr) {
        std::fprintf(stderr, "zexion-ap %s: no SDL_GL_SwapWindow after the helper's\n", kVersion);
    }
    g_folder = !folder.empty() && folder == folder_of("/proc/self/exe");
    int fd = listen_socket();
    if (fd < 0) return;
    if (!start_thread(fd)) {
        close(fd);
        return;
    }
    g_active = true;
    std::fprintf(stderr, "zexion-ap %s: serving build %s\n", kVersion, g_build);
}

}  // namespace

namespace {

// The game's thread, at each frame call: the mailbox's work, then an armed new game watched.
// What either throws is the helper's, never the game's: kept here.
void game_frame() {
    zap::memory_map_new_frame();
    g_mailbox.serve();
    if (!g_configured || !g_newgames.armed()) return;
    try {
        g_newgames.watch(g_newgame_at);
    } catch (...) {
        g_newgames.refuse("the placement failed");
    }
}

}  // namespace

// The game's per-frame call (native/README.md, "The Linux helper"). SDL2's returns nothing and
// SDL3's a bool: returned as an int, which an SDL2 caller ignores.
extern "C" __attribute__((visibility("default"))) int SDL_GL_SwapWindow(void* window) {
    if (g_active.load(std::memory_order_relaxed)) {
        g_frames.fetch_add(1, std::memory_order_relaxed);
        count_call_site(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
        game_frame();
    }
    Swap next = g_next_swap;
    if (next == nullptr) {  // SDL loaded after the constructor ran: look once more
        next = reinterpret_cast<Swap>(dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
        if (next == nullptr) return 0;
    }
    return next(window);
}
