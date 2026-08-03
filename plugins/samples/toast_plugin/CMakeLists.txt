# toast_plugin - Phase 8e test fixture, proves NeoMifesCoreApi::showToast
# round-trips through the DLL boundary into a real ui::ToastState, and that
# it works without NEOMIFES_PLUGIN_PERMISSION_DOCUMENT (showToast is never
# permission-gated). Same MODULE-target shape as
# permission_denied_plugin/CMakeLists.txt (Phase 8d).
add_library(toast_plugin MODULE
    toast_plugin.cpp
)

target_link_libraries(toast_plugin
    PRIVATE
        neomifes::compile_options
        neomifes::plugin_sdk
)

set_target_properties(toast_plugin PROPERTIES
    FOLDER "plugins/samples"
)
