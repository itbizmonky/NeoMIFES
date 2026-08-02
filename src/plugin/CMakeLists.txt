add_library(neomifes_plugin STATIC
    src/plugin_error.cpp
    src/plugin_host.cpp
    src/plugin_sandbox.cpp
)
add_library(neomifes::plugin ALIAS neomifes_plugin)

target_include_directories(neomifes_plugin
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_plugin
    PUBLIC
        # plugin_host.h's public API exposes NeoMifesPluginInfo/VTable/Context.
        neomifes::plugin_sdk
        # plugin_host.h's PluginHost class holds a platform::ModuleHandle
        # data member (LoadLibraryW/FreeLibrary RAII) - even though it's
        # private, any translation unit including this public header needs
        # handle_guard.h's include path to see the complete member type, so
        # neomifes::platform's usage requirements must propagate PUBLIC too.
        neomifes::platform
    PRIVATE
        neomifes::compile_options
)

set_target_properties(neomifes_plugin PROPERTIES
    FOLDER "src"
)
