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
        # render_pipeline.h exposes document::Document*/document::LineNumber
        # in its public API (RenderPipeline::setDocument/setTopLine). See
        # ADR-010 for why Rendering Engine depends directly on Document
        # Engine rather than routing through the app layer.
        neomifes::document
    PRIVATE
        neomifes::compile_options
        # wchar_cast.h (u16string_view -> WCHAR* for DrawText)
        neomifes::util
        d2d1
        dwrite
        dxgi
        d3d11
)

set_target_properties(neomifes_render PROPERTIES
    FOLDER "src"
)
