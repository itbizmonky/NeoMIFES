add_executable(NeoMIFES WIN32
    main.cpp
    startup_profile.cpp
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
        user32
        shell32
)

set_target_properties(NeoMIFES PROPERTIES
    FOLDER "src"
    OUTPUT_NAME "NeoMIFES"
)
