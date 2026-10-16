# Static `qscintilla` target, adapted from src/qscintilla.pro (QScintilla
# only ships a qmake project).

set(QSCI_ROOT ${qscintilla_SOURCE_DIR})

file(GLOB QSCI_PUBLIC_HEADERS ${QSCI_ROOT}/src/Qsci/*.h)
file(GLOB QSCI_WRAPPER_SOURCES ${QSCI_ROOT}/src/*.cpp)
file(GLOB QSCI_SCINTILLA_SOURCES
    ${QSCI_ROOT}/scintilla/src/*.cpp
    ${QSCI_ROOT}/scintilla/lexlib/*.cpp
    ${QSCI_ROOT}/scintilla/lexers/*.cpp)

add_library(qscintilla STATIC
    ${QSCI_PUBLIC_HEADERS}
    ${QSCI_ROOT}/src/ListBoxQt.h
    ${QSCI_ROOT}/src/SciAccessibility.h
    ${QSCI_ROOT}/src/SciClasses.h
    ${QSCI_ROOT}/src/ScintillaQt.h
    ${QSCI_WRAPPER_SOURCES}
    ${QSCI_SCINTILLA_SOURCES})

target_include_directories(qscintilla
    PUBLIC  ${QSCI_ROOT}/src
    PRIVATE ${QSCI_ROOT}/scintilla/include
            ${QSCI_ROOT}/scintilla/lexlib
            ${QSCI_ROOT}/scintilla/src)

target_compile_definitions(qscintilla PRIVATE SCINTILLA_QT SCI_LEXER INCLUDE_DEPRECATED_FEATURES)
target_link_libraries(qscintilla PUBLIC Qt6::Widgets Qt6::PrintSupport)
set_target_properties(qscintilla PROPERTIES AUTOMOC ON CXX_STANDARD 17)

# Third-party code: we do not want its warnings in our build.
if(MSVC)
    target_compile_options(qscintilla PRIVATE /w)
else()
    target_compile_options(qscintilla PRIVATE -w)
endif()
