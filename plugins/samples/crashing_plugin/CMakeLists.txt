# crashing_plugin - Phase 8a test fixture (see the .cpp file's header
# comment). Same shape as hello_plugin/CMakeLists.txt.
add_library(crashing_plugin MODULE
    crashing_plugin.cpp
)

target_link_libraries(crashing_plugin
    PRIVATE
        neomifes::compile_options
        neomifes::plugin_sdk
)

set_target_properties(crashing_plugin PROPERTIES
    FOLDER "plugins/samples"
)
