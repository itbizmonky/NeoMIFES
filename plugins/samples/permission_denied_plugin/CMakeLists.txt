# permission_denied_plugin - Phase 8d test fixture, proves the permission
# gate (NEOMIFES_PLUGIN_PERMISSION_DOCUMENT) actually denies CoreApi access
# via NULL function pointers, and that PluginHost's existing SEH trampoline
# isolates the resulting crash (see
# tests/integration/plugin_document_editing_test.cpp). Same MODULE-target
# shape as document_editing_plugin/CMakeLists.txt (Phase 8b).
add_library(permission_denied_plugin MODULE
    permission_denied_plugin.cpp
)

target_link_libraries(permission_denied_plugin
    PRIVATE
        neomifes::compile_options
        neomifes::plugin_sdk
)

set_target_properties(permission_denied_plugin PROPERTIES
    FOLDER "plugins/samples"
)
