add_subdirectory(util)
# neomifes_encoding has no dependency on document:: - placed before document
# per the layer diagram (Encoding Engine sits below Document Engine,
# CLAUDE.md sec.3); document:: gains a dependency on it once file loading
# becomes encoding-aware (Phase 6d+). It does depend on neomifes::platform
# (Phase 6b1, codepage_convert.h) - platform:: is a foundational Win32-facade
# layer with no upward dependencies of its own (peer of util::, not part of
# the layered engine stack), so this doesn't violate the layer direction.
# add_subdirectory() call order here doesn't need to match dependency order -
# CMake resolves target_link_libraries() across subdirectories regardless.
add_subdirectory(encoding)
add_subdirectory(platform)
add_subdirectory(document)
# neomifes_syntax (Phase 7a, ADR-014) depends on neomifes::document (TextRange/
# TextPos only) and links the tree-sitter/tree-sitter-cpp-grammar targets
# declared in cmake/Dependencies.cmake - placed after document per the same
# "declare after what it depends on" convention as encoding/platform above,
# though add_subdirectory() order itself doesn't affect CMake's cross-directory
# target_link_libraries() resolution.
add_subdirectory(syntax)
add_subdirectory(core)
# neomifes_search links re2::re2 (unconditionally fetched, see root
# CMakeLists.txt / cmake/Dependencies.cmake). Unconditional since Phase 5b3a:
# src/app/CMakeLists.txt's NeoMIFES target links neomifes::search directly
# (Find bar UI), not just tests/bench.
add_subdirectory(search)
add_subdirectory(render)
add_subdirectory(ui)
add_subdirectory(app)
