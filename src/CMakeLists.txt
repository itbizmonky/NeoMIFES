add_subdirectory(util)
add_subdirectory(platform)
add_subdirectory(document)
add_subdirectory(core)
if(NEOMIFES_BUILD_TESTS)
    # neomifes_search links re2::re2, which is only fetched when
    # NEOMIFES_BUILD_TESTS=ON (see root CMakeLists.txt) - see that comment
    # for why. Only tests/bench consume this module today.
    add_subdirectory(search)
endif()
add_subdirectory(render)
add_subdirectory(ui)
add_subdirectory(app)
