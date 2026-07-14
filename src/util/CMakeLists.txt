add_library(neomifes_util STATIC
    src/version.cpp
)
add_library(neomifes::util ALIAS neomifes_util)

target_include_directories(neomifes_util
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_util
    PRIVATE
        neomifes::compile_options
)

set_target_properties(neomifes_util PROPERTIES
    FOLDER "src"
)
