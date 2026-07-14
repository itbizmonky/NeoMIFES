add_library(neomifes_document STATIC
    src/add_buffer.cpp
    src/buffer_snapshot.cpp
    src/document.cpp
    src/file_loader.cpp
    src/line_index.cpp
    src/original_buffer.cpp
    src/piece_table.cpp
)
add_library(neomifes::document ALIAS neomifes_document)

target_include_directories(neomifes_document
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_document
    PUBLIC
        # Public headers expose nothing platform-specific; keep this minimal.
    PRIVATE
        neomifes::compile_options
        neomifes::util
)

set_target_properties(neomifes_document PROPERTIES
    FOLDER "src"
)
