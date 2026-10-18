add_executable(duet-mcp main.cpp)
duet_enable_warnings(duet-mcp)
target_link_libraries(duet-mcp PRIVATE Qt6::Core Qt6::Network)

# Installed next to the editor, where ClaudeLauncher looks for it.
set_target_properties(duet-mcp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# On macOS, it is copied into the editor's bundle (see src/CMakeLists.txt).
if(NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS duet-mcp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT duet)
endif()
