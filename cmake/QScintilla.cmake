# Static `qscintilla` target, adapted from src/qscintilla.pro (QScintilla
# only ships a qmake project).

set(QSCI_ROOT ${qscintilla_SOURCE_DIR})

file(GLOB QSCI_PUBLIC_HEADERS ${QSCI_ROOT}/src/Qsci/*.h)
file(GLOB QSCI_WRAPPER_SOURCES ${QSCI_ROOT}/src/*.cpp)

# PlatQt.cpp builds each style's QFont from its family name only, so the
# font's OpenType features cannot be chosen. Our copy reads a "#liga"
# suffix on the family: it turns the programming ligatures on, and they are
# off otherwise (see ScintillaStyles::applyDefaultStyle).
list(REMOVE_ITEM QSCI_WRAPPER_SOURCES ${QSCI_ROOT}/src/PlatQt.cpp)
file(READ ${QSCI_ROOT}/src/PlatQt.cpp QSCI_PLATQT)
set(QSCI_FACE_LINE "    f->setFamily(fp.faceName);")
string(FIND "${QSCI_PLATQT}" "${QSCI_FACE_LINE}" QSCI_FACE_AT)
if(QSCI_FACE_AT EQUAL -1)
    message(FATAL_ERROR "QScintilla's PlatQt.cpp changed: update the ligature patch in cmake/QScintilla.cmake")
endif()
string(REPLACE "${QSCI_FACE_LINE}" [[
    {
        // Duet: "family#liga" turns the ligatures on.
        QString face = QString::fromUtf8(fp.faceName);
        const bool ligatures = face.endsWith(QLatin1String("#liga"));
        if (ligatures)
            face.chop(5);
        f->setFamily(face);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        f->setFeature(QFont::Tag("liga"), ligatures ? 1 : 0);
        f->setFeature(QFont::Tag("calt"), ligatures ? 1 : 0);
#endif
    }]] QSCI_PLATQT "${QSCI_PLATQT}")
file(WRITE ${CMAKE_BINARY_DIR}/qscintilla/PlatQt.cpp.new "${QSCI_PLATQT}")
file(COPY_FILE ${CMAKE_BINARY_DIR}/qscintilla/PlatQt.cpp.new ${CMAKE_BINARY_DIR}/qscintilla/PlatQt.cpp ONLY_IF_DIFFERENT)
list(APPEND QSCI_WRAPPER_SOURCES ${CMAKE_BINARY_DIR}/qscintilla/PlatQt.cpp)
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
