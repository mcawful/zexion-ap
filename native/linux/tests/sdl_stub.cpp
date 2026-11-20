// A stand-in libSDL for the stand-in game: SDL2's SDL_GL_SwapWindow, counting its calls.
#include <atomic>

std::atomic<unsigned long> g_real_swaps{0};

extern "C" void SDL_GL_SwapWindow(void*) { g_real_swaps.fetch_add(1); }

extern "C" unsigned long zap_stub_swaps() { return g_real_swaps.load(); }

// And libsteam_api's SteamInternal_ContextInit, as the library has it: the context's interface
// pointer, at +0x10, made by the context's init function at +0x00 on the first call.
extern "C" void* SteamInternal_ContextInit(void* context) {
    auto** c = static_cast<void**>(context);
    if (c[2] == nullptr) reinterpret_cast<void (*)(void**)>(c[0])(&c[2]);
    return &c[2];
}
