# The native helper the AP client loads into the running game, or the proxy version.dll (also
# built here) loads as the game starts (see native/README.md).
cmake_minimum_required(VERSION 3.20)
project(zexion_ap LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "zexion_ap builds for 64-bit Windows only, like the game")
endif()
# The flags below are MSVC's, and the client, the release builder and the tests look for the
# DLLs under native/build/Release, where only a multi-config generator puts them.
if(NOT MSVC)
  message(FATAL_ERROR "zexion_ap needs Visual Studio's C++ compiler (MSVC)")
endif()
get_property(ZAP_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT ZAP_MULTI_CONFIG)
  message(FATAL_ERROR "zexion_ap needs a multi-config generator: Visual Studio, CMake's default "
                      "on Windows with Visual Studio installed")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The C runtime linked in statically, so the DLL needs nothing the game does not already load.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

add_library(zap_core STATIC
            src/iat.cpp src/build.cpp src/pipe.cpp src/mailbox.cpp src/game_mem.cpp
            src/game_calls.cpp src/game_offline.cpp src/requests.cpp src/text.cpp
            src/menu.cpp src/scan.cpp src/equip.cpp src/mission.cpp src/pause.cpp
            src/files.cpp src/intel.cpp src/offline.cpp src/title.cpp src/mapmarks.cpp
            src/apitem.cpp src/looks.cpp src/feats.cpp)
target_include_directories(zap_core PUBLIC src)
target_link_libraries(zap_core PUBLIC advapi32)

# The helper: hooks the game's SDL_GL_SwapWindow, in a known build only.
add_library(zexion_ap SHARED src/dllmain.cpp)
target_link_libraries(zexion_ap PRIVATE zap_core)

# The proxy the player copies into the game's folder so the helper loads with the game
# (native/README.md, "The proxy"): version.dll, built into a folder of its own so no program
# of ours picks it up in place of Windows' own.
add_library(version_proxy SHARED proxy/version.cpp proxy/version.def)
set_target_properties(version_proxy PROPERTIES OUTPUT_NAME version
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/proxy
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/proxy
                      ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/proxy)

# The helper built to hook the host's own GetTickCount (never a known game build), for
# unload_under_load and apply_in_frames.
add_library(zexion_ap_hooktest SHARED src/dllmain.cpp)
target_compile_definitions(zexion_ap_hooktest PRIVATE ZAP_HOOKTEST)
target_link_libraries(zexion_ap_hooktest PRIVATE zap_core)

enable_testing()
# Single-program tests on zap_core: NAME builds tests/NAME_tests.cpp as zap_NAME_tests.
set(ZAP_TESTS parts game requests menu apitem equip mission pause files intel title mapmarks
    offline)
foreach(name IN LISTS ZAP_TESTS)
  add_executable(zap_${name}_tests tests/${name}_tests.cpp)
  target_link_libraries(zap_${name}_tests PRIVATE zap_core)
  add_test(NAME ${name} COMMAND zap_${name}_tests)
endforeach()

# Feats and achievements (src/feats.h): the tests import SteamInternal_ContextInit by name from
# a stand-in steam_api64.dll, as the game does. Both are built into a folder of their own, so no
# program of ours picks the stand-in up in place of Steam's.
add_library(zap_steam_stub SHARED tests/steam_stub.cpp)
set_target_properties(zap_steam_stub PROPERTIES OUTPUT_NAME steam_api64)
add_executable(zap_feats_tests tests/feats_tests.cpp)
target_link_libraries(zap_feats_tests PRIVATE zap_core zap_steam_stub)
foreach(target IN ITEMS zap_steam_stub zap_feats_tests)
  set_target_properties(${target} PROPERTIES
                        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/feats
                        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/feats)
endforeach()
add_test(NAME feats COMMAND zap_feats_tests)

# The whole helper loaded and unloaded under load, grants applied in its frame calls, and a seed
# file put through the pipe.
foreach(name IN ITEMS unload apply offline_put)
  add_executable(zap_${name}_test tests/${name}_test.cpp)
  target_link_libraries(zap_${name}_test PRIVATE zap_core)
  add_dependencies(zap_${name}_test zexion_ap_hooktest)
endforeach()
add_test(NAME unload_under_load COMMAND zap_unload_test $<TARGET_FILE:zexion_ap_hooktest> 300)
add_test(NAME apply_in_frames COMMAND zap_apply_test $<TARGET_FILE:zexion_ap_hooktest>)
add_test(NAME offline_put_in_frames
         COMMAND zap_offline_put_test $<TARGET_FILE:zexion_ap_hooktest>)

add_executable(zap_proxy_test tests/proxy_test.cpp)
target_compile_definitions(zap_proxy_test PRIVATE
                           ZAP_PROXY_PATH="$<TARGET_FILE:version_proxy>")
add_dependencies(zap_proxy_test version_proxy)
add_test(NAME proxy COMMAND zap_proxy_test)
