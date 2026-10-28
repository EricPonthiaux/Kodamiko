# clang-format and clang-tidy for Kodamiko's own code (src/, tests/, tools/),
# configured by .clang-format and .clang-tidy at the root of the project.
#
#   cmake --build build --target format        reformats the sources
#   cmake --build build --target format-check  fails if a source is not formatted
#   cmake --build build --target tidy          analyzes every source
#
# -DKDK_CLANG_TIDY=ON also analyzes each source as it is compiled. Include
# this file after the dependencies: they must not be analyzed.

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
find_program(RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy run-clang-tidy.py)

file(GLOB_RECURSE kdk_formatted_sources CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/tests/*.cpp ${CMAKE_SOURCE_DIR}/tests/*.h
    ${CMAKE_SOURCE_DIR}/tools/*.cpp ${CMAKE_SOURCE_DIR}/tools/*.h)
# The refactoring corpus is test data, compared byte for byte.
list(FILTER kdk_formatted_sources EXCLUDE REGEX "^${CMAKE_SOURCE_DIR}/tests/refactoring/")

if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${kdk_formatted_sources}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Formatting the sources with clang-format"
        VERBATIM)
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror ${kdk_formatted_sources}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking the formatting of the sources"
        VERBATIM)
else()
    message(STATUS "clang-format not found: no format targets")
endif()

if(RUN_CLANG_TIDY_EXECUTABLE AND CLANG_TIDY_EXECUTABLE)
    add_custom_target(tidy
        COMMAND ${RUN_CLANG_TIDY_EXECUTABLE} -quiet
            -clang-tidy-binary ${CLANG_TIDY_EXECUTABLE}
            -p ${CMAKE_BINARY_DIR}
            "^${CMAKE_SOURCE_DIR}/(src|tests|tools)/"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Analyzing the sources with clang-tidy"
        VERBATIM)
else()
    message(STATUS "clang-tidy or run-clang-tidy not found: no tidy target")
endif()

option(KDK_CLANG_TIDY "Analyze each source with clang-tidy as it is compiled" OFF)
if(KDK_CLANG_TIDY)
    if(NOT CLANG_TIDY_EXECUTABLE)
        message(FATAL_ERROR "KDK_CLANG_TIDY is ON but clang-tidy was not found")
    endif()
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXECUTABLE})
    # Files generated in the build directory (moc, rcc) are compiled too:
    # clang-tidy reads the closest .clang-tidy, this one, and checks nothing
    # that could fire there.
    file(WRITE ${CMAKE_BINARY_DIR}/.clang-tidy "Checks: '-*,misc-definitions-in-headers'\n")
endif()

# The compiler warnings Kodamiko's own code must not trigger (see CLAUDE.md).
function(kdk_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        # -Wno-missing-field-initializers: an aggregate initialized with
        # some of its fields leaves the others to their default member
        # initializers, which every member has.
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
            -Wcast-qual -Wimplicit-fallthrough -Wno-missing-field-initializers)
    endif()
endfunction()
