# Static `scintilla` (the editing component and its Qt widget,
# ScintillaEditBase) and `lexilla` (the lexers) targets, adapted from
# qt/ScintillaEditBase/ScintillaEditBase.pro and src/Lexilla.pro: both only
# ship qmake or make projects.

set(SCI_ROOT ${scintilla_SOURCE_DIR})
set(SCI_QT ${SCI_ROOT}/qt/ScintillaEditBase)
set(LEXILLA_ROOT ${lexilla_SOURCE_DIR})

# PlatQt.cpp builds each style's QFont from its family name only, so the
# font's OpenType features cannot be chosen. Our copy reads a "#liga"
# suffix on the family: it turns the programming ligatures on, and they are
# off otherwise (see ScintillaStyles::applyDefaultStyle). It also keeps the
# style's weight (Light, Medium…), which PlatQt.cpp rounds to bold or not.
file(READ ${SCI_QT}/PlatQt.cpp SCI_PLATQT)
set(SCI_WEIGHT_LINE "		pfont->setBold(static_cast<int>(fp.weight) > 500);")
string(FIND "${SCI_PLATQT}" "${SCI_WEIGHT_LINE}" SCI_WEIGHT_AT)
if(SCI_WEIGHT_AT EQUAL -1)
    message(FATAL_ERROR "Scintilla's PlatQt.cpp changed: update the weight patch in cmake/Scintilla.cmake")
endif()
string(REPLACE "${SCI_WEIGHT_LINE}" "		pfont->setWeight(static_cast<QFont::Weight>(static_cast<int>(fp.weight)));"
       SCI_PLATQT "${SCI_PLATQT}")
set(SCI_FACE_LINE "		pfont->setFamily(QString::fromUtf8(fp.faceName));")
string(FIND "${SCI_PLATQT}" "${SCI_FACE_LINE}" SCI_FACE_AT)
if(SCI_FACE_AT EQUAL -1)
    message(FATAL_ERROR "Scintilla's PlatQt.cpp changed: update the ligature patch in cmake/Scintilla.cmake")
endif()
string(REPLACE "${SCI_FACE_LINE}" [[
		{
			// Kodamiko: "family#liga" turns the ligatures on.
			QString face = QString::fromUtf8(fp.faceName);
			const bool ligatures = face.endsWith(QLatin1String("#liga"));
			if (ligatures)
				face.chop(5);
			pfont->setFamily(face);
			pfont->setFeature(QFont::Tag("liga"), ligatures ? 1 : 0);
			pfont->setFeature(QFont::Tag("calt"), ligatures ? 1 : 0);
		}]] SCI_PLATQT "${SCI_PLATQT}")
file(WRITE ${CMAKE_BINARY_DIR}/scintilla/PlatQt.cpp.new "${SCI_PLATQT}")
file(COPY_FILE ${CMAKE_BINARY_DIR}/scintilla/PlatQt.cpp.new ${CMAKE_BINARY_DIR}/scintilla/PlatQt.cpp ONLY_IF_DIFFERENT)

file(GLOB SCI_CORE_SOURCES ${SCI_ROOT}/src/*.cxx)

add_library(scintilla STATIC
    ${SCI_QT}/ScintillaEditBase.h
    ${SCI_QT}/ScintillaEditBase.cpp
    ${SCI_QT}/ScintillaQt.h
    ${SCI_QT}/ScintillaQt.cpp
    ${SCI_QT}/PlatQt.h
    ${CMAKE_BINARY_DIR}/scintilla/PlatQt.cpp
    ${SCI_CORE_SOURCES})

# SYSTEM: their warnings are not ours (Platform.h's interfaces have no
# virtual destructor).
target_include_directories(scintilla SYSTEM
    PUBLIC  ${SCI_ROOT}/include
            ${SCI_ROOT}/src
            ${SCI_QT})
# ScintillaQt.cpp includes "PlatQt.h" beside it, our patched copy too.
target_include_directories(scintilla PRIVATE ${SCI_QT})

target_compile_definitions(scintilla PUBLIC SCINTILLA_QT=1)
# QTextCodec, for documents in a legacy code page: we only use UTF-8.
target_link_libraries(scintilla PUBLIC Qt6::Widgets Qt6::Core5Compat)
set_target_properties(scintilla PROPERTIES AUTOMOC ON CXX_STANDARD 17)

file(GLOB LEXILLA_SOURCES
    ${LEXILLA_ROOT}/src/Lexilla.cxx
    ${LEXILLA_ROOT}/lexlib/*.cxx
    ${LEXILLA_ROOT}/lexers/*.cxx)

add_library(lexilla STATIC ${LEXILLA_SOURCES})
target_include_directories(lexilla SYSTEM
    PUBLIC  ${LEXILLA_ROOT}/include
    PRIVATE ${LEXILLA_ROOT}/lexlib
            ${SCI_ROOT}/include)
set_target_properties(lexilla PROPERTIES CXX_STANDARD 17)

# Third-party code: we do not want its warnings in our build.
foreach(target scintilla lexilla)
    if(MSVC)
        target_compile_options(${target} PRIVATE /w)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endforeach()
