# throwing_plugin - Phase 8a test fixture (see the .cpp file's header
# comment). Same shape as hello_plugin/CMakeLists.txt.
add_library(throwing_plugin MODULE
    throwing_plugin.cpp
)

target_link_libraries(throwing_plugin
    PRIVATE
        neomifes::compile_options
        neomifes::plugin_sdk
)

set_target_properties(throwing_plugin PROPERTIES
    FOLDER "plugins/samples"
)
