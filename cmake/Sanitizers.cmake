# -----------------------------------------------------------------------------
# AddressSanitizer support for MSVC.
#   Enable by passing -DNEOMIFES_ENABLE_ASAN=ON (or using the 'asan' preset).
#   Notes:
#     - /RTC1 is incompatible with /fsanitize=address and must be removed.
#     - Debug info (/Zi) is required for symbolized reports.
# -----------------------------------------------------------------------------

if(NOT NEOMIFES_ENABLE_ASAN)
    return()
endif()

if(NOT MSVC)
    message(WARNING "AddressSanitizer support is currently wired for MSVC only.")
    return()
endif()

message(STATUS "AddressSanitizer: ENABLED")

# Strip /RTC* (added by CMake's Debug defaults) to avoid ASan/RTC conflict.
foreach(flag_var
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_C_FLAGS
        CMAKE_C_FLAGS_DEBUG)
    if(${flag_var} MATCHES "/RTC[1csu]")
        string(REGEX REPLACE "/RTC[1csu]" "" ${flag_var} "${${flag_var}}")
        set(${flag_var} "${${flag_var}}" CACHE STRING "" FORCE)
    endif()
endforeach()

target_compile_options(neomifes_compile_options INTERFACE
    /fsanitize=address
    /Zi
)
target_compile_definitions(neomifes_compile_options INTERFACE
    _DISABLE_VECTOR_ANNOTATION
    _DISABLE_STRING_ANNOTATION
)
target_link_options(neomifes_compile_options INTERFACE
    /INCREMENTAL:NO
)
