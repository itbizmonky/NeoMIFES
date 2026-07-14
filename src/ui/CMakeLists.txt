add_library(neomifes_ui STATIC
    src/main_window.cpp
)
add_library(neomifes::ui ALIAS neomifes_ui)

target_include_directories(neomifes_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_ui
    PUBLIC
        neomifes::platform
    PRIVATE
        neomifes::compile_options
        user32
        gdi32
)

set_target_properties(neomifes_ui PROPERTIES
    FOLDER "src"
)
