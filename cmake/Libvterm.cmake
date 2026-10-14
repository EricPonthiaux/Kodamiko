# Cible statique `vterm`. Les tables d'encodage (*.inc) sont normalement
# générées par un script Perl ; elles sont pré-générées dans
# cmake/libvterm/encoding pour ne pas exiger Perl sur Windows.

file(GLOB VTERM_SOURCES ${libvterm_SOURCE_DIR}/src/*.c)

add_library(vterm STATIC ${VTERM_SOURCES})
target_include_directories(vterm
    PUBLIC  ${libvterm_SOURCE_DIR}/include
    PRIVATE ${libvterm_SOURCE_DIR}/src
            ${CMAKE_CURRENT_LIST_DIR}/libvterm)
set_target_properties(vterm PROPERTIES C_STANDARD 99)
if(MSVC)
    target_compile_options(vterm PRIVATE /w)
else()
    target_compile_options(vterm PRIVATE -w)
endif()
