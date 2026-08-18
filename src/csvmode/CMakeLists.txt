add_library(neomifes_csvmode STATIC
    src/csv_model.cpp
    src/csv_delimiter_detection.cpp
    src/csv_model_worker.cpp
)
add_library(neomifes::csvmode ALIAS neomifes_csvmode)

target_include_directories(neomifes_csvmode
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_csvmode
    PUBLIC
        # csv_model.h exposes document::TextPos in CsvCell's public API and
        # takes a document::Document&/BufferSnapshot& in CsvModel::build(),
        # same reasoning as neomifes::logmode/neomifes::jsontree.
        neomifes::document
    PRIVATE
        neomifes::compile_options
        # WI-16a: unlike neomifes::logmode (re2::re2) and neomifes::jsontree
        # (nlohmann_json::nlohmann_json), this module's hand-rolled RFC4180
        # state machine parses document::Document's native UTF-16 text
        # directly - no external parsing library and no UTF-8 conversion
        # (neomifes::util/neomifes::encoding) is needed.
)

set_target_properties(neomifes_csvmode PROPERTIES
    FOLDER "src"
)
