# Duet's plugin for its Claude session (claude --plugin-dir): copied next
# to the editor, where ClaudeLauncher looks for it.
set(DUET_PLUGIN_FILES
    .claude-plugin/plugin.json
    skills/diagrams/SKILL.md
    skills/diagrams/board-format.md)
foreach(file ${DUET_PLUGIN_FILES})
    configure_file(${file} ${CMAKE_BINARY_DIR}/bin/claude-plugin/${file} COPYONLY)
endforeach()

# On macOS, it is copied into the editor's bundle (see src/CMakeLists.txt).
if(NOT APPLE)
    include(GNUInstallDirs)
    install(DIRECTORY ${CMAKE_BINARY_DIR}/bin/claude-plugin/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/duet/claude-plugin COMPONENT duet)
endif()
