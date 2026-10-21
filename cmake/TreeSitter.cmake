# tree-sitter runtime + one target per grammar + highlighting queries.

add_library(tree_sitter STATIC ${tree_sitter_SOURCE_DIR}/lib/src/lib.c)
target_include_directories(tree_sitter
    PUBLIC  ${tree_sitter_SOURCE_DIR}/lib/include
    PRIVATE ${tree_sitter_SOURCE_DIR}/lib/src
            ${tree_sitter_SOURCE_DIR}/lib/src/wasm)
target_compile_definitions(tree_sitter PRIVATE _POSIX_C_SOURCE=200112L _DEFAULT_SOURCE)
# Set explicitly: libgit2 puts CMAKE_C_STANDARD=90 in the cache, which
# would otherwise apply to every C target of the project.
set_target_properties(tree_sitter PROPERTIES C_STANDARD 11 AUTOMOC OFF)

# A grammar = parser.c (+ optional scanner.c) in a src/ folder.
function(add_tree_sitter_grammar target grammar_src_dir)
    set(sources ${grammar_src_dir}/parser.c)
    if(EXISTS ${grammar_src_dir}/scanner.c)
        list(APPEND sources ${grammar_src_dir}/scanner.c)
    endif()
    add_library(${target} STATIC ${sources})
    target_include_directories(${target} PRIVATE ${grammar_src_dir})
    set_target_properties(${target} PROPERTIES C_STANDARD 11 AUTOMOC OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /w)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

add_tree_sitter_grammar(tree_sitter_c          ${tree_sitter_c_SOURCE_DIR}/src)
add_tree_sitter_grammar(tree_sitter_cpp        ${tree_sitter_cpp_SOURCE_DIR}/src)
add_tree_sitter_grammar(tree_sitter_java       ${tree_sitter_java_SOURCE_DIR}/src)
add_tree_sitter_grammar(tree_sitter_javascript ${tree_sitter_javascript_SOURCE_DIR}/src)
add_tree_sitter_grammar(tree_sitter_typescript ${tree_sitter_typescript_SOURCE_DIR}/typescript/src)
add_tree_sitter_grammar(tree_sitter_tsx        ${tree_sitter_typescript_SOURCE_DIR}/tsx/src)
# For the occurrence index only: Python's highlighting stays Lexilla's.
add_tree_sitter_grammar(tree_sitter_python     ${tree_sitter_python_SOURCE_DIR}/src)

add_library(tree_sitter_grammars INTERFACE)
target_link_libraries(tree_sitter_grammars INTERFACE
    tree_sitter
    tree_sitter_c tree_sitter_cpp tree_sitter_java
    tree_sitter_javascript tree_sitter_typescript tree_sitter_tsx tree_sitter_python)

# The grammars' highlights.scm queries and the Outline panel's outline.scm
# queries (written for Duet, in src/syntax/queries), exposed to the application as
# Qt resources (`:/queries/<grammar>/<file>`) via duet_add_queries().
set(TREE_SITTER_QUERY_FILES
    "c/highlights.scm|${tree_sitter_c_SOURCE_DIR}/queries/highlights.scm"
    "cpp/highlights.scm|${tree_sitter_cpp_SOURCE_DIR}/queries/highlights.scm"
    "java/highlights.scm|${tree_sitter_java_SOURCE_DIR}/queries/highlights.scm"
    "javascript/highlights.scm|${tree_sitter_javascript_SOURCE_DIR}/queries/highlights.scm"
    "javascript/highlights-jsx.scm|${tree_sitter_javascript_SOURCE_DIR}/queries/highlights-jsx.scm"
    "javascript/highlights-params.scm|${tree_sitter_javascript_SOURCE_DIR}/queries/highlights-params.scm"
    "typescript/highlights.scm|${tree_sitter_typescript_SOURCE_DIR}/queries/highlights.scm")
foreach(grammar IN ITEMS c cpp java javascript typescript)
    list(APPEND TREE_SITTER_QUERY_FILES
        "${grammar}/outline.scm|${PROJECT_SOURCE_DIR}/src/syntax/queries/${grammar}/outline.scm")
endforeach()
# The occurrence index's queries (src/index/queries).
foreach(grammar IN ITEMS c cpp java javascript typescript python)
    list(APPEND TREE_SITTER_QUERY_FILES
        "${grammar}/occurrences.scm|${PROJECT_SOURCE_DIR}/src/index/queries/${grammar}/occurrences.scm")
endforeach()
list(APPEND TREE_SITTER_QUERY_FILES
    "javascript/occurrences-params.scm|${PROJECT_SOURCE_DIR}/src/index/queries/javascript/occurrences-params.scm")

function(duet_add_queries target)
    set(query_dir ${CMAKE_BINARY_DIR}/queries)
    set(resource_files "")
    foreach(entry IN LISTS TREE_SITTER_QUERY_FILES)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 alias)
        list(GET parts 1 source)
        configure_file(${source} ${query_dir}/${alias} COPYONLY)
        list(APPEND resource_files ${query_dir}/${alias})
    endforeach()
    qt_add_resources(${target} tree_sitter_queries
        PREFIX /queries
        BASE ${query_dir}
        FILES ${resource_files})
endfunction()
