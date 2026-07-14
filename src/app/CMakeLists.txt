add_executable(NeoMIFES WIN32
    main.cpp
    startup_profile.cpp
)

target_link_libraries(NeoMIFES
    PRIVATE
        neomifes::compile_options
        neomifes::util
        neomifes::platform
        neomifes::ui
        user32
        shell32
)

set_target_properties(NeoMIFES PROPERTIES
    FOLDER "src"
    OUTPUT_NAME "NeoMIFES"
)
