add_executable(duet-hook main.cpp)
duet_enable_warnings(duet-hook)
target_link_libraries(duet-hook PRIVATE Qt6::Core Qt6::Network duet_log)

# Installed next to the editor, where ClaudeLauncher looks for it.
set_target_properties(duet-hook PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# On macOS, it is copied into the editor's bundle (see src/CMakeLists.txt).
if(NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS duet-hook RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT duet)
endif()
