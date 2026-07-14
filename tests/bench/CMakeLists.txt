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
