# The native helper the AP client loads into the running game (see native/README.md).
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

add_library(zap_core STATIC src/iat.cpp src/build.cpp src/pipe.cpp src/mailbox.cpp
            src/game_calls.cpp src/menu.cpp src/skip.cpp
            src/apitem.cpp)
target_include_directories(zap_core PUBLIC src)
target_link_libraries(zap_core PUBLIC advapi32)

# The helper: hooks the game's SDL_GL_SwapWindow, in a known build only.
add_library(zexion_ap SHARED src/dllmain.cpp)
target_link_libraries(zexion_ap PRIVATE zap_core)

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
