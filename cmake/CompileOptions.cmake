# -----------------------------------------------------------------------------
# neomifes::compile_options
#   Shared compile flags / definitions / include-dir policies.
#   Every NeoMIFES target should link this INTERFACE library PRIVATE-ly.
# -----------------------------------------------------------------------------

add_library(neomifes_compile_options INTERFACE)
add_library(neomifes::compile_options ALIAS neomifes_compile_options)

if(MSVC)
    # Warnings
    target_compile_options(neomifes_compile_options INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /Zc:inline
        /Zc:throwingNew
        /EHsc
        /GR-           # Disable RTTI (dynamic_cast is banned - see CLAUDE.md sec.4)
        /utf-8
        /diagnostics:caret
        /volatile:iso
        /Zc:__STDC__
    )

    # clang-cl (used for the UBSan build, see Sanitizers.cmake) rejects several
    # of the MSVC-only /Zc:* flags above as "unused command line argument" -
    # with /WX that becomes a hard error. CI's clang-tidy job already works
    # around the same issue via --extra-arg=-Wno-unused-command-line-argument;
    # this is the equivalent for an actual clang-cl *compile*, not just analysis.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(neomifes_compile_options INTERFACE
            -Wno-unused-command-line-argument
        )
    endif()

    if(NEOMIFES_WARN_AS_ERROR)
        target_compile_options(neomifes_compile_options INTERFACE /WX)
    endif()

    # Config-specific
    target_compile_options(neomifes_compile_options INTERFACE
        $<$<CONFIG:Debug>:/Od;/Zi;/JMC>
        $<$<CONFIG:Release>:/O2;/Ob3;/DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:/O2;/Ob2;/Zi;/DNDEBUG>
    )

    # Definitions
    target_compile_definitions(neomifes_compile_options INTERFACE
        _UNICODE
        UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        _SCL_SECURE_NO_WARNINGS
        _WIN32_WINNT=0x0A00        # Windows 10
        WINVER=0x0A00
    )

    # Linker
    target_link_options(neomifes_compile_options INTERFACE
        /INCREMENTAL:NO
        $<$<CONFIG:Release>:/OPT:REF;/OPT:ICF>
    )
endif()
