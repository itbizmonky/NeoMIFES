# crashing_command_plugin - Phase 8f test fixture, proves the SEH trampoline
# (neomifes::plugin::invokePluginCallbackSafe()) isolates a crash inside a
# registerCommand callback invoked LATER, outside PluginHost::load()'s own
# call stack - a different code path than crashing_plugin (Phase 8a), which
# only exercised a crash during onLoad itself. Same MODULE-target shape.
add_library(crashing_command_plugin MODULE
    crashing_command_plugin.cpp
)

target_link_libraries(crashing_command_plugin
    PRIVATE
        neomifes::compile_options
        neomifes::plugin_sdk
)

set_target_properties(crashing_command_plugin PROPERTIES
    FOLDER "plugins/samples"
)
