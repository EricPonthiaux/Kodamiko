// SPDX-License-Identifier: GPL-3.0-only

#include "LexillaLanguages.h"

#include <QHash>
#include <QString>

#include <SciLexer.h>

namespace {

LexillaLanguage json()
{
    return {.lexer = "json",
            .styleCategories = {{SCE_JSON_NUMBER, "number"},
                                {SCE_JSON_STRING, "string"},
                                {SCE_JSON_STRINGEOL, "string"},
                                {SCE_JSON_PROPERTYNAME, "property"},
                                {SCE_JSON_ESCAPESEQUENCE, "string"},
                                {SCE_JSON_LINECOMMENT, "comment"},
                                {SCE_JSON_BLOCKCOMMENT, "comment"},
                                {SCE_JSON_OPERATOR, "operator"},
                                {SCE_JSON_URI, "string"},
                                {SCE_JSON_COMPACTIRI, "string"},
                                {SCE_JSON_KEYWORD, "keyword"},
                                {SCE_JSON_LDKEYWORD, "keyword"},
                                {SCE_JSON_ERROR, "variable.builtin"}},
            .keywords = {"false true null"}};
}

LexillaLanguage cmake()
{
    return {.lexer = "cmake",
            .styleCategories = {{SCE_CMAKE_COMMENT, "comment"},
                                {SCE_CMAKE_STRINGDQ, "string"},
                                {SCE_CMAKE_STRINGLQ, "string"},
                                {SCE_CMAKE_STRINGRQ, "string"},
                                {SCE_CMAKE_COMMANDS, "function"},
                                {SCE_CMAKE_PARAMETERS, "property"},
                                {SCE_CMAKE_VARIABLE, "property"},
                                {SCE_CMAKE_USERDEFINED, "function"},
                                {SCE_CMAKE_WHILEDEF, "keyword"},
                                {SCE_CMAKE_FOREACHDEF, "keyword"},
                                {SCE_CMAKE_IFDEFINEDEF, "keyword"},
                                {SCE_CMAKE_MACRODEF, "keyword"},
                                {SCE_CMAKE_STRINGVAR, "property"},
                                {SCE_CMAKE_NUMBER, "number"}},
            .keywords = {"add_compile_definitions add_compile_options add_custom_command add_custom_target "
                         "add_definitions add_dependencies add_executable add_library add_link_options "
                         "add_subdirectory add_test cmake_minimum_required cmake_parse_arguments cmake_path "
                         "cmake_policy configure_file enable_language enable_testing execute_process export "
                         "file find_file find_library find_package find_path find_program get_filename_component "
                         "get_property get_target_property include include_directories install link_directories "
                         "link_libraries list mark_as_advanced message option project return set "
                         "set_directory_properties set_property set_source_files_properties set_target_properties "
                         "set_tests_properties source_group string target_compile_definitions "
                         "target_compile_features target_compile_options target_include_directories "
                         "target_link_directories target_link_libraries target_link_options target_sources "
                         "try_compile try_run unset FetchContent_Declare FetchContent_MakeAvailable"}};
}

LexillaLanguage markdown()
{
    return {.lexer = "markdown",
            .styleCategories = {{SCE_MARKDOWN_STRONG1, "function"},    {SCE_MARKDOWN_STRONG2, "function"},
                                {SCE_MARKDOWN_EM1, "function"},        {SCE_MARKDOWN_EM2, "function"},
                                {SCE_MARKDOWN_HEADER1, "heading"},     {SCE_MARKDOWN_HEADER2, "heading"},
                                {SCE_MARKDOWN_HEADER3, "heading"},     {SCE_MARKDOWN_HEADER4, "heading"},
                                {SCE_MARKDOWN_HEADER5, "heading"},     {SCE_MARKDOWN_HEADER6, "heading"},
                                {SCE_MARKDOWN_ULIST_ITEM, "operator"}, {SCE_MARKDOWN_OLIST_ITEM, "operator"},
                                {SCE_MARKDOWN_BLOCKQUOTE, "comment"},  {SCE_MARKDOWN_HRULE, "operator"},
                                {SCE_MARKDOWN_LINK, "function"},       {SCE_MARKDOWN_CODE, "string"},
                                {SCE_MARKDOWN_CODE2, "string"},        {SCE_MARKDOWN_CODEBK, "string"},
                                {SCE_MARKDOWN_FRONT_MARK, "comment"},  {SCE_MARKDOWN_FRONT, "comment"},
                                {SCE_MARKDOWN_FRONT_KEY, "property"}}};
}

// HTML and XML: without word lists, every tag and attribute is an
// "unknown" one, highlighted the same.
std::vector<std::pair<int, const char *>> markupStyles()
{
    return {{SCE_H_TAG, "tag"},
            {SCE_H_TAGUNKNOWN, "tag"},
            {SCE_H_TAGEND, "tag"},
            {SCE_H_ATTRIBUTE, "attribute"},
            {SCE_H_ATTRIBUTEUNKNOWN, "attribute"},
            {SCE_H_NUMBER, "number"},
            {SCE_H_DOUBLESTRING, "string"},
            {SCE_H_SINGLESTRING, "string"},
            {SCE_H_COMMENT, "comment"},
            {SCE_H_ENTITY, "tag"},
            {SCE_H_XMLSTART, "keyword"},
            {SCE_H_XMLEND, "keyword"},
            {SCE_H_CDATA, "string"},
            {SCE_H_VALUE, "string"},
            {SCE_H_SGML_DEFAULT, "keyword"},
            {SCE_H_SGML_COMMAND, "keyword"},
            {SCE_H_SGML_COMMENT, "comment"}};
}

LexillaLanguage css()
{
    return {.lexer = "css",
            .styleCategories = {{SCE_CSS_TAG, "tag"},
                                {SCE_CSS_CLASS, "type"},
                                {SCE_CSS_PSEUDOCLASS, "attribute"},
                                {SCE_CSS_UNKNOWN_PSEUDOCLASS, "attribute"},
                                {SCE_CSS_OPERATOR, "operator"},
                                {SCE_CSS_IDENTIFIER, "property"},
                                {SCE_CSS_UNKNOWN_IDENTIFIER, "property"},
                                {SCE_CSS_IDENTIFIER2, "property"},
                                {SCE_CSS_IDENTIFIER3, "property"},
                                {SCE_CSS_EXTENDED_IDENTIFIER, "property"},
                                {SCE_CSS_VALUE, "string"},
                                {SCE_CSS_COMMENT, "comment"},
                                {SCE_CSS_ID, "type"},
                                {SCE_CSS_IMPORTANT, "keyword"},
                                {SCE_CSS_DIRECTIVE, "keyword"},
                                {SCE_CSS_GROUP_RULE, "keyword"},
                                {SCE_CSS_DOUBLESTRING, "string"},
                                {SCE_CSS_SINGLESTRING, "string"},
                                {SCE_CSS_ATTRIBUTE, "attribute"},
                                {SCE_CSS_PSEUDOELEMENT, "attribute"},
                                {SCE_CSS_EXTENDED_PSEUDOCLASS, "attribute"},
                                {SCE_CSS_EXTENDED_PSEUDOELEMENT, "attribute"},
                                {SCE_CSS_VARIABLE, "property"}}};
}

LexillaLanguage bash()
{
    return {.lexer = "bash",
            .styleCategories = {{SCE_SH_ERROR, "variable.builtin"},
                                {SCE_SH_COMMENTLINE, "comment"},
                                {SCE_SH_NUMBER, "number"},
                                {SCE_SH_WORD, "keyword"},
                                {SCE_SH_STRING, "string"},
                                {SCE_SH_CHARACTER, "string"},
                                {SCE_SH_OPERATOR, "operator"},
                                {SCE_SH_SCALAR, "property"},
                                {SCE_SH_PARAM, "property"},
                                {SCE_SH_BACKTICKS, "string"},
                                {SCE_SH_HERE_DELIM, "string"},
                                {SCE_SH_HERE_Q, "string"}},
            .keywords = {"alias break case cd continue declare do done echo elif else esac eval exec exit export "
                         "false fi for function getopts if in local printf read readonly return select set shift "
                         "source test then trap true typeset unset until wait while"}};
}

LexillaLanguage yaml()
{
    return {.lexer = "yaml",
            .styleCategories = {{SCE_YAML_COMMENT, "comment"},
                                {SCE_YAML_IDENTIFIER, "property"},
                                {SCE_YAML_KEYWORD, "keyword"},
                                {SCE_YAML_NUMBER, "number"},
                                {SCE_YAML_REFERENCE, "type"},
                                {SCE_YAML_DOCUMENT, "heading"},
                                {SCE_YAML_TEXT, "string"},
                                {SCE_YAML_ERROR, "variable.builtin"},
                                {SCE_YAML_OPERATOR, "operator"}},
            .keywords = {"true false yes no on off null"}};
}

LexillaLanguage makefile()
{
    return {.lexer = "makefile",
            .styleCategories = {{SCE_MAKE_COMMENT, "comment"},
                                {SCE_MAKE_PREPROCESSOR, "keyword"},
                                {SCE_MAKE_IDENTIFIER, "property"},
                                {SCE_MAKE_OPERATOR, "operator"},
                                {SCE_MAKE_TARGET, "function"},
                                {SCE_MAKE_IDEOL, "variable.builtin"}}};
}

LexillaLanguage properties()
{
    return {.lexer = "props",
            .styleCategories = {{SCE_PROPS_COMMENT, "comment"},
                                {SCE_PROPS_SECTION, "heading"},
                                {SCE_PROPS_ASSIGNMENT, "operator"},
                                {SCE_PROPS_DEFVAL, "string"},
                                {SCE_PROPS_KEY, "property"}}};
}

LexillaLanguage diff()
{
    return {.lexer = "diff",
            .styleCategories = {{SCE_DIFF_COMMENT, "comment"},
                                {SCE_DIFF_COMMAND, "keyword"},
                                {SCE_DIFF_HEADER, "heading"},
                                {SCE_DIFF_POSITION, "heading"},
                                {SCE_DIFF_DELETED, "diff.minus"},
                                {SCE_DIFF_ADDED, "diff.plus"},
                                {SCE_DIFF_CHANGED, "keyword"},
                                {SCE_DIFF_PATCH_ADD, "diff.plus"},
                                {SCE_DIFF_PATCH_DELETE, "diff.minus"},
                                {SCE_DIFF_REMOVED_PATCH_ADD, "diff.minus"},
                                {SCE_DIFF_REMOVED_PATCH_DELETE, "diff.minus"}}};
}

LexillaLanguage batch()
{
    return {.lexer = "batch",
            .styleCategories = {{SCE_BAT_COMMENT, "comment"},
                                {SCE_BAT_WORD, "keyword"},
                                {SCE_BAT_LABEL, "function"},
                                {SCE_BAT_HIDE, "operator"},
                                {SCE_BAT_COMMAND, "function"},
                                {SCE_BAT_IDENTIFIER, "property"},
                                {SCE_BAT_OPERATOR, "operator"}},
            .keywords = {"assoc break call cd chcp chdir cls color copy date defined del dir do echo else endlocal "
                         "erase errorlevel exist exit for ftype goto if in md mkdir mklink move not path pause "
                         "popd prompt pushd rd rem ren rename rmdir set setlocal shift start time title type ver "
                         "verify vol"}};
}

LexillaLanguage sql()
{
    return {.lexer = "sql",
            .styleCategories = {{SCE_SQL_COMMENT, "comment"},
                                {SCE_SQL_COMMENTLINE, "comment"},
                                {SCE_SQL_COMMENTDOC, "comment"},
                                {SCE_SQL_COMMENTLINEDOC, "comment"},
                                {SCE_SQL_NUMBER, "number"},
                                {SCE_SQL_WORD, "keyword"},
                                {SCE_SQL_WORD2, "function"},
                                {SCE_SQL_STRING, "string"},
                                {SCE_SQL_CHARACTER, "string"},
                                {SCE_SQL_OPERATOR, "operator"},
                                {SCE_SQL_QUOTEDIDENTIFIER, "property"}},
            .keywords = {"add all alter and as asc begin between by case cast check commit constraint create cross "
                         "database default delete desc distinct drop else end exists foreign from full grant group "
                         "having if in index inner insert into is join key left like limit not null offset on or "
                         "order outer primary references replace returning revoke right rollback schema select set "
                         "table then transaction trigger union unique update values view when where with",
                         "avg coalesce count max min sum"}};
}

QHash<QString, LexillaLanguage> makeLanguages()
{
    return {
        {QStringLiteral("json"), json()},
        {QStringLiteral("cmake"), cmake()},
        {QStringLiteral("markdown"), markdown()},
        {QStringLiteral("xml"), {.lexer = "xml", .styleCategories = markupStyles()}},
        {QStringLiteral("html"), {.lexer = "hypertext", .styleCategories = markupStyles()}},
        {QStringLiteral("css"), css()},
        {QStringLiteral("bash"), bash()},
        {QStringLiteral("yaml"), yaml()},
        {QStringLiteral("makefile"), makefile()},
        {QStringLiteral("properties"), properties()},
        {QStringLiteral("diff"), diff()},
        {QStringLiteral("batch"), batch()},
        {QStringLiteral("sql"), sql()},
    };
}

} // namespace

namespace LexillaLanguages {

const LexillaLanguage *forLanguage(QStringView languageId)
{
    static const QHash<QString, LexillaLanguage> languages = makeLanguages();
    const auto found = languages.constFind(languageId.toString());
    return found == languages.constEnd() ? nullptr : &found.value();
}

} // namespace LexillaLanguages
