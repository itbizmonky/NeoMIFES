add_library(neomifes_xmltree STATIC
    src/xml_tree.cpp
    src/xml_tree_worker.cpp
    src/xpath.cpp
)
add_library(neomifes::xmltree ALIAS neomifes_xmltree)

target_include_directories(neomifes_xmltree
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_xmltree
    PUBLIC
        # xml_tree.h exposes document::TextPos in XmlNode's public API and
        # takes a document::Document& in parseXmlTree(), same reasoning as
        # neomifes::jsontree's json_tree.h.
        neomifes::document
    PRIVATE
        neomifes::compile_options
        # WI-15f: tree-sitter-xml (already vendored since Phase 7r for
        # syntax highlighting, ADR-014) - neither tree-sitter type appears
        # in xml_tree.h's public signature, so PRIVATE. neomifes_syntax is
        # the only other module linking these directly.
        tree-sitter
        tree-sitter-xml-grammar
)

set_target_properties(neomifes_xmltree PROPERTIES
    FOLDER "src"
)
