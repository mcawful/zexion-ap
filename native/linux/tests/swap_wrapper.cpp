// Another library's SDL_GL_SwapWindow, as an overlay or a frame limiter has one, for the stand-in
// game: preloaded ahead of the helper, it is what the game's frame function calls, and it calls
// the next definition (the helper's) from its own code. Built with no frame pointer kept, as a
// wrapper in the wild may be, and with its own call not a tail call.
#include <dlfcn.h>

extern "C" __attribute__((visibility("default"))) void SDL_GL_SwapWindow(void* window) {
    using Swap = int (*)(void*);  // the helper's returns an int (SDL3's bool); SDL2's callers ignore it
    static Swap next = reinterpret_cast<Swap>(dlsym(RTLD_NEXT, "SDL_GL_SwapWindow"));
    if (next != nullptr) next(window);
    asm volatile("");  // the call returns here
}
