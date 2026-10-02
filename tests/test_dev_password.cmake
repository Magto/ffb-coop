# test_dev_password.cmake -- cmake/dev_password.cmake, the step that puts the
# dev channel's shared password into FFB Co-op - dev.exe at build time (#26).
# Run by ctest as `cmake -DSCRIPT=<cmake/dev_password.cmake> -DWORK=<dir> -P`.
#
# Every case runs the script in a clean environment whose HOME and USERPROFILE
# are an empty folder of its own, so the real password file is never read.
# The password used is "Ab1"; its \x escapes, worked out by hand, are
# \x41\x62\x31.
cmake_minimum_required(VERSION 3.21)

set(_failures 0)
macro(check cond what)
    cmake_language(EVAL CODE "if(${cond})\nset(_ok ON)\nelse()\nset(_ok OFF)\nendif()")
    if(_ok)
        message(STATUS "  ok    ${what}")
    else()
        message(STATUS "  FAIL  ${what}")
        math(EXPR _failures "${_failures} + 1")
    endif()
endmacro()

set(_expected "#define FFB_DEV_PASSWORD \"\\x41\\x62\\x31\"")

# run(<case> [ENV k=v ...]) -> _rc, _err, _header (the header's text, or "")
function(run name)
    set(_home "${WORK}/${name}/home")
    set(_out "${WORK}/${name}/ffb_dev_password.h")
    file(REMOVE_RECURSE "${WORK}/${name}")
    file(MAKE_DIRECTORY "${_home}")
    if(DEFINED ARG_HOME_FILE)
        file(WRITE "${_home}/.config/coopmods/ffb-dev-password" "${ARG_HOME_FILE}")
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env --unset=FFB_DEV_PASSWORD --unset=FFB_DEV_PASSWORD_FILE
                "HOME=${_home}" "USERPROFILE=${_home}" ${ARGN}
                ${CMAKE_COMMAND} -DOUT=${_out} -P ${SCRIPT}
        RESULT_VARIABLE _rc ERROR_VARIABLE _err OUTPUT_VARIABLE _ignored)
    set(_header "")
    if(EXISTS "${_out}")
        file(READ "${_out}" _header)
    endif()
    set(_rc "${_rc}" PARENT_SCOPE)
    set(_err "${_err}" PARENT_SCOPE)
    set(_header "${_header}" PARENT_SCOPE)
endfunction()

message(STATUS "no password anywhere: the build stops, nothing is written")
unset(ARG_HOME_FILE)
run(none)
check("NOT _rc EQUAL 0" "exit code non-zero")
string(FIND "${_err}" "needs the dev channel's shared password" _at)
check("_at GREATER -1" "the message says what is missing")
check("_header STREQUAL \"\"" "no header written")

message(STATUS "FFB_DEV_PASSWORD set: its value, escaped")
run(env "FFB_DEV_PASSWORD=Ab1")
check("_rc EQUAL 0" "exit code 0")
string(FIND "${_header}" "${_expected}" _at)
check("_at GREATER -1" "header holds the escaped bytes 41 62 31")
string(FIND "${_header}" "Ab1" _at)
check("_at EQUAL -1" "the plain password is not in the header")

message(STATUS "~/.config/coopmods/ffb-dev-password with its newline: stripped")
set(ARG_HOME_FILE "Ab1\n")
run(home)
unset(ARG_HOME_FILE)
check("_rc EQUAL 0" "exit code 0")
string(FIND "${_header}" "${_expected}" _at)
check("_at GREATER -1" "header holds the escaped bytes 41 62 31")

message(STATUS "FFB_DEV_PASSWORD_FILE naming a file: read from it")
file(WRITE "${WORK}/given-password" "Ab1\n")
run(file "FFB_DEV_PASSWORD_FILE=${WORK}/given-password")
check("_rc EQUAL 0" "exit code 0")
string(FIND "${_header}" "${_expected}" _at)
check("_at GREATER -1" "header holds the escaped bytes 41 62 31")

message(STATUS "FFB_DEV_PASSWORD_FILE naming no file: the build stops")
run(missing "FFB_DEV_PASSWORD_FILE=${WORK}/no-such-file")
check("NOT _rc EQUAL 0" "exit code non-zero")
check("_header STREQUAL \"\"" "no header written")

message(STATUS "an empty password file: the build stops")
set(ARG_HOME_FILE "\n")
run(empty)
unset(ARG_HOME_FILE)
check("NOT _rc EQUAL 0" "exit code non-zero")
string(FIND "${_err}" "is empty" _at)
check("_at GREATER -1" "the message says it is empty")
check("_header STREQUAL \"\"" "no header written")

if(_failures GREATER 0)
    message(FATAL_ERROR "${_failures} FAILED")
endif()
message(STATUS "all passed")
