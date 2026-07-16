// Integration test (not a unit test): exercises RenderPipeline::render()
// actually drawing Document content via DirectWrite, on top of the real
// COM/D3D11/D2D/DXGI device graph (render_device_smoke_test.cpp covers the
// device graph itself in isolation).
//
// LIMITATION: there is no pixel-capture mechanism in this codebase yet (that
// is Phase 3c/measurement-harness territory), so these tests only prove
// "render() succeeds and doesn't crash/error" for a Document with real
// content - not "the glyphs drawn are visually correct". Don't read more
// coverage into this file than that.

#include <gtest/gtest.h>

#include <windows.h>

#include "neomifes/document/document.h"
#include "neomifes/render/render_error.h"
#include "neomifes/render/render_pipeline.h"

namespace {

using neomifes::document::Document;
using neomifes::render::RenderPipeline;

// RAII helper so every TEST body doesn't repeat the hidden-window dance.
class HiddenWindow {
public:
    HiddenWindow() {
        m_hwnd = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 200, 100, nullptr, nullptr,
                                   nullptr, nullptr);
    }
    ~HiddenWindow() {
        if (m_hwnd != nullptr) {
            ::DestroyWindow(m_hwnd);
        }
    }
    HiddenWindow(const HiddenWindow&)            = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;

    [[nodiscard]] HWND get() const noexcept { return m_hwnd; }

private:
    HWND m_hwnd = nullptr;
};

TEST(RenderTextSmokeTest, RendersDocumentContentAcrossVersionChanges) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"first line\nsecond line\nthird line");
    pipeline.setDocument(&doc);

    // First render(): no cached snapshot yet, must fetch and draw.
    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());

    // Second render(): Document unchanged - exercises the cached-snapshot
    // reuse path in refreshDocumentCacheIfStale() (version() unchanged).
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() failed: " << neomifes::render::describe(second.error());

    // Mutate, then render() again - exercises the version-bump -> re-fetch path.
    doc.insertText(0, u"prepended\n");
    const auto third = pipeline.render();
    EXPECT_TRUE(third.has_value())
        << "third render() (after mutation) failed: " << neomifes::render::describe(third.error());
}

TEST(RenderTextSmokeTest, RendersWithoutDocumentAttached) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    // setDocument() never called - render() should just clear the background.
    const auto result = pipeline.render();
    EXPECT_TRUE(result.has_value())
        << "render() with no Document attached failed: "
        << neomifes::render::describe(result.error());
}

}  // namespace
