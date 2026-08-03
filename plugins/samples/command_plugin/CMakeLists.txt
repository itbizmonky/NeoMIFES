# command_plugin - Phase 8f test fixture, proves NeoMifesCoreApi::
# registerCommand round-trips through the DLL boundary and its deferred
# callback correctly reaches ctx->coreApi. Same MODULE-target shape as
# toast_plugin/CMakeLists.txt (Phase 8e).
add_library(command_plugin MODULE
    command_plugin.cpp
)

target_link_libraries(command_plugin
    PRIVATE
        neomifes::compile_options
        neomifes::plugin_sdk
)

set_target_properties(command_plugin PROPERTIES
    FOLDER "plugins/samples"
)
