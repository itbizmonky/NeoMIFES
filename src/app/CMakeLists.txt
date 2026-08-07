# Win32-independent input-translation logic (Phase 4b1) - split out as its
# own library (rather than compiled straight into the NeoMIFES exe) so
# tests/unit/ can link and exercise it headlessly, matching src/core/'s
# testing approach (no Win32 message simulation harness exists here).
add_library(neomifes_app_input STATIC
    editor_input.cpp
    document_open.cpp
    editor_session.cpp
    plugin_core_api_bridge.cpp
    workspace.cpp
)
add_library(neomifes::app_input ALIAS neomifes_app_input)

target_include_directories(neomifes_app_input
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_app_input
    PUBLIC
        neomifes::core
        neomifes::document
        # grep_query_builder.h/grep_result_formatting.h (Phase 5c3) expose
        # search::GrepQuery/GrepMatch in their public signatures - same
        # reasoning as core/document above (document_open.h).
        neomifes::search
        # outline_bridge.h (Phase 7g) exposes syntax::OutlineNode in its
        # public signature - same reasoning as neomifes::search above.
        neomifes::syntax
        # plugin_core_api_bridge.h (Phase 8b) exposes NeoMifesCoreApi/
        # NeoMifesDocument (plugin_sdk.h) in its public signature - same
        # reasoning as neomifes::search/neomifes::syntax above.
        neomifes::plugin_sdk
        # plugin_core_api_bridge.h (Phase 8e) exposes ui::ToastState in its
        # public signature (toNeoMifesToastSink()) - same reasoning as
        # neomifes::plugin_sdk above.
        neomifes::ui
    PRIVATE
        neomifes::compile_options
        neomifes::util
        # plugin_core_api_bridge.cpp (Phase 8f) calls
        # neomifes::plugin::invokePluginCallbackSafe() to run a
        # registerCommand callback - only used inside the .cpp (never in
        # plugin_core_api_bridge.h's public signatures), so PRIVATE, same
        # reasoning as neomifes::util above.
        neomifes::plugin
)

set_target_properties(neomifes_app_input PROPERTIES
    FOLDER "src"
)

add_executable(NeoMIFES WIN32
    main.cpp
    launch_setup.cpp
    normal_mode_wiring.cpp
    startup_profile.cpp
    frame_profile.cpp
    file_dialogs.cpp
    message_dialogs.cpp
)

target_link_libraries(NeoMIFES
    PRIVATE
        neomifes::compile_options
        neomifes::util
        neomifes::platform
        # Explicit even though neomifes::render now links it publicly (ADR-010) -
        # main.cpp calls document::loadFile/constructs document::Document
        # directly, not just through render's transitive dependency.
        neomifes::document
        neomifes::render
        neomifes::ui
        neomifes::app_input
        # Find bar UI (Phase 5b3a) - the first real app-level consumer of
        # SearchService::findAll(); previously only tests/bench linked this.
        neomifes::search
        user32
        shell32
        # file_dialogs.cpp (WI-02): CoInitializeEx/CoCreateInstance/
        # CoUninitialize/CoTaskMemFree - this repo's first CoCreateInstance-
        # based COM usage (existing D2D/DXGI/D3D11 COM, ADR-008, is all
        # factory-function-based and never needed ole32 directly).
        ole32
)

set_target_properties(NeoMIFES PROPERTIES
    FOLDER "src"
    OUTPUT_NAME "NeoMIFES"
)
