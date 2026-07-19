add_subdirectory(util)
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
