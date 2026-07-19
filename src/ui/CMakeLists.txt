add_library(neomifes_ui STATIC
    src/find_bar.cpp
    src/command_palette.cpp
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
        # find_bar.cpp: wchar_cast.h (char16_t/wchar_t boundary conversion
        # for the edit control's text, Phase 5b3a).
        neomifes::util
        user32
        gdi32
        # find_bar.cpp: SetWindowSubclass/DefSubclassProc (Phase 5b3a, this
        # project's first child HWND).
        comctl32
)

set_target_properties(neomifes_ui PROPERTIES
    FOLDER "src"
)
