add_executable(duet-askpass main.cpp)
duet_enable_warnings(duet-askpass)
target_link_libraries(duet-askpass PRIVATE Qt6::Core Qt6::Network)

# Installed next to the editor, where AskpassServer looks for it.
set_target_properties(duet-askpass PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# On macOS, it is copied into the editor's bundle (see src/CMakeLists.txt).
if(NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS duet-askpass RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT duet)
endif()
