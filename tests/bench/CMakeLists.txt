add_executable(neomifes_util_bench
    util_version_bench.cpp
)

target_link_libraries(neomifes_util_bench
    PRIVATE
        neomifes::compile_options
        neomifes::util
        benchmark::benchmark
        benchmark::benchmark_main
)

set_target_properties(neomifes_util_bench PROPERTIES
    FOLDER "tests"
)

add_executable(neomifes_document_bench
    document_load_bench.cpp
    document_piece_table_bench.cpp
)

target_link_libraries(neomifes_document_bench
    PRIVATE
        neomifes::compile_options
        neomifes::document
        benchmark::benchmark
        benchmark::benchmark_main
)

set_target_properties(neomifes_document_bench PROPERTIES
    FOLDER "tests"
)

add_executable(neomifes_render_bench
    render_text_layout_cache_bench.cpp
)

target_link_libraries(neomifes_render_bench
    PRIVATE
        neomifes::compile_options
        neomifes::render
        benchmark::benchmark
        benchmark::benchmark_main
)

set_target_properties(neomifes_render_bench PROPERTIES
    FOLDER "tests"
)

add_executable(neomifes_core_bench
    core_undo_stack_bench.cpp
)

target_link_libraries(neomifes_core_bench
    PRIVATE
        neomifes::compile_options
        neomifes::core
        neomifes::document
        benchmark::benchmark
        benchmark::benchmark_main
)

set_target_properties(neomifes_core_bench PROPERTIES
    FOLDER "tests"
)

add_executable(neomifes_search_bench
    search_find_all_bench.cpp
)

target_link_libraries(neomifes_search_bench
    PRIVATE
        neomifes::compile_options
        neomifes::search
        neomifes::document
        benchmark::benchmark
        benchmark::benchmark_main
)

set_target_properties(neomifes_search_bench PROPERTIES
    FOLDER "tests"
)

add_executable(neomifes_syntax_bench
    syntax_parse_bench.cpp
)

target_link_libraries(neomifes_syntax_bench
    PRIVATE
        neomifes::compile_options
        neomifes::syntax
        benchmark::benchmark
        benchmark::benchmark_main
)

set_target_properties(neomifes_syntax_bench PROPERTIES
    FOLDER "tests"
)
