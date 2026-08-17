add_library(neomifes_logmode STATIC
    src/log_pattern.cpp
    src/timestamp_parser.cpp
    src/log_model.cpp
    src/format_detection.cpp
    src/log_index_worker.cpp
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
)

set_target_properties(neomifes_logmode PROPERTIES
    FOLDER "src"
)
