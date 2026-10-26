# The native helper the AP client loads into the running game, or the proxy version.dll (also
# built here) loads as the game starts (see native/README.md).
cmake_minimum_required(VERSION 3.20)
project(zexion_ap LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "zexion_ap builds for 64-bit Windows only, like the game")
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
            src/menu.cpp src/skip.cpp src/scan.cpp src/equip.cpp src/mission.cpp src/pause.cpp
            src/files.cpp src/intel.cpp src/offline.cpp src/title.cpp src/mapmarks.cpp
            src/apitem.cpp src/looks.cpp)
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
# Single-program tests on zap_core: NAME=SOURCE builds tests/SOURCE.cpp as zap_SOURCE.
set(ZAP_TESTS
    parts=tests game=game_tests requests=requests_tests menu=menu_tests apitem=apitem_tests
    equip=equip_tests mission=mission_tests pause=pause_tests files=files_tests
    intel=intel_tests title=title_tests mapmarks=mapmarks_tests offline=offline_tests)
foreach(entry IN LISTS ZAP_TESTS)
  string(REPLACE "=" ";" pair ${entry})
  list(GET pair 0 name)
  list(GET pair 1 source)
  add_executable(zap_${source} tests/${source}.cpp)
  target_link_libraries(zap_${source} PRIVATE zap_core)
  add_test(NAME ${name} COMMAND zap_${source})
endforeach()

# The whole helper loaded and unloaded under load, and grants applied in its frame calls.
add_executable(zap_unload_test tests/unload_test.cpp)
target_link_libraries(zap_unload_test PRIVATE zap_core)
add_dependencies(zap_unload_test zexion_ap_hooktest)
add_test(NAME unload_under_load COMMAND zap_unload_test $<TARGET_FILE:zexion_ap_hooktest> 300)

add_executable(zap_apply_test tests/apply_test.cpp)
target_link_libraries(zap_apply_test PRIVATE zap_core)
add_dependencies(zap_apply_test zexion_ap_hooktest)
add_test(NAME apply_in_frames COMMAND zap_apply_test $<TARGET_FILE:zexion_ap_hooktest>)

add_executable(zap_proxy_test tests/proxy_test.cpp)
target_compile_definitions(zap_proxy_test PRIVATE
                           ZAP_PROXY_PATH="$<TARGET_FILE:version_proxy>")
add_dependencies(zap_proxy_test version_proxy)
add_test(NAME proxy COMMAND zap_proxy_test)
