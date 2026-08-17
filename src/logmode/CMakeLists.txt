add_library(neomifes_logmode STATIC
    src/log_pattern.cpp
    src/timestamp_parser.cpp
    src/log_model.cpp
    src/format_detection.cpp
    src/log_index_worker.cpp
    src/log_navigation.cpp
    src/log_grouping.cpp
    src/log_pattern_file.cpp
    src/json_string_convert.cpp
)
add_library(neomifes::logmode ALIAS neomifes_logmode)

target_include_directories(neomifes_logmode
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_logmode
    PUBLIC
        # log_model.h exposes document::Document/document::LineNumber in its
        # public API (LogModel::build()), same reasoning as neomifes::search.
        neomifes::document
    PRIVATE
        neomifes::compile_options
        # utf8_convert.h (UTF-16 -> UTF-8 + offset table for RE2)
        neomifes::util
        # RE2 (ADR-002) - an implementation detail, not exposed in
        # log_model.h, so PRIVATE (mirrors neomifes::search's own re2::re2).
        re2::re2
        # WI-14d: log_pattern_file.cpp/json_string_convert.cpp - JSON
        # parsing (ADR-013) and UTF-16<->UTF-8 conversion (neomifes::encoding)
        # for user-editable pattern files. Neither type appears in
        # log_pattern_file.h's public signature, so PRIVATE - mirrors
        # neomifes::core's identical PRIVATE pair for the same reason
        # (src/core/CMakeLists.txt).
        nlohmann_json::nlohmann_json
        neomifes::encoding
)

set_target_properties(neomifes_logmode PROPERTIES
    FOLDER "src"
)
