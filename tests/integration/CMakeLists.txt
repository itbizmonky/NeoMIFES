add_executable(neomifes_startup_measure_test
    startup_measure_test.cpp
)

target_link_libraries(neomifes_startup_measure_test
    PRIVATE
        neomifes::compile_options
        GTest::gtest
)

set_target_properties(neomifes_startup_measure_test PROPERTIES
    FOLDER "tests"
)

# Register with CTest, passing NeoMIFES.exe path as argv[1].
add_test(
    NAME    startup_measure
    COMMAND neomifes_startup_measure_test $<TARGET_FILE:NeoMIFES>
)

# Ensure the executable-under-test is built before running the integration test.
set_tests_properties(startup_measure PROPERTIES
    LABELS "integration"
    TIMEOUT 30
)

# render_device_smoke_test - exercises real COM/D3D11/D2D/DXGI device
# creation (HARDWARE->WARP fallback), which the plain unit-test binary
# deliberately avoids. No subprocess spawning needed, so gtest_main suffices
# (unlike startup_measure_test, which needs a custom main() to receive the
# built NeoMIFES.exe path as argv[1]).
add_executable(neomifes_render_device_smoke_test
    render_device_smoke_test.cpp
)

target_link_libraries(neomifes_render_device_smoke_test
    PRIVATE
        neomifes::compile_options
        neomifes::render
        GTest::gtest
        GTest::gtest_main
)

set_target_properties(neomifes_render_device_smoke_test PROPERTIES
    FOLDER "tests"
)

add_test(
    NAME    render_device_smoke
    COMMAND neomifes_render_device_smoke_test
)

set_tests_properties(render_device_smoke PROPERTIES
    LABELS "integration"
    TIMEOUT 30
)

# render_text_smoke_test - exercises RenderPipeline::render() actually
# drawing Document content via DirectWrite (Phase 3b), on top of the real
# device graph render_device_smoke_test already covers in isolation.
add_executable(neomifes_render_text_smoke_test
    render_text_smoke_test.cpp
)

target_link_libraries(neomifes_render_text_smoke_test
    PRIVATE
        neomifes::compile_options
        neomifes::document
        neomifes::render
        GTest::gtest
        GTest::gtest_main
)

set_target_properties(neomifes_render_text_smoke_test PROPERTIES
    FOLDER "tests"
)

add_test(
    NAME    render_text_smoke
    COMMAND neomifes_render_text_smoke_test
)

set_tests_properties(render_text_smoke PROPERTIES
    LABELS "integration"
    TIMEOUT 30
)

# render_syntax_worker_test - exercises SyntaxWorker's real background
# std::thread + PostMessageW completion handoff end-to-end (Phase 7c), which
# the plain unit-test binary avoids (this codebase's first std::thread).
add_executable(neomifes_render_syntax_worker_test
    render_syntax_worker_test.cpp
)

target_link_libraries(neomifes_render_syntax_worker_test
    PRIVATE
        neomifes::compile_options
        neomifes::document
        neomifes::render
        GTest::gtest
        GTest::gtest_main
)

set_target_properties(neomifes_render_syntax_worker_test PROPERTIES
    FOLDER "tests"
)

add_test(
    NAME    render_syntax_worker
    COMMAND neomifes_render_syntax_worker_test
)

set_tests_properties(render_syntax_worker PROPERTIES
    LABELS "integration"
    TIMEOUT 30
)

# platform_clipboard_test - exercises the real OS clipboard (Phase 4b6c),
# which the plain unit-test binary avoids entirely.
add_executable(neomifes_platform_clipboard_test
    platform_clipboard_test.cpp
)

target_link_libraries(neomifes_platform_clipboard_test
    PRIVATE
        neomifes::compile_options
        neomifes::platform
        GTest::gtest
        GTest::gtest_main
)

set_target_properties(neomifes_platform_clipboard_test PROPERTIES
    FOLDER "tests"
)

add_test(
    NAME    platform_clipboard
    COMMAND neomifes_platform_clipboard_test
)

set_tests_properties(platform_clipboard PROPERTIES
    LABELS "integration"
    TIMEOUT 30
)

# frame_measure_test - exercises --measure-frame (Phase 3c, ADR-011)
# end-to-end via subprocess spawn, same pattern as startup_measure_test.cpp
# (custom main() to receive the built NeoMIFES.exe path as argv[1]).
add_executable(neomifes_frame_measure_test
    frame_measure_test.cpp
)

target_link_libraries(neomifes_frame_measure_test
    PRIVATE
        neomifes::compile_options
        GTest::gtest
)

set_target_properties(neomifes_frame_measure_test PROPERTIES
    FOLDER "tests"
)

add_test(
    NAME    frame_measure
    COMMAND neomifes_frame_measure_test $<TARGET_FILE:NeoMIFES>
)

# 300 vsync-gated frames (~5s at 60fps) plus process startup, plus headroom
# for a loaded CI runner or WARP software fallback.
set_tests_properties(frame_measure PROPERTIES
    LABELS "integration"
    TIMEOUT 60
)

# plugin_load_test - loads the REAL built hello_plugin/
# hello_plugin_bad_api_version/crashing_plugin/throwing_plugin sample DLLs
# (Phase 8a, ADR-015) via PluginHost::load(). First test in this codebase to
# LoadLibraryW a sibling-built CMake artifact rather than spawning it as a
# subprocess (contrast startup_measure_test.cpp/frame_measure_test.cpp,
# which spawn NeoMIFES.exe as a child process instead).
add_executable(neomifes_plugin_load_test
    plugin_load_test.cpp
)

target_link_libraries(neomifes_plugin_load_test
    PRIVATE
        neomifes::compile_options
        neomifes::plugin
        GTest::gtest
)

set_target_properties(neomifes_plugin_load_test PROPERTIES
    FOLDER "tests"
)

add_test(
    NAME    plugin_load
    COMMAND neomifes_plugin_load_test
            $<TARGET_FILE:hello_plugin>
            $<TARGET_FILE:hello_plugin_bad_api_version>
            $<TARGET_FILE:crashing_plugin>
            $<TARGET_FILE:throwing_plugin>
)

set_tests_properties(plugin_load PROPERTIES
    LABELS "integration"
    TIMEOUT 30
)

# plugin_document_editing_test - loads the REAL built document_editing_plugin
# sample DLL (Phase 8b, ADR-016) via PluginHost::load(), passing a real
# NeoMifesCoreApi (neomifes::app::buildPluginCoreApi()) bound to a real
# document::Document, and asserts the plugin's onLoad actually mutated that
# document through the C-ABI boundary - proof (not assumption, CLAUDE.md
# rule 3) that the NeoMifesCoreApi round trip works end-to-end.
add_executable(neomifes_plugin_document_editing_test
    plugin_document_editing_test.cpp
)

target_link_libraries(neomifes_plugin_document_editing_test
    PRIVATE
        neomifes::compile_options
        neomifes::plugin
        neomifes::document
        neomifes::app_input
        GTest::gtest
)

set_target_properties(neomifes_plugin_document_editing_test PROPERTIES
    FOLDER "tests"
)

add_test(
    NAME    plugin_document_editing
    COMMAND neomifes_plugin_document_editing_test $<TARGET_FILE:document_editing_plugin>
)

set_tests_properties(plugin_document_editing PROPERTIES
    LABELS "integration"
    TIMEOUT 30
)

# plugin_sandbox_test - exercises the real Windows Job Object sandboxing
# (Phase 8c, ADR-017): AssignProcessToJobObject is a one-way, permanent
# operation for the life of a process, so this MUST be its own dedicated
# executable - mixing it into any other test binary would permanently
# strip that binary's ability to spawn child processes for every test
# that runs after it, for the rest of that process's life.
add_executable(neomifes_plugin_sandbox_test
    plugin_sandbox_test.cpp
)

target_link_libraries(neomifes_plugin_sandbox_test
    PRIVATE
        neomifes::compile_options
        neomifes::plugin
        GTest::gtest
        GTest::gtest_main
)

set_target_properties(neomifes_plugin_sandbox_test PROPERTIES
    FOLDER "tests"
)

add_test(
    NAME    plugin_sandbox
    COMMAND neomifes_plugin_sandbox_test
)

set_tests_properties(plugin_sandbox PROPERTIES
    LABELS "integration"
    TIMEOUT 30
)
