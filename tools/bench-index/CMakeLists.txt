# Measures the occurrence index on a folder (see main.cpp); built on
# demand: cmake --build build --target bench-index
add_executable(bench-index EXCLUDE_FROM_ALL main.cpp)
duet_enable_warnings(bench-index)
target_link_libraries(bench-index PRIVATE duet_core)
