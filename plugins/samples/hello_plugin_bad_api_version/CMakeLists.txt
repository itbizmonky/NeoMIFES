# hello_plugin_bad_api_version - Phase 8a test fixture (see the .cpp file's
# header comment). Same shape as hello_plugin/CMakeLists.txt.
add_library(hello_plugin_bad_api_version MODULE
    hello_plugin_bad_api_version.cpp
)

target_link_libraries(hello_plugin_bad_api_version
    PRIVATE
        neomifes::compile_options
        neomifes::plugin_sdk
)

set_target_properties(hello_plugin_bad_api_version PROPERTIES
    FOLDER "plugins/samples"
)
