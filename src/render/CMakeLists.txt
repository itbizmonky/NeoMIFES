add_library(neomifes_render STATIC
    src/d2d_factories.cpp
    src/render_device.cpp
    src/render_error.cpp
    src/render_pipeline.cpp
)
add_library(neomifes::render ALIAS neomifes_render)

target_include_directories(neomifes_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(neomifes_render
    PUBLIC
        # render_pipeline.h/render_device.h expose HWND and D2D1_COLOR_F in
        # their public API, and a future frame-timing member will reuse
        # PerfClock (its doc comment earmarks exactly this reuse).
        neomifes::platform
    PRIVATE
        neomifes::compile_options
        d2d1
        dwrite
        dxgi
        d3d11
)

set_target_properties(neomifes_render PROPERTIES
    FOLDER "src"
)
