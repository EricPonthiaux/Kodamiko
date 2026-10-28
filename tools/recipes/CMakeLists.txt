# Kodamiko's built-in recipes (see src/recipes/Recipe.h): copied next to the
# editor, where RecipeLibrary looks for them.
file(GLOB_RECURSE KDK_RECIPE_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*/*)
foreach(file ${KDK_RECIPE_FILES})
    configure_file(${file} ${CMAKE_BINARY_DIR}/bin/recipes/${file} COPYONLY)
endforeach()

# On macOS, they are copied into the editor's bundle (kodamiko-bundle, see src/CMakeLists.txt).
if(NOT APPLE)
    include(GNUInstallDirs)
    install(DIRECTORY ${CMAKE_BINARY_DIR}/bin/recipes/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kodamiko/recipes COMPONENT kodamiko)
endif()
