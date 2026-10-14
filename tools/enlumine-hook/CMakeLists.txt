add_executable(enlumine-hook main.cpp)
target_link_libraries(enlumine-hook PRIVATE Qt6::Core Qt6::Network)

# Installé à côté de l'éditeur, où ClaudeLauncher le cherche.
set_target_properties(enlumine-hook PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Sous macOS, il est copié dans le bundle de l'éditeur (voir src/CMakeLists.txt).
if(NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS enlumine-hook RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT enlumine)
endif()
