# hello_plugin - Phase 8a sample plugin, this repo's first SHARED/MODULE
# CMake target. MODULE (not SHARED): nothing here ever
# target_link_libraries()'s against hello_plugin - it is loaded exclusively
# at runtime via PluginHost::load() (LoadLibraryW), which is exactly what
# MODULE means. On Windows/MSVC this produces the same .dll artifact as
# SHARED would (the distinction matters on ELF/dlopen platforms, not here),
# but MODULE communicates intent to future readers.
add_library(hello_plugin MODULE
    hello_plugin.cpp
)

target_link_libraries(hello_plugin
    PRIVATE
        neomifes::compile_options
        neomifes::plugin_sdk
)

set_target_properties(hello_plugin PROPERTIES
    FOLDER "plugins/samples"
)
