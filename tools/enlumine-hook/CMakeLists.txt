add_executable(enlumine-hook main.cpp)
target_link_libraries(enlumine-hook PRIVATE Qt6::Core Qt6::Network)

# Installed next to the editor, where ClaudeLauncher looks for it.
set_target_properties(enlumine-hook PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# On macOS, it is copied into the editor's bundle (see src/CMakeLists.txt).
if(NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS enlumine-hook RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT enlumine)
endif()
