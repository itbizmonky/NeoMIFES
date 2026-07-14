# -----------------------------------------------------------------------------
# Test / benchmark dependencies (fetched only when NEOMIFES_BUILD_TESTS=ON).
#   - GoogleTest 1.15.x
#   - google/benchmark 1.9.x
# -----------------------------------------------------------------------------
include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

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
