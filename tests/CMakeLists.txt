# Unit tests -- plain executables, one per file, registered with ctest. Each
# returns non-zero on a failed CHECK (tests/ffb_test.h).

add_executable(test_version test_version.cpp)
target_include_directories(test_version PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(test_version PRIVATE /W3)
add_test(NAME test_version COMMAND test_version)

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
