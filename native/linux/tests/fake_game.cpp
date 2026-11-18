// The stand-in game for tests/test_native_linux.py: it prints where its marker and block are,
// then calls SDL_GL_SwapWindow every 5 ms until killed, from two call sites (every frame from
// draw_frame, every fourth from draw_editor, as the game swaps a second window), and on SIGTERM the stand-in SDL's count of the calls that
// reached it.
#include <csignal>
#include <cstdio>
#include <ctime>

extern "C" void SDL_GL_SwapWindow(void*);
extern "C" unsigned long zap_stub_swaps();

volatile char g_marker[] = "zexion-ap test marker";
volatile unsigned char g_block[40000];  // byte i is i % 251, for reads of more than one piece
volatile std::sig_atomic_t g_stop = 0;

extern "C" __attribute__((noinline)) void draw_frame() {
    SDL_GL_SwapWindow(nullptr);
    asm volatile("");  // not a tail call: the call returns here
}

extern "C" __attribute__((noinline)) void draw_editor() {
    SDL_GL_SwapWindow(nullptr);
    asm volatile("");
}

int main() {
    std::signal(SIGTERM, [](int) { g_stop = 1; });
    for (unsigned i = 0; i < sizeof g_block; ++i) g_block[i] = static_cast<unsigned char>(i % 251);
    std::printf("marker %p block %p\n", static_cast<void*>(const_cast<char*>(g_marker)),
                static_cast<void*>(const_cast<unsigned char*>(g_block)));
    std::fflush(stdout);
    timespec pause{0, 5 * 1000 * 1000};
    for (unsigned long frame = 0; !g_stop; ++frame) {
        draw_frame();
        if (frame % 4 == 0) draw_editor();
        nanosleep(&pause, nullptr);
    }
    std::printf("swaps %lu\n", zap_stub_swaps());
    return 0;
}
