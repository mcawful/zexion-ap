# The Linux helper, libzexion_ap.so, preloaded into Zexion's Linux build (native/README.md,
# "The Linux helper"). Included by native/CMakeLists.txt on Linux.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  message(FATAL_ERROR "zexion_ap builds for x86-64 Linux only, like the game")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  message(FATAL_ERROR "zexion_ap on Linux needs GCC or Clang")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Everything at the build folder's top: native/build/libzexion_ap.so, as the tests look for it.
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options(-Wall -Wextra -Werror)

add_library(zap_linux_core STATIC src/core.cpp)
target_include_directories(zap_linux_core PUBLIC src)

# The Windows helper's sources the Linux helper shares (native/src): new games, their requests'
# parser, and the platform layer under them (game_mem_linux.cpp, mailbox_linux.cpp in place of
# the Windows parts). GCC warns where MSVC does not on partly initialized structs.
add_library(zap_shared STATIC
            ../src/newgame.cpp ../src/choice.cpp ../src/requests.cpp ../src/text.cpp ../src/scan.cpp
            ../src/trap.cpp ../src/game_mem.cpp ../src/game_mem_linux.cpp
            ../src/mailbox_linux.cpp)
target_include_directories(zap_shared PUBLIC ../src)
target_compile_options(zap_shared PRIVATE -Wno-missing-field-initializers)

# The C++ runtime linked in: the game runs in Steam's runtime container, whose libstdc++ may be
# older than the one it was built with. Only glibc and libdl are needed from the container.
# Only SDL_GL_SwapWindow is exported (exports.map): the runtime's own symbols, operator new and
# __cxa_* among them, would otherwise stand in for the game's libstdc++ in every process the
# preload reaches. tests/test_native_linux.py checks the build's exports.
function(zap_helper target)
  add_library(${target} SHARED src/helper.cpp)
  target_link_libraries(${target} PRIVATE zap_linux_core zap_shared dl pthread -static-libstdc++
                        -static-libgcc -Wl,--no-undefined -Wl,-z,defs -Wl,--exclude-libs,ALL
                        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map)
  set_target_properties(${target} PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/exports.map)
endfunction()

zap_helper(zexion_ap)  # libzexion_ap.so

# The helper serving any program, for tests/test_native_linux.py's stand-in game.
zap_helper(zexion_ap_hooktest)
target_compile_definitions(zexion_ap_hooktest PRIVATE ZAP_HOOKTEST)

# The stand-in game: it calls SDL_GL_SwapWindow from a stand-in libSDL (a shared library, so the
# preloaded helper comes first), and keeps a marker in its data for the memory read test.
add_library(zap_sdl_stub SHARED tests/sdl_stub.cpp)
set_target_properties(zap_sdl_stub PROPERTIES CXX_VISIBILITY_PRESET default)
add_executable(zap_fake_game tests/fake_game.cpp)
target_link_libraries(zap_fake_game PRIVATE zap_sdl_stub)
# A fixed-address executable (ET_EXEC), as the game is, so its sections are at their link
# addresses when it runs.
set_target_properties(zap_fake_game PROPERTIES POSITION_INDEPENDENT_CODE OFF)
target_compile_options(zap_fake_game PRIVATE -fno-pie)
target_link_options(zap_fake_game PRIVATE -no-pie)

enable_testing()
add_executable(zap_linux_core_tests tests/core_tests.cpp)
target_link_libraries(zap_linux_core_tests PRIVATE zap_linux_core)
add_test(NAME linux_core COMMAND zap_linux_core_tests)

add_executable(zap_newgame_tests tests/newgame_tests.cpp)
target_link_libraries(zap_newgame_tests PRIVATE zap_shared)
add_test(NAME newgame COMMAND zap_newgame_tests)
