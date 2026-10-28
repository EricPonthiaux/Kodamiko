add_executable(kodamiko-hook main.cpp)
kdk_enable_warnings(kodamiko-hook)
kdk_relink_while_running(kodamiko-hook)
target_link_libraries(kodamiko-hook PRIVATE Qt6::Core Qt6::Network kdk_log kdk_system)

# Installed next to the editor, where ClaudeLauncher looks for it.
set_target_properties(kodamiko-hook PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# On macOS, it is copied into the editor's bundle (see src/CMakeLists.txt).
if(NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS kodamiko-hook RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT kodamiko)
endif()
