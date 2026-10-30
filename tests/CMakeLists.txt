# One Qt Test executable per tst_*.cpp file. Tests that create
# widgets run without a display (Qt "offscreen" platform).
# Each test also writes its report to test-logs/: on the Windows CI, the
# report Qt Test prints to stdout does not reach ctest's output.
set(KDK_TEST_LOG_DIR ${CMAKE_BINARY_DIR}/test-logs)
file(MAKE_DIRECTORY ${KDK_TEST_LOG_DIR})
# What the tests need of each system's programs and shell: one source per
# system.
if(WIN32)
    add_library(kdk_test_support STATIC support/windows/SystemFixturesWindows.cpp)
else()
    add_library(kdk_test_support STATIC support/unix/SystemFixturesUnix.cpp)
endif()
target_include_directories(kdk_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kdk_test_support PUBLIC Qt6::Core)
kdk_enable_warnings(kdk_test_support)

# The test `name`, from <folder>/<name>.cpp when a folder follows: unix or
# windows, for a test of one system.
function(kdk_add_test name)
    set(source ${name}.cpp)
    if(ARGC GREATER 1)
        set(source ${ARGV1}/${name}.cpp)
    endif()
    add_executable(${name} ${source})
    kdk_enable_warnings(${name})
    target_link_libraries(${name} PRIVATE kdk_core kdk_test_support Qt6::Test)
    add_test(NAME ${name} COMMAND ${name} -o -,txt -o ${KDK_TEST_LOG_DIR}/${name}.txt,txt)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # Windows has no rpath: the tests find Qt's DLLs through PATH, and
    # Graphviz's in the executable's folder, where the package takes them.
    # Qt's installation comes first: the copy windeployqt puts in bin has
    # no offscreen platform plugin, and a test without one aborts. The last
    # prepend ends up first.
    if(WIN32)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/bin;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endfunction()

# The test of a feature of the system, for the system the build is for:
# windows/tst_<name>_windows.cpp or unix/tst_<name>_unix.cpp; tst_<name>.cpp,
# when there is one, holds the behavior every system shares.
function(kdk_add_system_test name)
    if(WIN32)
        kdk_add_test(${name}_windows windows)
    else()
        kdk_add_test(${name}_unix unix)
    endif()
endfunction()

# The system's ways (src/system).
kdk_add_test(tst_system)
kdk_add_system_test(tst_system)
kdk_add_test(tst_shell)
kdk_add_system_test(tst_shell)
kdk_add_test(tst_syntax)
kdk_add_test(tst_outline)
kdk_add_test(tst_navigation)
kdk_add_test(tst_changes)
kdk_add_test(tst_git)
# The hooks are shell scripts; Windows has no test of its own yet.
if(NOT WIN32)
    kdk_add_test(tst_git_unix unix)
endif()
kdk_add_test(tst_gitsync)
kdk_add_test(tst_merge)
kdk_add_test(tst_gitcommandguard)
kdk_add_system_test(tst_gitsync)
target_compile_definitions(tst_gitsync PRIVATE KDK_ASKPASS_PATH="$<TARGET_FILE:kodamiko-askpass>")
add_dependencies(tst_gitsync kodamiko-askpass)
kdk_add_test(tst_history)
kdk_add_test(tst_editor)
kdk_add_test(tst_completion)
# A scripted language server, for the LSP client's tests.
add_executable(fake-lsp-server FakeLspServer.cpp)
kdk_enable_warnings(fake-lsp-server)
target_link_libraries(fake-lsp-server PRIVATE Qt6::Core kdk_system)

kdk_add_test(tst_lsp)
target_compile_definitions(tst_lsp PRIVATE FAKE_LSP_SERVER_PATH="$<TARGET_FILE:fake-lsp-server>")
add_dependencies(tst_lsp fake-lsp-server)
kdk_add_test(tst_codeintelligence)
kdk_add_test(tst_refactor)
kdk_add_test(tst_index)
# The refactoring corpus: one row per case of tests/refactoring.
kdk_add_test(tst_refactorcorpus)
target_compile_definitions(tst_refactorcorpus PRIVATE KDK_REFACTORING_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/refactoring")
kdk_add_test(tst_projects)
kdk_add_test(tst_recipes)
# Visual Studio exists on Windows only.
if(WIN32)
    kdk_add_test(tst_recipes_windows windows)
endif()
# Kodamiko's own recipes, from the source tree.
foreach(test tst_projects tst_recipes)
    set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT "KDK_RECIPES_DIR=${CMAKE_SOURCE_DIR}/tools/recipes")
endforeach()
kdk_add_test(tst_run)
kdk_add_system_test(tst_run)
# The Markdown preview's web engine: no Chromium sandbox on CI machines.
set_property(TEST tst_editor APPEND PROPERTY ENVIRONMENT "QTWEBENGINE_DISABLE_SANDBOX=1")
kdk_add_test(tst_search)
kdk_add_test(tst_textfile)
kdk_add_test(tst_imageview)
kdk_add_test(tst_workspace)
kdk_add_test(tst_windowbars)
kdk_add_test(tst_contextmenus)
kdk_add_test(tst_filetree)
kdk_add_test(tst_terminal)
kdk_add_system_test(tst_terminal)
kdk_add_test(tst_terminalpaint)
kdk_add_test(tst_setup)
kdk_add_system_test(tst_setup)
kdk_add_test(tst_settings)
kdk_add_test(tst_fontsettings)
kdk_add_test(tst_motion)
kdk_add_test(tst_tooltips)
if(GRAPHVIZ_FOUND)
    kdk_add_test(tst_diagramview)
    # One test per diagram family.
    foreach(family sequence classes states datamodel journey c4 mindmap explainer)
        kdk_add_test(tst_diagram_${family})
    endforeach()
endif()
kdk_add_test(tst_diagnostics)
kdk_add_test(tst_agent)
kdk_add_test(tst_snackbar)
kdk_add_test(tst_claude)
kdk_add_system_test(tst_claude)
target_compile_definitions(tst_claude PRIVATE KDK_HOOK_PATH="$<TARGET_FILE:kodamiko-hook>")
add_dependencies(tst_claude kodamiko-hook)
kdk_add_test(tst_mcp)
target_compile_definitions(tst_mcp PRIVATE KDK_HOOK_PATH="$<TARGET_FILE:kodamiko-hook>"
                                           KDK_MCP_PATH="$<TARGET_FILE:kodamiko-mcp>")
add_dependencies(tst_mcp kodamiko-hook kodamiko-mcp)
kdk_add_test(tst_mcptools)
