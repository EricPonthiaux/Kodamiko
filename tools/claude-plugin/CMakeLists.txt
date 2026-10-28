# Kodamiko's plugin for its Claude session (claude --plugin-dir): copied next
# to the editor, where ClaudeLauncher looks for it.
set(KDK_PLUGIN_FILES
    .claude-plugin/plugin.json
    skills/diagrams/SKILL.md
    skills/diagrams/board-format.md)
foreach(file ${KDK_PLUGIN_FILES})
    configure_file(${file} ${CMAKE_BINARY_DIR}/bin/claude-plugin/${file} COPYONLY)
endforeach()

# On macOS, it is copied into the editor's bundle (kodamiko-bundle, see src/CMakeLists.txt).
if(NOT APPLE)
    include(GNUInstallDirs)
    install(DIRECTORY ${CMAKE_BINARY_DIR}/bin/claude-plugin/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kodamiko/claude-plugin COMPONENT kodamiko)
endif()
