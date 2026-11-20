// Another library's SDL_GL_SwapWindow, as an overlay or a frame limiter has one, for the stand-in
// game: preloaded ahead of the helper, it is what the game's frame function calls, and it calls
// the next definition (the helper's) from its own code. Built with no frame pointer kept, as a
// wrapper in the wild may be, and with its own call not a tail call. With ZAP_WRAPPER_CALLS set to
// N it calls the next one N times, back to back, as a frame-duplicating layer would (a very large
// N is a tight loop of calls).
#include <dlfcn.h>

#include <cstdlib>

extern "C" __attribute__((visibility("default"))) void SDL_GL_SwapWindow(void* window) {
    using Swap = int (*)(void*);  // the helper's returns an int (SDL3's bool); SDL2's callers ignore it
    static Swap next = reinterpret_cast<Swap>(dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
    static const long calls = [] {
        const char* n = std::getenv("ZAP_WRAPPER_CALLS");
        return n != nullptr ? std::atol(n) : 1L;
    }();
    if (next == nullptr) return;
    for (long i = 0; i < calls; ++i) {
        next(window);
        asm volatile("");  // the call returns here, never a tail call
    }
}
