add_library(neomifes_git STATIC
    src/git_init.cpp
    src/git_repository.cpp
    src/git_diff_worker.cpp
)
add_library(neomifes::git ALIAS neomifes_git)

target_include_directories(neomifes_git
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        # WI-17a (ADR-022): libgit2package exposes its headers only via
        # INSTALL_INTERFACE, not BUILD_INTERFACE (unlike RE2/nlohmann_json's
        # clean PUBLIC exports) - target_link_libraries() alone does NOT make
        # <git2.h> visible, hence this explicit entry. PRIVATE (not PUBLIC)
        # because neomifes::git's own public headers (git_init.h and
        # friends) never expose a libgit2 type in their own signatures -
        # callers outside this module never need <git2.h> themselves.
        "${libgit2_SOURCE_DIR}/include"
        "${libgit2_BINARY_DIR}/include"  # generated headers (git2/sys/... version macros etc.)
)

target_link_libraries(neomifes_git
    PUBLIC
        # git_repository.h exposes document::LineNumber in LineDiffRegion's
        # public API and takes a document::Document& in diffAgainstHead(),
        # same reasoning as neomifes::logmode/neomifes::jsontree/
        # neomifes::csvmode's identical PUBLIC neomifes::document.
        neomifes::document
    PRIVATE
        neomifes::compile_options
        neomifes::util
        libgit2package
)

set_target_properties(neomifes_git PROPERTIES
    FOLDER "src"
)
