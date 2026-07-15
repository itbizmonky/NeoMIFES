# -----------------------------------------------------------------------------
# Sanitizer support.
#   ASan: -DNEOMIFES_ENABLE_ASAN=ON (or the 'asan' preset). MSVC's own /fsanitize=address.
#   UBSan: -DNEOMIFES_ENABLE_UBSAN=ON (or the 'ubsan' preset). MSVC has no UBSan of its
#          own (self_review.md R4), so this branch only applies when the compiler is
#          clang-cl (CMAKE_CXX_COMPILER_ID=="Clang", CMAKE_CXX_COMPILER_FRONTEND_VARIANT
#          =="MSVC") - clang-cl accepts clang-style single-dash -fsanitize= flags directly.
# -----------------------------------------------------------------------------

if(NEOMIFES_ENABLE_ASAN)
    if(NOT MSVC)
        message(WARNING "AddressSanitizer support is currently wired for MSVC only.")
    else()
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
    endif()
endif()

if(NEOMIFES_ENABLE_UBSAN)
    if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
        message(FATAL_ERROR "NEOMIFES_ENABLE_UBSAN requires clang-cl (CMAKE_CXX_COMPILER=clang-cl.exe); "
                             "MSVC has no UBSan of its own (self_review.md R4).")
    endif()

    message(STATUS "UndefinedBehaviorSanitizer (clang-cl): ENABLED")

    # clang-cl's bundled UBSan runtime (clang_rt.ubsan_standalone_cxx-*.lib) is
    # only built against the release CRT. The 'ubsan' preset forces
    # CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL (/MD, not /MDd) so
    # _ITERATOR_DEBUG_LEVEL matches; /RTC1 (CMake's Debug-config default)
    # requires the debug CRT, so it must be stripped here for the same reason
    # ASan strips it above.
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

    # -fno-sanitize=alignment: the Microsoft STL/UCRT headers (e.g. wchar.h's
    # wcslen fast path) do unaligned word-at-a-time reads that are safe on
    # x86/x64 hardware but technically violate strict alignment rules - a
    # well-known false-positive source when running UBSan against Microsoft's
    # own standard library, confirmed locally (AddBufferTest.* flagged inside
    # ucrt/wchar.h, not in NeoMIFES code). Every other UBSan check stays on.
    target_compile_options(neomifes_compile_options INTERFACE
        -fsanitize=undefined
        -fno-sanitize=alignment
        -fno-sanitize-recover=undefined
    )
    # CMake's clang-cl+Ninja setup invokes lld-link.exe directly for linking,
    # which does not understand -fsanitize=undefined (benign "ignoring unknown
    # argument" warning) - the UBSan runtime actually gets linked via a
    # #pragma comment(lib, ...) that clang-cl embeds in each object file at
    # compile time, so the flag is kept here for documentation/consistency
    # rather than because lld-link acts on it.
    target_link_options(neomifes_compile_options INTERFACE
        -fsanitize=undefined
        /INCREMENTAL:NO
    )
endif()
