add_library(neomifes_encoding STATIC
    src/encoding.cpp
)
add_library(neomifes::encoding ALIAS neomifes_encoding)

target_include_directories(neomifes_encoding
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_encoding
    PRIVATE
        neomifes::compile_options
        neomifes::platform
)

set_target_properties(neomifes_encoding PROPERTIES
    FOLDER "src"
)
