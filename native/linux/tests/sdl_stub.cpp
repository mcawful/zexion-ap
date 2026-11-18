// A stand-in libSDL for the stand-in game: SDL2's SDL_GL_SwapWindow, counting its calls.
#include <atomic>

std::atomic<unsigned long> g_real_swaps{0};

extern "C" void SDL_GL_SwapWindow(void*) { g_real_swaps.fetch_add(1); }

extern "C" unsigned long zap_stub_swaps() { return g_real_swaps.load(); }
