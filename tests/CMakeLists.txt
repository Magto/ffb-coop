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

# The exe itself (Done bullet 1 of #2), as two tests: with PASS_REGULAR_EXPRESSION
# set ctest ignores the exit code, so the output and the exit code cannot share one.
# ffb_coop_exits_0: runs and exits 0 (no pass regex, so the exit code decides).
add_test(NAME ffb_coop_exits_0 COMMAND ffb_coop)
# ffb_coop_version_line: prints the version line first.
add_test(NAME ffb_coop_version_line COMMAND ffb_coop)
set_tests_properties(ffb_coop_version_line PROPERTIES
    PASS_REGULAR_EXPRESSION "^FFB Co-op [0-9]+\\.[0-9]+\\.[0-9]+")

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
add_executable(test_sha256 test_sha256.cpp ${CMAKE_SOURCE_DIR}/src/net.cpp)
target_include_directories(test_sha256 PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_sha256 COMMAND test_sha256)
add_executable(test_package test_package.cpp ${CMAKE_SOURCE_DIR}/src/package.cpp ${CMAKE_SOURCE_DIR}/src/net.cpp ${CMAKE_SOURCE_DIR}/src/plain_name.cpp)
target_include_directories(test_package PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party)
add_test(NAME test_package COMMAND test_package)
