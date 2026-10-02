# Unit tests -- plain executables, one per file, registered with ctest. Each
# returns non-zero on a failed CHECK (tests/ffb_test.h).

add_executable(test_version test_version.cpp)
target_include_directories(test_version PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(test_version PRIVATE /W3)
add_test(NAME test_version COMMAND test_version)

# The self-update's version compare and decision logic (#5), over a fake I/O --
# no network, no Windows calls, so only src/selfupdate.cpp is linked.
add_executable(test_selfupdate test_selfupdate.cpp ${CMAKE_SOURCE_DIR}/src/selfupdate.cpp)
target_include_directories(test_selfupdate PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(test_selfupdate PRIVATE /W3)
add_test(NAME test_selfupdate COMMAND test_selfupdate)

# The exe itself (Done bullet 1 of #2), as separate tests: with PASS_REGULAR_EXPRESSION
# set ctest ignores the exit code, so the output and the exit code cannot share one.
# Both run `FFB Co-op.exe --version`, which stops before any network or folder work
# (#6): the suite never depends on the network or on what coopmods.com serves. The
# flow's screens and exit codes are test_app's, over fakes.
# ffb_coop_exits_0: runs and exits 0 (no pass regex, so the exit code decides).
add_test(NAME ffb_coop_exits_0 COMMAND ffb_coop --version)
# ffb_coop_version_line: prints the version line first.
add_test(NAME ffb_coop_version_line COMMAND ffb_coop --version)
set_tests_properties(ffb_coop_version_line PROPERTIES
    PASS_REGULAR_EXPRESSION "^FFB Co-op [0-9]+\\.[0-9]+\\.[0-9]+")
# ffb_coop_offline_not_installed: the whole exe with no argument, in build\Release
# (no game, nothing installed). FFB_COOP_OFFLINE=1 makes every fetch fail
# (src/app_win.cpp), so this takes the offline path whatever coopmods.com serves.
# ctest pipes the output, so the key wait is skipped (WindowsAppIo::wait_key).
add_test(NAME ffb_coop_offline_not_installed COMMAND ffb_coop)
set_tests_properties(ffb_coop_offline_not_installed PROPERTIES
    PASS_REGULAR_EXPRESSION "Could not reach coopmods.com, and FFB Co-op is not installed in ")
# Every test that runs the exe: never the live server, and never a hang.
set_tests_properties(ffb_coop_exits_0 ffb_coop_version_line ffb_coop_offline_not_installed
    PROPERTIES ENVIRONMENT "FFB_COOP_OFFLINE=1" TIMEOUT 30)

# games.json parser and the network file-name rule (#3). third_party/json.hpp is
# nlohmann/json 3.12.0, the copy mewgenics-coop's loader uses.
add_executable(test_games test_games.cpp ${CMAKE_SOURCE_DIR}/src/games.cpp ${CMAKE_SOURCE_DIR}/src/plain_name.cpp)
target_include_directories(test_games PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(test_games PRIVATE /W3)
add_test(NAME test_games COMMAND test_games)

# Game-folder finder (#3).
add_executable(test_finder test_finder.cpp ${CMAKE_SOURCE_DIR}/src/finder.cpp ${CMAKE_SOURCE_DIR}/src/games.cpp ${CMAKE_SOURCE_DIR}/src/plain_name.cpp)
target_include_directories(test_finder PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(test_finder PRIVATE /W3)
add_test(NAME test_finder COMMAND test_finder)

# #4: sha256 (src/net.cpp) and the package update (src/package.cpp), with a fake network.
# net.cpp sends the dev credential (#26), so everything that links it links src/channel.cpp too.
add_executable(test_sha256 test_sha256.cpp ${CMAKE_SOURCE_DIR}/src/net.cpp ${CMAKE_SOURCE_DIR}/src/channel.cpp)
target_include_directories(test_sha256 PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_sha256 COMMAND test_sha256)
add_executable(test_package test_package.cpp ${CMAKE_SOURCE_DIR}/src/package.cpp ${CMAKE_SOURCE_DIR}/src/net.cpp ${CMAKE_SOURCE_DIR}/src/channel.cpp ${CMAKE_SOURCE_DIR}/src/plain_name.cpp
    ${CMAKE_SOURCE_DIR}/src/signature.cpp ${CMAKE_SOURCE_DIR}/third_party/tweetnacl.c)
target_include_directories(test_package PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party)
add_test(NAME test_package COMMAND test_package)

# #21: the ed25519 signature check -- RFC 8032 vectors, the .sig format, and the
# valid / missing / wrong-key / tampered-byte cases on a games.json and a manifest.
# tests/ffb_sign_test.h signs fixtures with the RFC 8032 test key, never the real one.
add_executable(test_signature test_signature.cpp ${CMAKE_SOURCE_DIR}/src/signature.cpp ${CMAKE_SOURCE_DIR}/third_party/tweetnacl.c)
target_include_directories(test_signature PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(test_signature PRIVATE /W3)
add_test(NAME test_signature COMMAND test_signature)

# #6: the start flow over a fake network, self-update, process start and key press,
# against a real temporary game folder.
add_executable(test_app test_app.cpp ${CMAKE_SOURCE_DIR}/src/app.cpp ${CMAKE_SOURCE_DIR}/src/selfupdate.cpp
    ${CMAKE_SOURCE_DIR}/src/games.cpp ${CMAKE_SOURCE_DIR}/src/finder.cpp ${CMAKE_SOURCE_DIR}/src/plain_name.cpp
    ${CMAKE_SOURCE_DIR}/src/package.cpp ${CMAKE_SOURCE_DIR}/src/net.cpp ${CMAKE_SOURCE_DIR}/src/channel.cpp
    ${CMAKE_SOURCE_DIR}/src/signature.cpp ${CMAKE_SOURCE_DIR}/third_party/tweetnacl.c)
target_include_directories(test_app PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(test_app PRIVATE NOMINMAX)
add_test(NAME test_app COMMAND test_app)

# #6: the command line handed to the package launcher -- quoting and passthrough.
add_executable(test_cmdline test_cmdline.cpp ${CMAKE_SOURCE_DIR}/src/app.cpp ${CMAKE_SOURCE_DIR}/src/selfupdate.cpp
    ${CMAKE_SOURCE_DIR}/src/games.cpp ${CMAKE_SOURCE_DIR}/src/finder.cpp ${CMAKE_SOURCE_DIR}/src/plain_name.cpp
    ${CMAKE_SOURCE_DIR}/src/package.cpp ${CMAKE_SOURCE_DIR}/src/net.cpp ${CMAKE_SOURCE_DIR}/src/channel.cpp
    ${CMAKE_SOURCE_DIR}/src/signature.cpp ${CMAKE_SOURCE_DIR}/third_party/tweetnacl.c)
target_include_directories(test_cmdline PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(test_cmdline PRIVATE NOMINMAX)
add_test(NAME test_cmdline COMMAND test_cmdline)

# #26: the dev channel -- the channel table, the base64 and the rule that sends the
# dev credential to coopmods.com/dev/ and nowhere else.
add_executable(test_channel test_channel.cpp ${CMAKE_SOURCE_DIR}/src/channel.cpp)
target_include_directories(test_channel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(test_channel PRIVATE /W3)
add_test(NAME test_channel COMMAND test_channel)

# #26: ffb_version.h built with the dev switch, as the ffb_coop_dev target builds it.
add_executable(test_version_dev test_version_dev.cpp ${CMAKE_SOURCE_DIR}/src/channel.cpp)
target_include_directories(test_version_dev PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_version_dev PRIVATE FFB_DEV_CHANNEL)
target_compile_options(test_version_dev PRIVATE /W3)
add_test(NAME test_version_dev COMMAND test_version_dev)

# #26: the dev exe itself, the same three checks as FFB Co-op.exe's above. It
# prints its own name and dev version, and offline it says the dev package is
# not installed (FFB_COOP_OFFLINE=1). Only when it is built (FFB_BUILD_DEV).
if(TARGET ffb_coop_dev)
    add_test(NAME ffb_coop_dev_exits_0 COMMAND ffb_coop_dev --version)
    add_test(NAME ffb_coop_dev_version_line COMMAND ffb_coop_dev --version)
    set_tests_properties(ffb_coop_dev_version_line PROPERTIES
        PASS_REGULAR_EXPRESSION "^FFB Co-op - dev [0-9]+\\.0\\.[1-9][0-9]*")
    add_test(NAME ffb_coop_dev_offline_not_installed COMMAND ffb_coop_dev)
    set_tests_properties(ffb_coop_dev_offline_not_installed PROPERTIES
        PASS_REGULAR_EXPRESSION "Could not reach coopmods.com, and FFB Co-op is not installed in ")
    set_tests_properties(ffb_coop_dev_exits_0 ffb_coop_dev_version_line ffb_coop_dev_offline_not_installed
        PROPERTIES ENVIRONMENT "FFB_COOP_OFFLINE=1" TIMEOUT 30)
endif()

# #26: cmake/dev_password.cmake -- where the dev exe's shared password is read
# from at build time, and that a build with none stops. It never reads the real one.
add_test(NAME test_dev_password
         COMMAND ${CMAKE_COMMAND} -DSCRIPT=${CMAKE_SOURCE_DIR}/cmake/dev_password.cmake
                 -DWORK=${CMAKE_CURRENT_BINARY_DIR}/dev_password_work -P ${CMAKE_CURRENT_SOURCE_DIR}/test_dev_password.cmake)

# tools/publish.py, the SHIP_CMD (#7): its Python unit tests, so TEST_CMD runs them beside the C++
# ones. CI runs the same suite in .github/workflows/doc-rules.yml.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME test_publish
             COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_publish.py
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
else()
    message(WARNING "no Python 3 found: test_publish (tools/publish.py) is not registered")
endif()
