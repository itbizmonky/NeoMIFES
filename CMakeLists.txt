cmake_minimum_required(VERSION 3.28)

project(NeoMIFES
    VERSION 0.0.1
    DESCRIPTION "NeoMIFES - Windows native text editor"
    LANGUAGES CXX
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

if(NEOMIFES_BUILD_TESTS)
    # Dependencies.cmake fetches RE2/Abseil (Search Engine, ADR-002) plus
    # GoogleTest/benchmark. RE2/Abseil are gated here rather than fetched
    # unconditionally: as of Phase 5a, neomifes::search is only actually
    # linked by tests/bench (src/app/CMakeLists.txt's NeoMIFES target does
    # not link it yet), so an app-only configure has no real need for them.
    # Move this include out of the NEOMIFES_BUILD_TESTS guard once Phase 5b
    # actually wires SearchService into the NeoMIFES executable - at that
    # point RE2/Abseil genuinely become an unconditional runtime dependency.
    include(Dependencies)
    enable_testing()
endif()

# -----------------------------------------------------------------------------
# Subdirectories
# -----------------------------------------------------------------------------
add_subdirectory(src)

if(NEOMIFES_BUILD_TESTS)
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
