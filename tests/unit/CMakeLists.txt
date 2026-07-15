add_executable(neomifes_unit_tests
    util_version_test.cpp
    util_wchar_cast_test.cpp
    platform_perf_clock_test.cpp
    platform_process_metrics_test.cpp
    document_add_buffer_test.cpp
    document_buffer_snapshot_test.cpp
    document_piece_table_test.cpp
    document_piece_tree_test.cpp
    document_line_index_test.cpp
    document_property_test.cpp
    document_file_loader_test.cpp
)

target_link_libraries(neomifes_unit_tests
    PRIVATE
        neomifes::compile_options
        neomifes::util
        neomifes::platform
        neomifes::document
        GTest::gtest
        GTest::gtest_main
)

set_target_properties(neomifes_unit_tests PROPERTIES
    FOLDER "tests"
)

gtest_discover_tests(neomifes_unit_tests
    DISCOVERY_TIMEOUT 30
)
