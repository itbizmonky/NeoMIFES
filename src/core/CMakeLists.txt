add_library(neomifes_core STATIC
    src/bookmark_manager.cpp
    src/command_dispatcher.cpp
    src/cumulative_shift_edit.cpp
    src/edit_commands.cpp
    src/replace_all_command.cpp
    src/selection_model.cpp
    src/undo_stack.cpp
    src/viewport.cpp
)
add_library(neomifes::core ALIAS neomifes_core)

target_include_directories(neomifes_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_core
    PUBLIC
        # command.h/edit_commands.h/selection_model.h/viewport.h expose
        # document::Document&/document::TextPos/document::TextRange in their
        # public API.
        neomifes::document
    PRIVATE
        neomifes::compile_options
        neomifes::util
)

set_target_properties(neomifes_core PROPERTIES
    FOLDER "src"
)
