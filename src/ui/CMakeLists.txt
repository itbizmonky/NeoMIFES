add_library(neomifes_ui STATIC
    src/find_dialog.cpp
    src/find_replace_dialog.cpp
    src/command_palette.cpp
    src/goto_line_bar.cpp
    src/grep_bar.cpp
    src/outline_pane.cpp
    src/json_tree_pane.cpp
    src/json_path_bar.cpp
    src/csv_grid_pane.cpp
    src/git_pane.cpp
    src/tab_bar.cpp
    src/status_bar.cpp
    src/main_window.cpp
    src/text_surface_accessible.cpp
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
        # find_dialog.cpp/find_replace_dialog.cpp: wchar_cast.h (char16_t/
        # wchar_t boundary conversion for the edit controls' text; this
        # requirement traces back to find_bar.cpp, Phase 5b3a's original
        # WC_EDIT child, before WI-24 replaced it with find_dialog.cpp).
        neomifes::util
        user32
        gdi32
        # find_dialog.cpp/find_replace_dialog.cpp: SetWindowSubclass/
        # DefSubclassProc (originally find_bar.cpp, Phase 5b3a, this
        # project's first child HWND).
        comctl32
        # main_window.cpp: ImmGetContext/ImmGetCompositionStringW/
        # ImmSetCandidateWindow/ImmReleaseContext (WM_IME_*, WI-06). All raw
        # Imm32 calls live in this class - see main_window.h's header
        # comment - so this is the only target that needs imm32 linked.
        imm32
        # main_window.cpp/text_surface_accessible.cpp:
        # CreateStdAccessibleObject/LresultFromObject
        # (text_surface_no_screen_reader_exposure.md's minimal live-region
        # tier, WM_GETOBJECT).
        oleacc
)

set_target_properties(neomifes_ui PROPERTIES
    FOLDER "src"
)
