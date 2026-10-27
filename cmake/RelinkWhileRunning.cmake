# duet_relink_while_running(<target>): on Windows, <target> links even while
# a build of it runs (Duet itself, or a helper Claude started from it): see
# MoveRunningExecutableAside.cmake. Elsewhere, the system already lets a
# running executable be replaced.

set(duet_move_aside_script ${CMAKE_CURRENT_LIST_DIR}/MoveRunningExecutableAside.cmake)

function(duet_relink_while_running target)
    if(NOT WIN32)
        return()
    endif()
    add_custom_command(TARGET ${target} PRE_LINK
        COMMAND ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:${target}> -P ${duet_move_aside_script}
        VERBATIM)
endfunction()
