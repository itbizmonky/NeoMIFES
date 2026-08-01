# document_editing_plugin - Phase 8b sample plugin, proves NeoMifesCoreApi's
# insertText() round-trips through the DLL boundary into a real
# document::Document (see tests/integration/plugin_document_editing_test.cpp).
# Same MODULE-target shape as hello_plugin/CMakeLists.txt (Phase 8a) - see
# that file's comment for why MODULE, not SHARED.
add_library(document_editing_plugin MODULE
    document_editing_plugin.cpp
)

target_link_libraries(document_editing_plugin
    PRIVATE
        neomifes::compile_options
        neomifes::plugin_sdk
)

set_target_properties(document_editing_plugin PROPERTIES
    FOLDER "plugins/samples"
)
