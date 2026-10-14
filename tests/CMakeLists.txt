# Un exécutable Qt Test par fichier tst_*.cpp. Les tests qui créent des
# widgets tournent sans affichage (plateforme Qt « offscreen »).
function(enlumine_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE enlumine_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

enlumine_add_test(tst_syntax)
enlumine_add_test(tst_outline)
enlumine_add_test(tst_changes)
enlumine_add_test(tst_textfile)
enlumine_add_test(tst_imageview)
enlumine_add_test(tst_terminal)
enlumine_add_test(tst_claude)
target_compile_definitions(tst_claude PRIVATE ENLUMINE_HOOK_PATH="$<TARGET_FILE:enlumine-hook>")
add_dependencies(tst_claude enlumine-hook)
