# FFB Co-op.exe -- one launcher for every FFB co-op mod.
#
# Same toolchain as mewgenics-coop: MSVC via CMake, Windows x64 only.
#
#   cmake -B build -A x64
#   cmake --build build --config Release
#   ctest --test-dir build -C Release
#
# -> "build\Release\FFB Co-op.exe" with a Visual Studio (multi-config) generator.
cmake_minimum_required(VERSION 3.21)
project(ffb_coop LANGUAGES C CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "FFB Co-op.exe is a Windows x64 console app -- build it with MSVC, see CLAUDE.md")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT: players download this one exe on its own, so it cannot assume a
# VC redist is installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(FFB_BUILD_TESTS "build the unit tests in tests/" ON)

add_executable(ffb_coop src/main.cpp res/ffb_coop.rc)
# Self-update (#5): the logic, and its Windows side (WinHTTP, BCrypt).
target_sources(ffb_coop PRIVATE src/selfupdate.cpp src/selfupdate_win.cpp)
target_link_libraries(ffb_coop PRIVATE winhttp bcrypt)
# The start flow (#6) and the pieces it wires together: games.json and the
# finder (#3), the package update and its network (#4).
target_sources(ffb_coop PRIVATE src/app.cpp src/app_win.cpp src/games.cpp src/finder.cpp
    src/plain_name.cpp src/package.cpp src/net.cpp)
# The file name players see, space and all. The target keeps a plain name.
set_target_properties(ffb_coop PROPERTIES OUTPUT_NAME "FFB Co-op")
target_compile_definitions(ffb_coop PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
target_compile_options(ffb_coop PRIVATE /W3)
target_include_directories(ffb_coop PRIVATE src third_party)
# res/ffb_coop.rc includes ffb_version.h, so the version resource and the
# printed version line come from the same numbers.
set_property(SOURCE res/ffb_coop.rc APPEND PROPERTY INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/src)

if(FFB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
