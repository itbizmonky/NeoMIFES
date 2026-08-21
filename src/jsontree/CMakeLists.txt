add_library(neomifes_jsontree STATIC
    src/json_tree.cpp
    src/json_format.cpp
    src/json_string_convert.cpp
    src/json_tree_worker.cpp
)
add_library(neomifes::jsontree ALIAS neomifes_jsontree)

target_include_directories(neomifes_jsontree
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_jsontree
    PUBLIC
        # json_tree.h exposes document::TextPos in JsonNode's public API and
        # takes a document::Document& in parseJsonTree(), same reasoning as
        # neomifes::logmode's log_model.h.
        neomifes::document
    PRIVATE
        neomifes::compile_options
        # WI-15a: nlohmann::ordered_json (ADR-013) for structural parsing +
        # key-order preservation; neither type appears in json_tree.h's
        # public signature, so PRIVATE - mirrors neomifes::logmode's
        # identical PRIVATE nlohmann_json::nlohmann_json for the same reason.
        nlohmann_json::nlohmann_json
        # utf8_convert.h (UTF-16 -> UTF-8 + offset table), same role as
        # neomifes::logmode/neomifes::search's use of neomifes::util.
        neomifes::util
        # json_string_convert.cpp's fromUtf8() (decode a single nlohmann-
        # decoded UTF-8 object key back to UTF-16) wraps encoding::decode() -
        # same PRIVATE reasoning as neomifes::logmode's identical pairing.
        neomifes::encoding
)

set_target_properties(neomifes_jsontree PROPERTIES
    FOLDER "src"
)
