add_library(neomifes_core STATIC
    src/autosave_index.cpp
    src/bookmark_manager.cpp
    src/command_dispatcher.cpp
    src/cumulative_shift_edit.cpp
    src/edit_commands.cpp
    src/folding_model.cpp
    src/indentation_conversion.cpp
    src/json_string_convert.cpp
    src/key_bindings.cpp
    src/key_bindings_presets.cpp
    src/recent_files.cpp
    src/replace_all_command.cpp
    src/search_history.cpp
    src/selection_model.cpp
    src/settings.cpp
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
        # json_string_convert.cpp (WI-10, shared by settings.cpp/
        # search_history.cpp/key_bindings.cpp): UTF-16<->UTF-8 boundary
        # conversion (neomifes::encoding) and JSON I/O (nlohmann_json) -
        # both are implementation details, not exposed in any of those
        # files' public headers, so PRIVATE is sufficient (ADR-013).
        neomifes::encoding
        nlohmann_json::nlohmann_json
)

set_target_properties(neomifes_core PROPERTIES
    FOLDER "src"
)
