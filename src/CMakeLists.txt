add_subdirectory(util)
# neomifes_encoding has no dependency on document:: (Phase 6a) - placed
# before document per the layer diagram (Encoding Engine sits below Document
# Engine, CLAUDE.md sec.3); document:: gains a dependency on it once file
# loading becomes encoding-aware (Phase 6d+).
add_subdirectory(encoding)
add_subdirectory(platform)
add_subdirectory(document)
add_subdirectory(core)
# neomifes_search links re2::re2 (unconditionally fetched, see root
# CMakeLists.txt / cmake/Dependencies.cmake). Unconditional since Phase 5b3a:
# src/app/CMakeLists.txt's NeoMIFES target links neomifes::search directly
# (Find bar UI), not just tests/bench.
add_subdirectory(search)
add_subdirectory(render)
add_subdirectory(ui)
add_subdirectory(app)
