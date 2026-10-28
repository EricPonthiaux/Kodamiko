add_executable(kodamiko-mcp main.cpp)
kdk_enable_warnings(kodamiko-mcp)
kdk_relink_while_running(kodamiko-mcp)
target_link_libraries(kodamiko-mcp PRIVATE Qt6::Core Qt6::Network kdk_log)

# Installed next to the editor, where ClaudeLauncher looks for it.
set_target_properties(kodamiko-mcp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# On macOS, it is copied into the editor's bundle (see src/CMakeLists.txt).
if(NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS kodamiko-mcp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT kodamiko)
endif()
