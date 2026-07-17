add_library(neomifes_search STATIC
    src/search_service.cpp
)
add_library(neomifes::search ALIAS neomifes_search)

target_include_directories(neomifes_search
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_search
    PUBLIC
        # search_service.h exposes document::TextRange/document::Document in
        # its public API (Match/findAll), same reasoning as neomifes::core.
        neomifes::document
    PRIVATE
        neomifes::compile_options
        # utf8_convert.h (UTF-16 -> UTF-8 + offset table for RE2)
        neomifes::util
        # RE2 (ADR-002) - an implementation detail, not exposed in
        # search_service.h, so PRIVATE.
        re2::re2
)

set_target_properties(neomifes_search PROPERTIES
    FOLDER "src"
)
