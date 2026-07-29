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

# ---- tree-sitter-c grammar (Phase 7n1) -------------------------------------
# Same SOURCE_SUBDIR workaround as tree-sitter-cpp/tree-sitter-python above.
# Unlike those two, tree-sitter-c's src/ has no scanner.c (confirmed via the
# GitHub API before writing this block, not guessed - CLAUDE.md rule 3): C's
# grammar needs no external-scanner state machine, so only parser.c is
# compiled.
FetchContent_Declare(
    tree-sitter-c
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-c.git
    GIT_TAG        v0.24.2
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-c)

add_library(tree-sitter-c-grammar STATIC
    "${tree-sitter-c_SOURCE_DIR}/src/parser.c"
)
target_include_directories(tree-sitter-c-grammar PRIVATE "${tree-sitter-c_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-c-grammar PRIVATE tree-sitter)

# ---- tree-sitter-javascript grammar (Phase 7n1) ----------------------------
# Same SOURCE_SUBDIR workaround. Has scanner.c (confirmed via GitHub API,
# same 2-file shape as tree-sitter-python/tree-sitter-cpp above).
FetchContent_Declare(
    tree-sitter-javascript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-javascript.git
    GIT_TAG        v0.25.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-javascript)

add_library(tree-sitter-javascript-grammar STATIC
    "${tree-sitter-javascript_SOURCE_DIR}/src/parser.c"
    "${tree-sitter-javascript_SOURCE_DIR}/src/scanner.c"
)
target_include_directories(tree-sitter-javascript-grammar PRIVATE "${tree-sitter-javascript_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-javascript-grammar PRIVATE tree-sitter)

# ---- tree-sitter-java grammar (Phase 7n1) ----------------------------------
# Same SOURCE_SUBDIR workaround. No scanner.c (confirmed via GitHub API).
FetchContent_Declare(
    tree-sitter-java
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-java.git
    GIT_TAG        v0.23.5
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-java)

add_library(tree-sitter-java-grammar STATIC
    "${tree-sitter-java_SOURCE_DIR}/src/parser.c"
)
target_include_directories(tree-sitter-java-grammar PRIVATE "${tree-sitter-java_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-java-grammar PRIVATE tree-sitter)

# ---- tree-sitter-go grammar (Phase 7n1) ------------------------------------
# Same SOURCE_SUBDIR workaround. No scanner.c (confirmed via GitHub API).
FetchContent_Declare(
    tree-sitter-go
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-go.git
    GIT_TAG        v0.25.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-go)

add_library(tree-sitter-go-grammar STATIC
    "${tree-sitter-go_SOURCE_DIR}/src/parser.c"
)
target_include_directories(tree-sitter-go-grammar PRIVATE "${tree-sitter-go_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-go-grammar PRIVATE tree-sitter)

# ---- tree-sitter-rust grammar (Phase 7n1) ----------------------------------
# Same SOURCE_SUBDIR workaround. Has scanner.c (confirmed via GitHub API,
# same 2-file shape as tree-sitter-python/tree-sitter-cpp/tree-sitter-javascript).
FetchContent_Declare(
    tree-sitter-rust
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-rust.git
    GIT_TAG        v0.24.2
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-rust)

add_library(tree-sitter-rust-grammar STATIC
    "${tree-sitter-rust_SOURCE_DIR}/src/parser.c"
    "${tree-sitter-rust_SOURCE_DIR}/src/scanner.c"
)
target_include_directories(tree-sitter-rust-grammar PRIVATE "${tree-sitter-rust_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-rust-grammar PRIVATE tree-sitter)

# ---- tree-sitter-json grammar (Phase 7n1) ----------------------------------
# Same SOURCE_SUBDIR workaround. No scanner.c (confirmed via GitHub API) -
# JSON's grammar is simple enough to need no external-scanner state machine.
FetchContent_Declare(
    tree-sitter-json
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-json.git
    GIT_TAG        v0.24.8
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-json)

add_library(tree-sitter-json-grammar STATIC
    "${tree-sitter-json_SOURCE_DIR}/src/parser.c"
)
target_include_directories(tree-sitter-json-grammar PRIVATE "${tree-sitter-json_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-json-grammar PRIVATE tree-sitter)

# ---- tree-sitter-html grammar (Phase 7r) -----------------------------------
# Same SOURCE_SUBDIR workaround. Has scanner.c (confirmed via GitHub API,
# same 2-file shape as tree-sitter-python/tree-sitter-cpp above).
FetchContent_Declare(
    tree-sitter-html
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-html.git
    GIT_TAG        v0.23.2
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-html)

add_library(tree-sitter-html-grammar STATIC
    "${tree-sitter-html_SOURCE_DIR}/src/parser.c"
    "${tree-sitter-html_SOURCE_DIR}/src/scanner.c"
)
target_include_directories(tree-sitter-html-grammar PRIVATE "${tree-sitter-html_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-html-grammar PRIVATE tree-sitter)

# ---- tree-sitter-css grammar (Phase 7r) ------------------------------------
# Same SOURCE_SUBDIR workaround. Has scanner.c (confirmed via GitHub API).
FetchContent_Declare(
    tree-sitter-css
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-css.git
    GIT_TAG        v0.25.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-css)

add_library(tree-sitter-css-grammar STATIC
    "${tree-sitter-css_SOURCE_DIR}/src/parser.c"
    "${tree-sitter-css_SOURCE_DIR}/src/scanner.c"
)
target_include_directories(tree-sitter-css-grammar PRIVATE "${tree-sitter-css_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-css-grammar PRIVATE tree-sitter)

# ---- tree-sitter-bash grammar (Phase 7r) -----------------------------------
# neomifes::syntax's Language::Shell - upstream repo/grammar name is "bash"
# (covers sh/bash scripts generically), roadmap sec.7.2 calls the language
# "Shell". Same SOURCE_SUBDIR workaround. Has scanner.c (confirmed via
# GitHub API).
FetchContent_Declare(
    tree-sitter-bash
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-bash.git
    GIT_TAG        v0.25.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-bash)

add_library(tree-sitter-bash-grammar STATIC
    "${tree-sitter-bash_SOURCE_DIR}/src/parser.c"
    "${tree-sitter-bash_SOURCE_DIR}/src/scanner.c"
)
target_include_directories(tree-sitter-bash-grammar PRIVATE "${tree-sitter-bash_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-bash-grammar PRIVATE tree-sitter)

# ---- tree-sitter-yaml grammar (Phase 7r) -----------------------------------
# From tree-sitter-grammars/ (the org tree-sitter migrated many community
# grammars into), not tree-sitter/ - confirmed to exist and MIT-licensed via
# GitHub API before adding this block. Same SOURCE_SUBDIR workaround. Has
# scanner.c PLUS three extra schema-validation source files (schema.core.c/
# schema.json.c/schema.legacy.c, confirmed via GitHub API) - all four are
# compiled in since parser.c references symbols from all of them (schema
# validation is baked into the grammar's external scanner, not an optional
# add-on).
FetchContent_Declare(
    tree-sitter-yaml
    GIT_REPOSITORY https://github.com/tree-sitter-grammars/tree-sitter-yaml.git
    GIT_TAG        v0.7.2
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-yaml)

add_library(tree-sitter-yaml-grammar STATIC
    "${tree-sitter-yaml_SOURCE_DIR}/src/parser.c"
    "${tree-sitter-yaml_SOURCE_DIR}/src/scanner.c"
    "${tree-sitter-yaml_SOURCE_DIR}/src/schema.core.c"
    "${tree-sitter-yaml_SOURCE_DIR}/src/schema.json.c"
    "${tree-sitter-yaml_SOURCE_DIR}/src/schema.legacy.c"
)
target_include_directories(tree-sitter-yaml-grammar PRIVATE "${tree-sitter-yaml_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-yaml-grammar PRIVATE tree-sitter)

# ---- tree-sitter-toml grammar (Phase 7r) -----------------------------------
# From tree-sitter-grammars/. Same SOURCE_SUBDIR workaround. Has scanner.c
# (confirmed via GitHub API).
FetchContent_Declare(
    tree-sitter-toml
    GIT_REPOSITORY https://github.com/tree-sitter-grammars/tree-sitter-toml.git
    GIT_TAG        v0.7.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-toml)

add_library(tree-sitter-toml-grammar STATIC
    "${tree-sitter-toml_SOURCE_DIR}/src/parser.c"
    "${tree-sitter-toml_SOURCE_DIR}/src/scanner.c"
)
target_include_directories(tree-sitter-toml-grammar PRIVATE "${tree-sitter-toml_SOURCE_DIR}/src")
target_link_libraries(tree-sitter-toml-grammar PRIVATE tree-sitter)

# ---- tree-sitter-xml grammar (Phase 7r) ------------------------------------
# From tree-sitter-grammars/. This repo hosts TWO grammars side by side
# (xml/ and dtd/, confirmed via GitHub API) - only xml/ is used here (dtd/
# is a separate, much less commonly needed schema language, deliberately
# out of scope). Source paths therefore include the xml/ subdirectory,
# unlike every other grammar block above whose single-grammar repos put
# src/ at the repo root. Has scanner.c (confirmed via GitHub API).
FetchContent_Declare(
    tree-sitter-xml
    GIT_REPOSITORY https://github.com/tree-sitter-grammars/tree-sitter-xml.git
    GIT_TAG        v0.7.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-xml)

add_library(tree-sitter-xml-grammar STATIC
    "${tree-sitter-xml_SOURCE_DIR}/xml/src/parser.c"
    "${tree-sitter-xml_SOURCE_DIR}/xml/src/scanner.c"
)
target_include_directories(tree-sitter-xml-grammar PRIVATE "${tree-sitter-xml_SOURCE_DIR}/xml/src")
target_link_libraries(tree-sitter-xml-grammar PRIVATE tree-sitter)

# ---- tree-sitter-typescript / tree-sitter-tsx grammars (Phase 7s) ---------
# One repo hosts TWO independent, complete grammars side by side (typescript/
# for .ts, tsx/ for .tsx with JSX support) - unlike PHP/Markdown below, this
# is not a "pick the primary one" situation: both are used, selected by file
# extension (detectLanguage()). Each scanner.c references the repo-root
# common/scanner.h via a relative #include ("../../common/scanner.h",
# confirmed via the real source file) - no extra target_include_directories
# entry is needed for that (the relative include resolves on its own, same
# as upstream's own CMakeLists.txt does not add common/ as an include dir).
FetchContent_Declare(
    tree-sitter-typescript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-typescript.git
    GIT_TAG        v0.23.2
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-typescript)

add_library(tree-sitter-typescript-grammar STATIC
    "${tree-sitter-typescript_SOURCE_DIR}/typescript/src/parser.c"
    "${tree-sitter-typescript_SOURCE_DIR}/typescript/src/scanner.c"
)
target_include_directories(tree-sitter-typescript-grammar PRIVATE
    "${tree-sitter-typescript_SOURCE_DIR}/typescript/src")
target_link_libraries(tree-sitter-typescript-grammar PRIVATE tree-sitter)

add_library(tree-sitter-tsx-grammar STATIC
    "${tree-sitter-typescript_SOURCE_DIR}/tsx/src/parser.c"
    "${tree-sitter-typescript_SOURCE_DIR}/tsx/src/scanner.c"
)
target_include_directories(tree-sitter-tsx-grammar PRIVATE
    "${tree-sitter-typescript_SOURCE_DIR}/tsx/src")
target_link_libraries(tree-sitter-tsx-grammar PRIVATE tree-sitter)

# ---- tree-sitter-php grammar (Phase 7s) ------------------------------------
# This repo hosts TWO grammars (php/ and php_only/, confirmed via GitHub
# API) - only php/ (the full grammar, including <?php ?> tag handling and
# embedded HTML) is used here. php_only/ is meant for embedding PHP inside
# another host grammar's injection, not for standalone .php files, so unlike
# TypeScript/TSX above there is no ambiguity to resolve: php/ is simply the
# only correct choice for opening real .php files.
FetchContent_Declare(
    tree-sitter-php
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-php.git
    GIT_TAG        v0.24.2
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-php)

add_library(tree-sitter-php-grammar STATIC
    "${tree-sitter-php_SOURCE_DIR}/php/src/parser.c"
    "${tree-sitter-php_SOURCE_DIR}/php/src/scanner.c"
)
target_include_directories(tree-sitter-php-grammar PRIVATE "${tree-sitter-php_SOURCE_DIR}/php/src")
target_link_libraries(tree-sitter-php-grammar PRIVATE tree-sitter)

# ---- tree-sitter-markdown grammar (Phase 7s) -------------------------------
# This repo hosts TWO grammars, but unlike php/php_only these are NOT
# alternatives to pick between - tree-sitter-markdown (block-level: headings/
# lists/code blocks/...) is meant to have tree-sitter-markdown-inline
# (emphasis/links/inline code/...) injected into its paragraph text nodes via
# tree-sitter's language-injection mechanism (the standard pattern used by
# e.g. nvim-treesitter). neomifes::syntax has no injection mechanism (single
# TSParser + single grammar per parse, confirmed by reading syntax_internal.h/
# incremental_parser.cpp) and adding one is out of scope here (CLAUDE.md rule
# 10 - no speculative complexity without a concrete need) - so only the block
# grammar is wired up. Inline formatting inside paragraphs stays unstyled,
# an accepted gap of the same kind as HTML's raw_text or CSS's plain_value.
FetchContent_Declare(
    tree-sitter-markdown
    GIT_REPOSITORY https://github.com/tree-sitter-grammars/tree-sitter-markdown.git
    GIT_TAG        v0.5.3
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "does-not-exist"
)
FetchContent_MakeAvailable(tree-sitter-markdown)

add_library(tree-sitter-markdown-grammar STATIC
    "${tree-sitter-markdown_SOURCE_DIR}/tree-sitter-markdown/src/parser.c"
    "${tree-sitter-markdown_SOURCE_DIR}/tree-sitter-markdown/src/scanner.c"
)
target_include_directories(tree-sitter-markdown-grammar PRIVATE
    "${tree-sitter-markdown_SOURCE_DIR}/tree-sitter-markdown/src")
target_link_libraries(tree-sitter-markdown-grammar PRIVATE tree-sitter)

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
# tree-sitter-{c,javascript,java,go,rust,json}-grammar (Phase 7n1) are too.
# tree-sitter-{html,css,bash,yaml,toml,xml}-grammar (Phase 7r) are too.
# tree-sitter-{typescript,tsx,php,markdown}-grammar (Phase 7s) are too.
neomifes_collect_targets_recursive(_neomifes_absl_targets "${abseil-cpp_SOURCE_DIR}")
foreach(_tp ${_neomifes_absl_targets} re2 nlohmann_json tree-sitter tree-sitter-cpp-grammar tree-sitter-python-grammar
        tree-sitter-c-grammar tree-sitter-javascript-grammar tree-sitter-java-grammar tree-sitter-go-grammar
        tree-sitter-rust-grammar tree-sitter-json-grammar tree-sitter-html-grammar tree-sitter-css-grammar
        tree-sitter-bash-grammar tree-sitter-yaml-grammar tree-sitter-toml-grammar tree-sitter-xml-grammar
        tree-sitter-typescript-grammar tree-sitter-tsx-grammar tree-sitter-php-grammar tree-sitter-markdown-grammar)
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
