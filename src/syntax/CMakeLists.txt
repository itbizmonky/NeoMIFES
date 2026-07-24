add_library(neomifes_syntax STATIC
    src/syntax.cpp
)
add_library(neomifes::syntax ALIAS neomifes_syntax)

target_include_directories(neomifes_syntax
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_syntax
    PUBLIC
        neomifes::document
    PRIVATE
        neomifes::compile_options
        tree-sitter
        tree-sitter-cpp-grammar
)

set_target_properties(neomifes_syntax PROPERTIES
    FOLDER "src"
)
