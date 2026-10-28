add_executable(kodamiko-askpass main.cpp)
kdk_enable_warnings(kodamiko-askpass)
kdk_relink_while_running(kodamiko-askpass)
target_link_libraries(kodamiko-askpass PRIVATE Qt6::Core Qt6::Network kdk_log kdk_system)

# Installed next to the editor, where AskpassServer looks for it.
set_target_properties(kodamiko-askpass PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# On macOS, it is copied into the editor's bundle (see src/CMakeLists.txt).
if(NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS kodamiko-askpass RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT kodamiko)
endif()
