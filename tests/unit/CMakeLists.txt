add_executable(neomifes_unit_tests
    util_version_test.cpp
    util_wchar_cast_test.cpp
    util_utf8_convert_test.cpp
    platform_perf_clock_test.cpp
    platform_process_metrics_test.cpp
    platform_file_mapping_test.cpp
    document_add_buffer_test.cpp
    document_buffer_snapshot_test.cpp
    document_piece_table_test.cpp
    document_piece_tree_test.cpp
    document_line_index_test.cpp
    document_property_test.cpp
    document_file_loader_test.cpp
    document_document_test.cpp
    render_resize_math_test.cpp
    render_error_test.cpp
    render_viewport_math_test.cpp
    render_text_layout_cache_test.cpp
    core_selection_model_test.cpp
    core_bookmark_manager_test.cpp
    core_edit_commands_test.cpp
    core_indentation_conversion_test.cpp
    core_replace_all_command_test.cpp
    core_undo_stack_test.cpp
    core_command_dispatcher_test.cpp
    core_viewport_test.cpp
    app_editor_input_test.cpp
    ui_click_tracking_test.cpp
    ui_find_navigation_test.cpp
    ui_command_palette_filter_test.cpp
    ui_goto_line_parser_test.cpp
    util_fuzzy_matcher_test.cpp
    search_search_service_test.cpp
    search_replacement_test.cpp
)

target_link_libraries(neomifes_unit_tests
    PRIVATE
        neomifes::compile_options
        neomifes::util
        neomifes::platform
        neomifes::document
        neomifes::render
        neomifes::core
        neomifes::search
        neomifes::app_input
        neomifes::ui
        GTest::gtest
        GTest::gtest_main
)

set_target_properties(neomifes_unit_tests PROPERTIES
    FOLDER "tests"
)

gtest_discover_tests(neomifes_unit_tests
    DISCOVERY_TIMEOUT 30
)
