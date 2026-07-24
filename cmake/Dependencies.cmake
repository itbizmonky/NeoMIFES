# -----------------------------------------------------------------------------
# Dependencies. Unconditionally include()'d from root CMakeLists.txt (Phase
# 5b3a): RE2 + Abseil (Search Engine, ADR-002) are a genuine runtime
# dependency of the NeoMIFES.exe app target itself now that
# src/app/CMakeLists.txt links neomifes::search (Find bar UI). Test/bench-only
# dependencies (GoogleTest/google-benchmark) live in TestDependencies.cmake,
# include()'d only when NEOMIFES_BUILD_TESTS is ON - this file must NOT grow
# a test-only dependency back into it, or an app-only (NEOMIFES_BUILD_TESTS=OFF)
# configure would start fetching test infrastructure it doesn't need.
# -----------------------------------------------------------------------------
include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# get_property(... DIRECTORY <dir> PROPERTY BUILDSYSTEM_TARGETS) only returns
# targets created directly in <dir>'s own CMakeLists.txt, not ones created by
# further add_subdirectory() calls inside it - Abseil's top-level
# CMakeLists.txt only add_subdirectory(absl)'s, so every actual absl_* target
# lives several directories deeper. This walks SUBDIRECTORIES recursively to
# collect them all; needed below to force-correct MSVC_RUNTIME_LIBRARY on
# every one of them, not just whichever happened to be visible at the top.
function(neomifes_collect_targets_recursive out_var dir)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_subdir ${_subdirs})
        neomifes_collect_targets_recursive(_subdir_targets "${_subdir}")
        list(APPEND _targets ${_subdir_targets})
    endforeach()
    set(${out_var} ${_targets} PARENT_SCOPE)
endfunction()

# EXCLUDE_FROM_ALL on both Declares below: we never install() this project,
# and this keeps re2/absl::* out of the default "ALL" build unless something
# we actually link (neomifes_search) pulls them in transitively.

# ---- Abseil -------------------------------------------------------------
# LTS 20250814 - the release current around RE2 2025-11-05's own release date
# (paired deliberately rather than jumping to a much newer Abseil LTS the
# pinned RE2 tag was never tested against; RE2's CMake CI tracks Abseil via
# vcpkg's rolling package rather than a pinned tag, so no exact compatibility
# matrix exists to check against - ADR-002 sec."影響").
FetchContent_Declare(
    abseil-cpp
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG        20250814.2
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
)
set(ABSL_PROPAGATE_CXX_STD ON  CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL    OFF CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING     OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)  # Abseil's CMake also honors the generic CTest switch

FetchContent_MakeAvailable(abseil-cpp)

# ---- RE2 (ADR-002) --------------------------------------------------------
# Declared after abseil-cpp so RE2's CMakeLists (which does
# `if(NOT TARGET absl::base) find_package(absl REQUIRED)`) finds the
# already-populated in-tree Abseil targets instead of searching for a
# system install.
FetchContent_Declare(
    re2
    GIT_REPOSITORY https://github.com/google/re2.git
    GIT_TAG        2025-11-05
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
)
set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RE2_INSTALL       OFF CACHE BOOL "" FORCE)  # guards the install(EXPORT re2Targets ...) that fails against ABSL_ENABLE_INSTALL=OFF
set(BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(re2)

# ---- nlohmann/json (ADR-013) ----------------------------------------------
# core::SearchHistory (Phase 5c5) is this project's first consumer -
# header-only, no absl/RE2-style transitive dependency chain, so this is a
# much smaller addition than the block above.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json)

# ---- tree-sitter core (ADR-014) --------------------------------------------
FetchContent_Declare(
    tree-sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG        v0.26.11
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
)
set(TREE_SITTER_FEATURE_WASM OFF CACHE BOOL "" FORCE)  # roadmap sec.7.3 "WASM除外版"
# BUILD_SHARED_LIBS OFF already forced above (RE2 block) and stays in effect.

FetchContent_MakeAvailable(tree-sitter)

# ---- tree-sitter-cpp grammar (ADR-014) --------------------------------------
# SOURCE_SUBDIR pointing at a nonexistent path is the documented FetchContent
# idiom for "populate the source but do NOT add_subdirectory() it" - needed
# because tree-sitter-cpp's own CMakeLists.txt has an add_custom_command that
# tries to regenerate src/parser.c via a `tree-sitter` CLI binary
# (find_program(TREE_SITTER_CLI tree-sitter)) that isn't installed on this
# machine (or CI), which fails the build even though the already-committed
# parser.c works fine as-is (verified via a standalone probe, see ADR-014
# "実装上の注意点"). Instead, a small add_library() below compiles the
# fetched parser.c/scanner.c directly, bypassing that CMakeLists.txt.
FetchContent_Declare(
    tree-sitter-cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG        v0.23.4
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-cpp)

add_library(tree-sitter-cpp-grammar STATIC
    "${tree-sitter-cpp_SOURCE_DIR}/src/parser.c"
    "${tree-sitter-cpp_SOURCE_DIR}/src/scanner.c"
)
target_include_directories(tree-sitter-cpp-grammar PRIVATE "${tree-sitter-cpp_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-cpp-grammar PRIVATE tree-sitter)

# ---- tree-sitter-python grammar (Phase 7d) ---------------------------------
# Same SOURCE_SUBDIR "does-not-exist" workaround as tree-sitter-cpp above -
# tree-sitter-python's own CMakeLists.txt has the identical
# find_program(TREE_SITTER_CLI tree-sitter) regeneration problem (verified via
# a standalone probe before this block was written, matching the same
# discipline as ADR-014's original tree-sitter-cpp verification). Unlike C++,
# Python's grammar needs src/scanner.c (an external scanner implementing
# INDENT/DEDENT token generation for the language's indentation-based block
# structure) in addition to src/parser.c - both are compiled directly below.
FetchContent_Declare(
    tree-sitter-python
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python.git
    GIT_TAG        v0.25.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-python)

add_library(tree-sitter-python-grammar STATIC
    "${tree-sitter-python_SOURCE_DIR}/src/parser.c"
    "${tree-sitter-python_SOURCE_DIR}/src/scanner.c"
)
target_include_directories(tree-sitter-python-grammar PRIVATE "${tree-sitter-python_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-python-grammar PRIVATE tree-sitter)

# Third-party targets should not be linted with our strict flags, nor built
# with COMPILE_WARNING_AS_ERROR (RE2/Abseil are warning-clean upstream but
# not against our stricter /W4 policy).
#
# MSVC_RUNTIME_LIBRARY is also force-reset here to whatever the top-level
# CMAKE_MSVC_RUNTIME_LIBRARY cache value is: Abseil's own CMakeLists.txt
# unconditionally calls set(CMAKE_MSVC_RUNTIME_LIBRARY ...) itself (its
# ABSL_MSVC_STATIC_RUNTIME option, default OFF -> the *DLL, config-dependent
# form), which silently overrides whatever the ubsan preset requested for
# every absl_* target several add_subdirectory() levels deep - RE2 and every
# other target outside that tree still see the preset's original value. The
# two disagreeing produces an /MDd (Abseil) vs /MT (RE2) link error
# ("mismatch detected for '_ITERATOR_DEBUG_LEVEL'"), discovered when first
# adding this dependency (Phase 5a). Re-applying our own value after the
# fact on every fetched target, rather than fighting Abseil's option, keeps
# the ubsan preset's already-documented release-CRT requirement
# (Sanitizers.cmake) intact. nlohmann_json is INTERFACE-only (header-only,
# no compiled sources of its own), so it has no MSVC_RUNTIME_LIBRARY/
# COMPILE_WARNING_AS_ERROR properties to fight in the first place, but is
# included in the FOLDER tidy-up below for consistency. tree-sitter/
# tree-sitter-cpp-grammar are plain C static libs (Phase 7a, ADR-014) added
# to the same loop rather than duplicating these property-setting calls.
# tree-sitter-python-grammar (Phase 7d) is the same kind of target.
neomifes_collect_targets_recursive(_neomifes_absl_targets "${abseil-cpp_SOURCE_DIR}")
foreach(_tp ${_neomifes_absl_targets} re2 nlohmann_json tree-sitter tree-sitter-cpp-grammar tree-sitter-python-grammar)
    if(TARGET ${_tp})
        set_target_properties(${_tp} PROPERTIES
            FOLDER "third_party"
            COMPILE_WARNING_AS_ERROR OFF
        )
        # Only override when the preset actually requests a specific runtime
        # (the ubsan preset does; debug/release do not) - setting this
        # property to an explicit empty string is NOT equivalent to leaving
        # it unset, so this must not run unconditionally or it would change
        # debug/release's (already-working) third-party runtime selection.
        if(CMAKE_MSVC_RUNTIME_LIBRARY)
            set_target_properties(${_tp} PROPERTIES
                MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}"
            )
        endif()
    endif()
endforeach()
