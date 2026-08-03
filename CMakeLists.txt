cmake_minimum_required(VERSION 3.28)

project(NeoMIFES
    VERSION 0.0.1
    DESCRIPTION "NeoMIFES - Windows native text editor"
    # C added in Phase 7a (ADR-014): tree-sitter core + tree-sitter-cpp-grammar
    # are plain C libraries. Declaring both languages at the top level (rather
    # than relying solely on tree-sitter's own nested project(... LANGUAGES C)
    # call to introduce it) avoids CMAKE_C_COMPILE_OBJECT/CMAKE_C_ARCHIVE_*
    # going undetected on an incremental reconfigure of a build tree that
    # predates this dependency - observed firsthand when adding it.
    LANGUAGES CXX C
)

# -----------------------------------------------------------------------------
# Global settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CMake >=3.28 auto-enables C++20 module scanning for MSVC. That produces
# `@...obj.modmap` compile flags in compile_commands.json which clang-tidy
# (LLVM 19) cannot parse and rejects with a hard error. We do not use
# `import std;` / `import` yet (CLAUDE.md sec.4 "段階採用"), so disable
# scanning until we adopt modules for real - at which point the tooling
# story also needs revisiting.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Prefer folder view in IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# -----------------------------------------------------------------------------
# Toolchain enforcement (ADR-005: MSVC v14.43 / VS 17.13+)
# -----------------------------------------------------------------------------
if(NOT MSVC)
    message(FATAL_ERROR
        "NeoMIFES currently supports MSVC (Visual Studio) only. "
        "See docs/decisions/ADR-005-min-msvc-version.md")
endif()

if(MSVC_VERSION LESS 1943)
    message(FATAL_ERROR
        "NeoMIFES requires MSVC v14.43 (Visual Studio 2022 17.13) or later. "
        "Detected MSVC_VERSION=${MSVC_VERSION}. "
        "See docs/decisions/ADR-005-min-msvc-version.md")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "NeoMIFES supports x64 builds only.")
endif()

# -----------------------------------------------------------------------------
# Project options
# -----------------------------------------------------------------------------
option(NEOMIFES_BUILD_TESTS   "Build unit tests and benchmarks"        ON)
option(NEOMIFES_ENABLE_ASAN   "Enable AddressSanitizer"                 OFF)
option(NEOMIFES_ENABLE_UBSAN  "Enable UndefinedBehaviorSanitizer (clang-cl only)" OFF)
option(NEOMIFES_WARN_AS_ERROR "Treat compile warnings as errors"        ON)

# -----------------------------------------------------------------------------
# Shared cmake helpers
# -----------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompileOptions)
include(Sanitizers)

# Dependencies.cmake fetches RE2/Abseil (Search Engine, ADR-002).
# Unconditional since Phase 5b3a: src/app/CMakeLists.txt's NeoMIFES target
# now links neomifes::search (Find bar UI), so RE2/Abseil are a genuine
# runtime dependency of the app itself, not just of tests/bench.
include(Dependencies)

# neomifes::plugin_sdk (Phase 8a, ADR-015) - the C-ABI header both
# src/plugin/ (the host) and plugins/samples/**/ (sample plugin DLLs) link
# against. Included before add_subdirectory(src) since src/plugin/CMakeLists.txt
# depends on it.
include(PluginSdk)

if(NEOMIFES_BUILD_TESTS)
    # TestDependencies.cmake fetches GoogleTest/benchmark - test-only, so
    # stays gated (an app-only configure must not fetch these).
    include(TestDependencies)
    enable_testing()
endif()

# -----------------------------------------------------------------------------
# Subdirectories
# -----------------------------------------------------------------------------
add_subdirectory(src)

if(NEOMIFES_BUILD_TESTS)
    # plugins/samples/{hello_plugin,hello_plugin_bad_api_version,
    # crashing_plugin,throwing_plugin} (Phase 8a, ADR-015) - this repo's
    # first SHARED/MODULE CMake targets. Gated behind NEOMIFES_BUILD_TESTS:
    # tests/integration/plugin_load_test.cpp is their only consumer right
    # now (PluginHost is not wired into NeoMIFES.exe itself yet - see
    # ADR-015's deferred list).
    add_subdirectory(plugins/samples/hello_plugin)
    add_subdirectory(plugins/samples/hello_plugin_bad_api_version)
    add_subdirectory(plugins/samples/crashing_plugin)
    add_subdirectory(plugins/samples/throwing_plugin)
    # document_editing_plugin (Phase 8b, ADR-016) - proves NeoMifesCoreApi's
    # insertText round-trips through the DLL boundary into a real
    # document::Document. tests/integration/plugin_document_editing_test.cpp
    # is its only consumer, same "headless-only" reasoning as above.
    add_subdirectory(plugins/samples/document_editing_plugin)
    # permission_denied_plugin (Phase 8d, ADR-018) - proves an
    # undeclared-permission plugin gets a NULL-gated NeoMifesCoreApi and
    # crashes safely (SEH-isolated) if it calls through it anyway.
    # tests/integration/plugin_document_editing_test.cpp is its only
    # consumer, same "headless-only" reasoning as above.
    add_subdirectory(plugins/samples/permission_denied_plugin)
    # toast_plugin (Phase 8e, ADR-019) - proves NeoMifesCoreApi::showToast
    # round-trips into a real ui::ToastState without NEOMIFES_PLUGIN_
    # PERMISSION_DOCUMENT. tests/integration/plugin_toast_test.cpp is its
    # only consumer, same "headless-only" reasoning as above.
    add_subdirectory(plugins/samples/toast_plugin)
    # command_plugin/crashing_command_plugin (Phase 8f, ADR-020) - prove
    # NeoMifesCoreApi::registerCommand's deferred SEH-protected invocation
    # round-trips correctly (command_plugin) and safely isolates a crash
    # invoked outside PluginHost::load()'s own call stack
    # (crashing_command_plugin). tests/integration/plugin_command_test.cpp
    # is their only consumer, same "headless-only" reasoning as above.
    add_subdirectory(plugins/samples/command_plugin)
    add_subdirectory(plugins/samples/crashing_command_plugin)
    add_subdirectory(tests)
endif()

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "NeoMIFES ${PROJECT_VERSION}")
message(STATUS "  Config     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  MSVC       : ${MSVC_VERSION}")
message(STATUS "  Build tests: ${NEOMIFES_BUILD_TESTS}")
message(STATUS "  ASan       : ${NEOMIFES_ENABLE_ASAN}")
message(STATUS "  Warn=Error : ${NEOMIFES_WARN_AS_ERROR}")
message(STATUS "")
