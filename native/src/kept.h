// A global the helper never destroys: it is never unloaded, and at the game's exit the helper's
// threads may still be using it (a destructor run by exit() on the game thread would pull it from
// under them). Built in place by its own constructor; used through -> and *.
//
// As a namespace-scope global it is built when the library's initializers run, so on Linux, where
// the helper's constructor starts the serving thread from the same .init_array, it carries
// __attribute__((init_priority(101))) to be built ahead of that constructor (native/linux/src/
// helper.cpp, game_mem_linux.cpp), so that none is built afresh over what the thread holds, whether
// or not the compiler's library builds a global at load time. A shared source with no such
// attribute (offline.cpp builds on MSVC too) holds one in a function-local static instead: built on
// first use, thread-safely, by whichever thread asks first. A mutex or a once_flag needs neither: it
// is `constinit`, built at load time by its constant constructor (a compile error if it were not),
// and destroying it at exit is a no-op. tests/test_native_linux_init_order.py checks all three.
#pragma once

#include <new>
#include <type_traits>

namespace zap {

template <class T>
class Kept {
public:
    Kept() { ::new (static_cast<void*>(bytes_)) T(); }
    Kept(const Kept&) = delete;
    Kept& operator=(const Kept&) = delete;
    T* operator->() { return std::launder(reinterpret_cast<T*>(bytes_)); }
    T& operator*() { return *operator->(); }

private:
    alignas(T) unsigned char bytes_[sizeof(T)];
};

}  // namespace zap
