# Fails when Ninja recorded no header dependencies for objects it has already
# built.
#
# Ninja learns those dependencies by stripping a prefix from cl.exe's
# /showIncludes output, and it stores the prefix CMake observed at configure
# time. When the configure and the build see different compiler languages or
# console code pages, the prefix stops matching and ninja silently records
# nothing. Header edits then rebuild nothing, a header that changes a class
# layout leaves half the objects on the old one, and the result links, runs and
# crashes on a wild pointer with no bad code in sight. Clean builds never show
# it, which is why CI is not where this gets caught.
#
#   cmake -DBUILD_DIR=out/build/windows-dev -P script/check_ninja_deps.cmake
cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "Pass the configured build tree with -DBUILD_DIR=<path>")
endif ()
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE)

set(_cache "${BUILD_DIR}/CMakeCache.txt")
if (NOT EXISTS "${_cache}")
    message(FATAL_ERROR "Not a configured build tree: ${BUILD_DIR}")
endif ()

if (NOT DEFINED NINJA)
    file(STRINGS "${_cache}" _entry REGEX "^CMAKE_MAKE_PROGRAM:[^=]*=")
    if (_entry)
        string(REGEX REPLACE "^CMAKE_MAKE_PROGRAM:[^=]*=" "" NINJA "${_entry}")
    endif ()
endif ()
if (NOT NINJA OR NOT EXISTS "${NINJA}")
    find_program(NINJA NAMES ninja ninja-build)
endif ()
if (NOT NINJA)
    message(FATAL_ERROR "ninja was not found; pass -DNINJA=<path>")
endif ()

execute_process(
    COMMAND "${NINJA}" -C "${BUILD_DIR}" -t deps
    OUTPUT_VARIABLE _deps
    ERROR_VARIABLE _deps_error
    RESULT_VARIABLE _result)
if (NOT "${_result}" STREQUAL "0")
    message(FATAL_ERROR "ninja -t deps failed: ${_deps_error}")
endif ()

string(REPLACE "\n" ";" _lines "${_deps}")
set(_empty)
set(_tracked 0)
foreach (_line IN LISTS _lines)
    if (NOT _line MATCHES "^([^:]+\.(obj|o)): #deps ([0-9]+),")
        continue()
    endif ()
    set(_object "${CMAKE_MATCH_1}")
    set(_count "${CMAKE_MATCH_3}")
    if (NOT EXISTS "${BUILD_DIR}/${_object}")
        continue()
    endif ()
    # Qt generates these, and several of them legitimately include nothing at all:
    # rcc emits pure data, and mocs_compilation.cpp is a stub when a target has no
    # Q_OBJECT header. Their inputs are tracked through the autogen target instead.
    if (_object MATCHES "_autogen/")
        continue()
    endif ()
    if (_count GREATER 0)
        math(EXPR _tracked "${_tracked} + 1")
    else ()
        list(APPEND _empty "${_object}")
    endif ()
endforeach ()

list(LENGTH _empty _empty_count)
if (_empty_count EQUAL 0)
    message(STATUS "Header dependency tracking is live: ${_tracked} objects.")
    return()
endif ()

list(SUBLIST _empty 0 5 _sample)
string(REPLACE ";" "\n  " _sample "${_sample}")
message(FATAL_ERROR
    "${_empty_count} built objects have no recorded header dependencies "
    "(${_tracked} do). Incremental builds in this tree are silently stale.\n"
    "  ${_sample}\n"
    "On Windows this is the /showIncludes prefix mismatch: configure the tree "
    "and build it from the same shell, with the same VSLANG and the same "
    "console code page. Installing the MSVC English language pack makes "
    "VSLANG=1033 effective and removes the code page from the equation. Until "
    "the check passes, only a clean build is trustworthy.")
