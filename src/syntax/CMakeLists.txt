add_library(neomifes_syntax STATIC
    src/syntax.cpp
    src/outline.cpp
    src/incremental_parser.cpp
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
        tree-sitter-python-grammar
        tree-sitter-c-grammar
        tree-sitter-javascript-grammar
        tree-sitter-java-grammar
        tree-sitter-go-grammar
        tree-sitter-rust-grammar
        tree-sitter-json-grammar
)

set_target_properties(neomifes_syntax PROPERTIES
    FOLDER "src"
)
