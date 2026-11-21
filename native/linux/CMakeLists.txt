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

# The Windows helper's sources the Linux helper shares (native/src): its Game and every part
# Game holds (only the ported ones run: native/README.md, "The Linux helper"), the requests'
# parser, and the platform layer under them (game_mem_linux.cpp, mailbox_linux.cpp in place of
# the Windows parts). GCC warns where MSVC does not on partly initialized structs.
# zap_shared is built twice from the same objects: zap_shared is the release's, zap_shared_test
# the test-only build, whose seed files' sources (offline.cpp, offline_fs_posix.cpp) are built with
# ZAP_TEST_BUILD set: each write primitive then refuses a folder that is not a test's own (inside
# the temporary folder, never a real one). Every test program and the test build of the helper link
# zap_shared_test; the release links zap_shared.
add_library(zap_shared_objs OBJECT
            ../src/game_calls.cpp ../src/game_offline.cpp ../src/newgame.cpp ../src/choice.cpp
            ../src/requests.cpp ../src/game_requests.cpp ../src/offline_inbox.cpp ../src/text.cpp ../src/menu.cpp ../src/scan.cpp ../src/equip.cpp
            ../src/mission.cpp ../src/pause.cpp ../src/files.cpp ../src/intel.cpp
            ../src/title.cpp ../src/mapmarks.cpp ../src/trap.cpp
            ../src/apitem.cpp ../src/looks.cpp ../src/feats.cpp ../src/translation.cpp
            ../src/game_mem.cpp ../src/game_mem_linux.cpp ../src/mailbox_linux.cpp)
target_include_directories(zap_shared_objs PUBLIC ../src)
target_compile_options(zap_shared_objs PRIVATE -Wno-missing-field-initializers)
foreach(core IN ITEMS zap_shared zap_shared_test)
  add_library(${core} STATIC $<TARGET_OBJECTS:zap_shared_objs>)
  target_include_directories(${core} PUBLIC ../src)
  target_compile_options(${core} PRIVATE -Wno-missing-field-initializers)
endforeach()
foreach(core IN ITEMS zap_shared zap_shared_test)
  target_sources(${core} PRIVATE ../src/offline.cpp ../src/offline_fs_posix.cpp)
endforeach()
target_compile_definitions(zap_shared_test PRIVATE ZAP_TEST_BUILD)
# dlsym and dlopen (apitem.cpp, feats.cpp), std::call_once and threads: on a glibc before 2.34,
# the Steam Runtime's 2.31 among them, those are libdl's and libpthread's, not libc's. -pthread
# (Threads, preferring the flag) keeps libpthread linked into a program that calls none of its
# functions itself: named as a plain library, the linker may drop it, and std::call_once then
# finds no threads and throws (system_error -1, seen in the SDK's build, 2026-11-20).
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(zap_shared PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)
target_link_libraries(zap_shared_test PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)

# The C++ runtime linked in: the game runs in Steam's runtime container, whose libstdc++ may be
# older than the one it was built with. Only glibc is needed from the container (libdl and
# libpthread, glibc's own libraries before 2.34, among it).
# Only SDL_GL_SwapWindow and SteamInternal_ContextInit, the calls it stands in for, are exported
# (exports.map): the runtime's own symbols, operator new and
# __cxa_* among them, would otherwise stand in for the game's libstdc++ in every process the
# preload reaches. tests/test_native_linux.py checks the build's exports.
function(zap_helper target core)
  add_library(${target} SHARED src/helper.cpp)
  target_compile_options(${target} PRIVATE -Wno-missing-field-initializers)  # Game's headers
  target_link_libraries(${target} PRIVATE zap_linux_core ${core} dl pthread -static-libstdc++
                        -static-libgcc -Wl,--no-undefined -Wl,-z,defs -Wl,--exclude-libs,ALL
                        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map)
  set_target_properties(${target} PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/exports.map)
endfunction()

zap_helper(zexion_ap zap_shared)  # libzexion_ap.so

# The helper serving any program, for tests/test_native_linux.py's stand-in game.
zap_helper(zexion_ap_hooktest zap_shared_test)
target_compile_definitions(zexion_ap_hooktest PRIVATE ZAP_HOOKTEST)

# The stand-in game: it calls SDL_GL_SwapWindow from a stand-in libSDL (a shared library, so the
# preloaded helper comes first), and keeps a marker in its data for the memory read test.
add_library(zap_sdl_stub SHARED tests/sdl_stub.cpp)
set_target_properties(zap_sdl_stub PROPERTIES CXX_VISIBILITY_PRESET default)
# Another library's SDL_GL_SwapWindow, to preload ahead of the helper (a wrapper that calls it from
# its own code, with no frame pointer kept: the unwinder finds its caller).
add_library(zap_swap_wrapper SHARED tests/swap_wrapper.cpp)
target_compile_options(zap_swap_wrapper PRIVATE -fomit-frame-pointer)
add_executable(zap_fake_game tests/fake_game.cpp)
target_link_libraries(zap_fake_game PRIVATE zap_sdl_stub)
# A fixed-address executable (ET_EXEC), as the game is, so its sections are at their link
# addresses when it runs.
set_target_properties(zap_fake_game PROPERTIES POSITION_INDEPENDENT_CODE OFF)
target_compile_options(zap_fake_game PRIVATE -fno-pie)
target_link_options(zap_fake_game PRIVATE -no-pie -rdynamic)  # its stand-in OpenGL found by dlsym

enable_testing()
add_executable(zap_linux_core_tests tests/core_tests.cpp)
target_link_libraries(zap_linux_core_tests PRIVATE zap_linux_core)
add_test(NAME linux_core COMMAND zap_linux_core_tests)

add_executable(zap_newgame_tests tests/newgame_tests.cpp)
target_link_libraries(zap_newgame_tests PRIVATE zap_shared_test)
add_test(NAME newgame COMMAND zap_newgame_tests)

# The Linux build's code patches, run side by side with the game's bytes they replace.
add_executable(zap_patch_tests tests/patch_tests.cpp)
target_link_libraries(zap_patch_tests PRIVATE zap_shared_test)
add_test(NAME patch COMMAND zap_patch_tests)

# The Steam export's answer where no libsteam_api is loaded (this program has none).
add_executable(zap_feats_tests tests/feats_tests.cpp)
target_link_libraries(zap_feats_tests PRIVATE zap_shared_test)
add_test(NAME feats COMMAND zap_feats_tests)

# The seed files' POSIX half: the atomic write, the listing, the save folder, the copies Steam
# Cloud carries with the saves.
add_executable(zap_linux_offline_tests tests/offline_tests.cpp)
target_link_libraries(zap_linux_offline_tests PRIVATE zap_shared_test)
target_compile_options(zap_linux_offline_tests PRIVATE -Wno-missing-field-initializers)
add_test(NAME linux_offline COMMAND zap_linux_offline_tests)

# The canary (tests/canary.cpp, native/tests): every ZexionAP-seed-* file in each real save folder
# and every file in each real per-user folder (absent folders recorded as absent), as they were
# before the first test, checked after the last. Every test requires the fixture, so the snapshot
# runs first and the check last.
add_executable(zap_canary ../tests/canary.cpp)
target_link_libraries(zap_canary PRIVATE zap_shared_test)
target_compile_options(zap_canary PRIVATE -Wno-missing-field-initializers)
add_test(NAME canary_before COMMAND zap_canary snapshot ${CMAKE_BINARY_DIR}/canary.snapshot)
add_test(NAME canary_after COMMAND zap_canary verify ${CMAKE_BINARY_DIR}/canary.snapshot)
set_tests_properties(canary_before PROPERTIES FIXTURES_SETUP real_folders)
set_tests_properties(canary_after PROPERTIES FIXTURES_CLEANUP real_folders)

# The seeds the serving thread hands the game's thread (the same test as the Windows helper's).
add_executable(zap_inbox_tests ../tests/inbox_tests.cpp)
target_link_libraries(zap_inbox_tests PRIVATE zap_shared_test)
target_compile_options(zap_inbox_tests PRIVATE -Wno-missing-field-initializers)
add_test(NAME inbox COMMAND zap_inbox_tests)

# The Linux build's calls in a translation: the label builder given the helper's own std::wstring.
add_executable(zap_translation_tests tests/translation_tests.cpp)
target_link_libraries(zap_translation_tests PRIVATE zap_shared_test)
target_compile_options(zap_translation_tests PRIVATE -Wno-missing-field-initializers)
add_test(NAME translation COMMAND zap_translation_tests)

# Every test but the canary's own requires the canary's fixture.
get_property(ZAP_ALL_TESTS DIRECTORY PROPERTY TESTS)
list(REMOVE_ITEM ZAP_ALL_TESTS canary_before canary_after)
set_tests_properties(${ZAP_ALL_TESTS} PROPERTIES FIXTURES_REQUIRED real_folders)
