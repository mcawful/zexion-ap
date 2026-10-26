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

# The same helper hooking the loading program's GetTickCount, for the unload test.
add_library(zexion_ap_hooktest SHARED src/dllmain.cpp)
target_compile_definitions(zexion_ap_hooktest PRIVATE ZAP_HOOKTEST)
target_link_libraries(zexion_ap_hooktest PRIVATE zap_core)

enable_testing()
add_executable(zap_tests tests/tests.cpp)
target_link_libraries(zap_tests PRIVATE zap_core)
add_test(NAME parts COMMAND zap_tests)

add_executable(zap_game_tests tests/game_tests.cpp)
target_link_libraries(zap_game_tests PRIVATE zap_core)
add_test(NAME game COMMAND zap_game_tests)

add_executable(zap_unload_test tests/unload_test.cpp)
target_link_libraries(zap_unload_test PRIVATE zap_core)
add_dependencies(zap_unload_test zexion_ap_hooktest)
add_test(NAME unload_under_load COMMAND zap_unload_test $<TARGET_FILE:zexion_ap_hooktest> 300)

add_executable(zap_apply_test tests/apply_test.cpp)
target_link_libraries(zap_apply_test PRIVATE zap_core)
add_dependencies(zap_apply_test zexion_ap_hooktest)
add_test(NAME apply_in_frames COMMAND zap_apply_test $<TARGET_FILE:zexion_ap_hooktest>)

add_executable(zap_menu_tests tests/menu_tests.cpp)
target_link_libraries(zap_menu_tests PRIVATE zap_core)
add_test(NAME menu COMMAND zap_menu_tests)

add_executable(zap_apitem_tests tests/apitem_tests.cpp)
target_link_libraries(zap_apitem_tests PRIVATE zap_core)
add_test(NAME apitem COMMAND zap_apitem_tests)

add_executable(zap_equip_tests tests/equip_tests.cpp)
target_link_libraries(zap_equip_tests PRIVATE zap_core)
add_test(NAME equip COMMAND zap_equip_tests)

add_executable(zap_mission_tests tests/mission_tests.cpp)
target_link_libraries(zap_mission_tests PRIVATE zap_core)
add_test(NAME mission COMMAND zap_mission_tests)

add_executable(zap_pause_tests tests/pause_tests.cpp)
target_link_libraries(zap_pause_tests PRIVATE zap_core)
add_test(NAME pause COMMAND zap_pause_tests)

add_executable(zap_files_tests tests/files_tests.cpp)
target_link_libraries(zap_files_tests PRIVATE zap_core)
add_test(NAME files COMMAND zap_files_tests)

add_executable(zap_intel_tests tests/intel_tests.cpp)
target_link_libraries(zap_intel_tests PRIVATE zap_core)
add_test(NAME intel COMMAND zap_intel_tests)

add_executable(zap_title_tests tests/title_tests.cpp)
target_link_libraries(zap_title_tests PRIVATE zap_core)
add_test(NAME title COMMAND zap_title_tests)

add_executable(zap_mapmarks_tests tests/mapmarks_tests.cpp)
target_link_libraries(zap_mapmarks_tests PRIVATE zap_core)
add_test(NAME mapmarks COMMAND zap_mapmarks_tests)

add_executable(zap_proxy_test tests/proxy_test.cpp)
target_compile_definitions(zap_proxy_test PRIVATE
                           ZAP_PROXY_PATH="$<TARGET_FILE:version_proxy>")
add_dependencies(zap_proxy_test version_proxy)
add_test(NAME proxy COMMAND zap_proxy_test)

add_executable(zap_offline_tests tests/offline_tests.cpp)
target_link_libraries(zap_offline_tests PRIVATE zap_core)
add_test(NAME offline COMMAND zap_offline_tests)
