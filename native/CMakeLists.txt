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
add_compile_options(/W4 /WX /permissive- /utf-8)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

add_library(zap_core STATIC src/iat.cpp src/build.cpp src/pipe.cpp)
target_include_directories(zap_core PUBLIC src)

add_library(zexion_ap SHARED src/dllmain.cpp)
target_link_libraries(zexion_ap PRIVATE zap_core)

enable_testing()
add_executable(zap_tests tests/tests.cpp)
target_link_libraries(zap_tests PRIVATE zap_core)
add_test(NAME zap_tests COMMAND zap_tests)
