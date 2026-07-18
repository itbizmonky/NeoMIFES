# -----------------------------------------------------------------------------
# Dependencies. This whole file is only include()'d when NEOMIFES_BUILD_TESTS
# is ON (see root CMakeLists.txt) - every dependency declared here is
# currently a test/bench-only consumer:
#   - GoogleTest / google/benchmark: test infrastructure itself.
#   - RE2 + Abseil (Search Engine, ADR-002): only tests/bench link
#     neomifes::search today (src/app/CMakeLists.txt's NeoMIFES target does
#     not yet). Move this file's RE2/Abseil section to be unconditionally
#     include()'d once Phase 5b actually wires SearchService into the app -
#     at that point it becomes a genuine core runtime dependency.
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
# (Sanitizers.cmake) intact.
neomifes_collect_targets_recursive(_neomifes_absl_targets "${abseil-cpp_SOURCE_DIR}")
foreach(_tp ${_neomifes_absl_targets} re2)
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

# ---- GoogleTest -------------------------------------------------------------
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
set(BUILD_GMOCK        OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST      OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)   # Match /MDd / /MD used by our targets

# ---- google/benchmark -------------------------------------------------------
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
    GIT_SHALLOW    TRUE
)
set(BENCHMARK_ENABLE_TESTING     OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL     OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR      OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest benchmark)

# Third-party targets should not be linted with our strict flags.
foreach(_tp gtest gtest_main gmock gmock_main benchmark benchmark_main)
    if(TARGET ${_tp})
        set_target_properties(${_tp} PROPERTIES
            FOLDER "third_party"
            COMPILE_WARNING_AS_ERROR OFF
        )
    endif()
endforeach()
