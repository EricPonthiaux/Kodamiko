# Run before linking an executable on Windows:
#   cmake -DEXECUTABLE=<path> -P MoveRunningExecutableAside.cmake
#
# Windows refuses to write an executable that is running (LNK1168), but it
# lets it be renamed: the running process keeps its image under the new name.
# The executable is moved aside and copied back, so that the linker writes
# an unlocked file and can still link incrementally. The copies moved aside
# by earlier builds are deleted once nothing runs them any more.

file(GLOB set_aside "${EXECUTABLE}.*.old")
foreach(old IN LISTS set_aside)
    # Fails while that build still runs: deleted by a later build.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -f "${old}" OUTPUT_QUIET ERROR_QUIET)
endforeach()

if(NOT EXISTS "${EXECUTABLE}")
    return()
endif()

string(TIMESTAMP stamp "%Y%m%d%H%M%S")
string(RANDOM LENGTH 6 suffix)
set(aside "${EXECUTABLE}.${stamp}${suffix}.old")
file(RENAME "${EXECUTABLE}" "${aside}" RESULT renamed)
if(NOT renamed EQUAL 0)
    message(WARNING "Could not move ${EXECUTABLE} aside before linking it: ${renamed}")
    return()
endif()
file(COPY_FILE "${aside}" "${EXECUTABLE}")
# Not running: the copy moved aside goes at once.
execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -f "${aside}" OUTPUT_QUIET ERROR_QUIET)
