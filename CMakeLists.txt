# FFB Co-op.exe -- one launcher for every FFB co-op mod.
#
# Same toolchain as mewgenics-coop: MSVC via CMake, Windows x64 only.
#
#   cmake -B build -A x64
#   cmake --build build --config Release
#   ctest --test-dir build -C Release
#
# -> "build\Release\FFB Co-op.exe" with a Visual Studio (multi-config) generator.
# With -DFFB_BUILD_DEV=ON at configure, also "build\Release\FFB Co-op - dev.exe" (#26).
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
# FFB Co-op - dev.exe (#26), opt-in (Martin, 2026-10-02 21:51): it needs the dev
# channel's shared password at build time (cmake/dev_password.cmake says where it
# is read from), and with ON and no password the build fails. The plain build and
# the release exe never touch the secret.
option(FFB_BUILD_DEV "build FFB Co-op - dev.exe too (needs the dev password)" OFF)

# FFB Co-op.exe and FFB Co-op - dev.exe (#26) are one source built twice: the dev
# exe is the same code with FFB_DEV_CHANNEL defined, which picks the dev channel
# in src/channel.cpp (coopmods.com/dev/, the `FFB Co-op dev\` package folder),
# the dev names and version in src/ffb_version.h, and the shared dev password
# sent to coopmods.com/dev/ (src/app_win.cpp). Not a fork: nothing else differs.
function(ffb_coop_exe target output_name)
    add_executable(${target} src/main.cpp res/ffb_coop.rc)
    # Self-update (#5): the logic, and its Windows side (WinHTTP, BCrypt).
    target_sources(${target} PRIVATE src/selfupdate.cpp src/selfupdate_win.cpp)
    target_link_libraries(${target} PRIVATE winhttp bcrypt)
    # The start flow (#6) and the pieces it wires together: games.json and the
    # finder (#3), the package update and its network (#4), the channel (#26).
    target_sources(${target} PRIVATE src/app.cpp src/app_win.cpp src/games.cpp src/finder.cpp
        src/plain_name.cpp src/package.cpp src/net.cpp src/channel.cpp)
    # The package launcher's environment: SteamAppId for a genuine Steam copy (#28).
    target_sources(${target} PRIVATE src/child_env.cpp)
    # The ed25519 signature check on games.json and the manifest (#21); the
    # arithmetic is TweetNaCl, public domain (THIRD-PARTY-NOTICES.md).
    target_sources(${target} PRIVATE src/signature.cpp third_party/tweetnacl.c)
    # The file name players see, space and all. The target keeps a plain name.
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${output_name}")
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_compile_options(${target} PRIVATE /W3)
    target_include_directories(${target} PRIVATE src third_party)
endfunction()

ffb_coop_exe(ffb_coop "FFB Co-op")
if(FFB_BUILD_DEV)
    ffb_coop_exe(ffb_coop_dev "FFB Co-op - dev")
    # The build switch. Target definitions reach res/ffb_coop.rc too, so the dev
    # exe's version resource carries the dev name and version.
    target_compile_definitions(ffb_coop_dev PRIVATE FFB_DEV_CHANNEL)
    # The shared dev password, read at every build into a header in the build
    # tree (never the source tree); the build stops when there is none.
    set(FFB_DEV_PASSWORD_HEADER ${CMAKE_BINARY_DIR}/generated/ffb_dev_password.h)
    add_custom_target(ffb_dev_password
        COMMAND ${CMAKE_COMMAND} -DOUT=${FFB_DEV_PASSWORD_HEADER} -P ${CMAKE_SOURCE_DIR}/cmake/dev_password.cmake
        BYPRODUCTS ${FFB_DEV_PASSWORD_HEADER}
        COMMENT "Reading the dev channel's shared password (cmake/dev_password.cmake)"
        VERBATIM)
    add_dependencies(ffb_coop_dev ffb_dev_password)
    target_include_directories(ffb_coop_dev PRIVATE ${CMAKE_BINARY_DIR}/generated)
endif()
# res/ffb_coop.rc includes ffb_version.h, so the version resource and the
# printed version line come from the same numbers.
set_property(SOURCE res/ffb_coop.rc APPEND PROPERTY INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/src)

if(FFB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# third_party/tweetnacl.c is kept byte-for-byte as published; its narrowing
# conversions and unsigned negation are deliberate (constant-time arithmetic),
# so those three MSVC warnings are silenced for that one file, everywhere it builds.
set(_tweetnacl_dirs ${CMAKE_SOURCE_DIR})
if(FFB_BUILD_TESTS)
    list(APPEND _tweetnacl_dirs ${CMAKE_SOURCE_DIR}/tests)
endif()
set_source_files_properties(${CMAKE_SOURCE_DIR}/third_party/tweetnacl.c DIRECTORY ${_tweetnacl_dirs}
    PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4146;/wd4018")
