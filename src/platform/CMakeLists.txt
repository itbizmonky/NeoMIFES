add_library(neomifes_platform STATIC
    src/clipboard.cpp
    src/codepage_convert.cpp
    src/file_mapping.cpp
    src/perf_clock.cpp
    src/process_metrics.cpp
)
add_library(neomifes::platform ALIAS neomifes_platform)

target_include_directories(neomifes_platform
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_platform
    PUBLIC
        # System libraries needed by public headers or callers.
    PRIVATE
        neomifes::compile_options
        neomifes::util
        Psapi
)

set_target_properties(neomifes_platform PROPERTIES
    FOLDER "src"
)
