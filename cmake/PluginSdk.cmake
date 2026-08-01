# -----------------------------------------------------------------------------
# neomifes::plugin_sdk
#   Header-only INTERFACE target for include/neomifes/plugin_sdk.h - the
#   C-ABI plugin SDK header (Phase 8a, ADR-015). Lives at the repo TOP-LEVEL
#   include/ directory (not under any src/<module>/include/), unlike every
#   other public header in this codebase: it is the one header meant to be
#   copied out and given to third-party plugin authors independent of this
#   repo's internal src/ layout (CLAUDE.md sec.5 directory plan).
#   Deliberately depends on nothing else in this project.
# -----------------------------------------------------------------------------
add_library(neomifes_plugin_sdk INTERFACE)
add_library(neomifes::plugin_sdk ALIAS neomifes_plugin_sdk)

target_include_directories(neomifes_plugin_sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)
