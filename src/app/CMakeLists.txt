# Win32-independent input-translation logic (Phase 4b1) - split out as its
# own library (rather than compiled straight into the NeoMIFES exe) so
# tests/unit/ can link and exercise it headlessly, matching src/core/'s
# testing approach (no Win32 message simulation harness exists here).
add_library(neomifes_app_input STATIC
    editor_input.cpp
    document_open.cpp
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
    PRIVATE
        neomifes::compile_options
        neomifes::util
)

set_target_properties(neomifes_app_input PROPERTIES
    FOLDER "src"
)

add_executable(NeoMIFES WIN32
    main.cpp
    startup_profile.cpp
    frame_profile.cpp
)

target_link_libraries(NeoMIFES
    PRIVATE
        neomifes::compile_options
        neomifes::util
        neomifes::platform
        # Explicit even though neomifes::render now links it publicly (ADR-010) -
        # main.cpp calls document::loadUtf8File/constructs document::Document
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
)

set_target_properties(NeoMIFES PROPERTIES
    FOLDER "src"
    OUTPUT_NAME "NeoMIFES"
)
