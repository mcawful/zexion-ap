// The stand-in game for tests/test_native_linux.py: it prints where its marker and block are,
// then calls SDL_GL_SwapWindow every 5 ms until killed, and prints the stand-in SDL's count of
// the calls that reached it on SIGTERM.
#include <csignal>
#include <cstdio>
#include <ctime>

extern "C" void SDL_GL_SwapWindow(void*);
extern "C" unsigned long zap_stub_swaps();

volatile char g_marker[] = "zexion-ap test marker";
volatile unsigned char g_block[40000];  // byte i is i % 251, for reads of more than one piece
volatile std::sig_atomic_t g_stop = 0;

int main() {
    std::signal(SIGTERM, [](int) { g_stop = 1; });
    for (unsigned i = 0; i < sizeof g_block; ++i) g_block[i] = static_cast<unsigned char>(i % 251);
    std::printf("marker %p block %p\n", static_cast<void*>(const_cast<char*>(g_marker)),
                static_cast<void*>(const_cast<unsigned char*>(g_block)));
    std::fflush(stdout);
    timespec pause{0, 5 * 1000 * 1000};
    while (!g_stop) {
        SDL_GL_SwapWindow(nullptr);
        nanosleep(&pause, nullptr);
    }
    std::printf("swaps %lu\n", zap_stub_swaps());
    return 0;
}
