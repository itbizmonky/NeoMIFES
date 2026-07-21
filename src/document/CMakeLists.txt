add_library(neomifes_document STATIC
    src/add_buffer.cpp
    src/buffer_snapshot.cpp
    src/document.cpp
    src/file_loader.cpp
    src/line_index.cpp
    src/original_buffer.cpp
    src/piece_table.cpp
    src/piece_tree.cpp
)
add_library(neomifes::document ALIAS neomifes_document)

target_include_directories(neomifes_document
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_document
    PUBLIC
        # original_buffer.h includes neomifes/platform/file_mapping.h (mmap RAII),
        # so platform is a public (transitive) dependency of this target.
        neomifes::platform
        # original_buffer.h/file_loader.h include neomifes/encoding/encoding.h
        # (Phase 6d: Encoding-tagged mmap decode, LoadResult::detectedEncoding),
        # so encoding is likewise a public (transitive) dependency.
        neomifes::encoding
    PRIVATE
        neomifes::compile_options
        neomifes::util
)

set_target_properties(neomifes_document PROPERTIES
    FOLDER "src"
)
