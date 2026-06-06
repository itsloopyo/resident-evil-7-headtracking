cmake_minimum_required(VERSION 3.20)
project(RE7HeadTracking VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# MinHook for native function detouring
include(FetchContent)
# Pinned to an exact commit for reproducible, supply-chain-safe builds.
# A floating ref (master) would let upstream changes flow into release
# binaries silently. The last release tag (v1.3.3) predates MinHook's CMake
# support, so a master commit is required; bump deliberately with a build +
# test. This commit ("Add support for VS2026") is master HEAD as of 2026-05.
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        05c06c5bbca226b72ffb40fc0caaef33bcaf6f74
)
FetchContent_MakeAvailable(minhook)

# cameraunlock-core shared library (static) with hook manager + REFramework utils
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_REFRAMEWORK ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern" CACHE STRING "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# Plugin sources
add_library(RE7HeadTracking SHARED
    src/pch.cpp
    src/plugin_main.cpp
    src/core/mod.cpp
    src/core/config.cpp
    src/core/logger.cpp
    src/camera/camera_hook.cpp
    src/camera/game_state_detector.cpp
)

target_precompile_headers(RE7HeadTracking PRIVATE src/pch.h)

set_target_properties(RE7HeadTracking PROPERTIES
    OUTPUT_NAME "RE7HeadTracking"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_link_libraries(RE7HeadTracking PRIVATE
    cameraunlock cameraunlock_hooks cameraunlock_reframework ws2_32)

target_include_directories(RE7HeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern
)

# Host-side unit tests (config validation). Off by default so normal/CI
# plugin builds are unaffected; enable with -DRE7HT_BUILD_TESTS=ON.
option(RE7HT_BUILD_TESTS "Build RE7 head tracking host unit tests" OFF)
if(RE7HT_BUILD_TESTS)
    add_executable(re7ht_tests
        tests/test_main.cpp
        tests/config_tests.cpp
        src/core/config.cpp
        src/core/logger.cpp
    )
    target_include_directories(re7ht_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern
    )
    target_link_libraries(re7ht_tests PRIVATE cameraunlock ws2_32)
    enable_testing()
    add_test(NAME re7ht_tests COMMAND re7ht_tests)
endif()
