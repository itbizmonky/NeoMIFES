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
using neomifes::document::TextRange;
using neomifes::render::CursorVisual;
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

    // First render(): no cached snapshot yet, must fetch and draw. 3 lines,
    // each a first-time TextLayoutCache miss.
    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());
    EXPECT_EQ(pipeline.layoutCacheStats().misses, 3U);

    // Second render(): Document/topLine/size all unchanged - exercises the
    // cached-snapshot reuse path in refreshDocumentCacheIfStale() (version()
    // unchanged). Whether this also short-circuits via the frame-skip fast
    // path (Phase 3c) or genuinely re-walks and hits the layout cache is an
    // implementation detail either way misses must not increase - see the
    // dedicated frame-skip test below for the stricter "stats completely
    // frozen" assertion that distinguishes the two.
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() failed: " << neomifes::render::describe(second.error());
    EXPECT_EQ(pipeline.layoutCacheStats().misses, 3U);

    // Mutate, then render() again - exercises the version-bump -> re-fetch
    // path, which also wholesale-invalidates the layout cache (ADR-011).
    doc.insertText(0, u"prepended\n");
    const auto missesBeforeThird = pipeline.layoutCacheStats().misses;
    const auto third = pipeline.render();
    EXPECT_TRUE(third.has_value())
        << "third render() (after mutation) failed: " << neomifes::render::describe(third.error());
    EXPECT_GT(pipeline.layoutCacheStats().misses, missesBeforeThird);
}

TEST(RenderTextSmokeTest, RepeatedRenderOfUnchangedLineIsLayoutCacheHitNotMiss) {
    // Distinct from the test above: this scrolls (changes topLine) between
    // renders specifically to avoid the frame-skip fast path (Phase 3c)
    // short-circuiting before drawVisibleLines() runs, so it can assert on
    // genuine TextLayoutCache hit/miss behavior for a line whose content is
    // revisited after a real state change.
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2\nline3\nline4");
    pipeline.setDocument(&doc);

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());
    const auto missesAfterFirst = pipeline.layoutCacheStats().misses;
    ASSERT_GT(missesAfterFirst, 0U);

    // Scroll down by one line: every line still visible after the scroll
    // (line1 onward) was already cached by the first render, so this frame
    // must be all TextLayoutCache hits for those lines - misses should not
    // grow past whatever new line (if any) scrolls into view for the first
    // time, and hits must increase.
    pipeline.setTopLine(1);
    const auto hitsBeforeSecond = pipeline.layoutCacheStats().hits;
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() (after scroll) failed: " << neomifes::render::describe(second.error());
    EXPECT_GT(pipeline.layoutCacheStats().hits, hitsBeforeSecond);
}

TEST(RenderTextSmokeTest, IdenticalStateRenderSkipsEntirelyButChangedStateDoesNot) {
    // Distinguishes the coarse frame-skip fast path (Phase 3c, ADR-011) from
    // "redraws every time but hits the layout cache": only a genuine skip
    // leaves BOTH hits and misses completely frozen. A full redraw with all
    // cache hits would still show misses==0 but hits growing - that's not
    // what this test checks for on the repeat call.
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2");
    pipeline.setDocument(&doc);

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());
    const auto statsAfterFirst = pipeline.layoutCacheStats();

    // Nothing changed (Document, topLine, size, DPI all identical) - must be
    // a complete skip, so the cache stats must not move at all.
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() (unchanged state) failed: "
        << neomifes::render::describe(second.error());
    EXPECT_EQ(pipeline.layoutCacheStats().hits, statsAfterFirst.hits);
    EXPECT_EQ(pipeline.layoutCacheStats().misses, statsAfterFirst.misses);

    // topLine moved -> FrameState differs -> must NOT skip, so stats move
    // again (some combination of new hits/misses, don't care which exactly -
    // just that the frozen state from the skip above is over).
    pipeline.setTopLine(1);
    const auto third = pipeline.render();
    ASSERT_TRUE(third.has_value())
        << "third render() (after scroll) failed: " << neomifes::render::describe(third.error());
    const auto statsAfterThird = pipeline.layoutCacheStats();
    EXPECT_TRUE(statsAfterThird.hits != statsAfterFirst.hits ||
                statsAfterThird.misses != statsAfterFirst.misses);
}

TEST(RenderTextSmokeTest, CaretOnlyMovementForcesRedrawInsteadOfFrameSkip) {
    // Phase 4b1: FrameState now includes caretPosition specifically so that
    // moving the caret alone (Document/topLine/size/DPI all unchanged) is
    // not swallowed by the Phase 3c coarse frame-skip. Same technique as
    // IdenticalStateRenderSkipsEntirelyButChangedStateDoesNot above: a
    // genuine skip leaves layout-cache stats completely frozen, so a caret
    // move that still shows movement here proves the skip did NOT trigger.
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2");
    pipeline.setDocument(&doc);
    pipeline.setCursorVisuals({CursorVisual{.position = 0}});

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());
    const auto statsAfterFirst = pipeline.layoutCacheStats();

    // Move the caret only - everything else stays identical.
    pipeline.setCursorVisuals({CursorVisual{.position = 3}});
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() (caret moved) failed: " << neomifes::render::describe(second.error());
    const auto statsAfterSecond = pipeline.layoutCacheStats();
    EXPECT_TRUE(statsAfterSecond.hits != statsAfterFirst.hits ||
                statsAfterSecond.misses != statsAfterFirst.misses)
        << "caret-only movement was frame-skipped instead of triggering a redraw";
}

TEST(RenderTextSmokeTest, HitTestReturnsPositionsWithinKnownLineBounds) {
    // Phase 4b2: RenderPipeline::hitTest() is the first HitTestPoint use in
    // this codebase - no exact pixel-to-column round trip is asserted here
    // (that depends on Consolas' actual glyph advance widths, which this
    // test doesn't control), only that clicks land within plausible bounds
    // for a short, known line.
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"ab\ncd");
    pipeline.setDocument(&doc);
    const auto rendered = pipeline.render();  // populates the layout cache
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    // x=0 on line 0 should land at or near the very start of the document.
    const auto lineStartHit = pipeline.hitTest(0, 0);
    ASSERT_TRUE(lineStartHit.has_value());
    EXPECT_LE(*lineStartHit, 2U);  // within "ab"

    // A far-right x on line 0 should land at or before the end of "ab" (2),
    // never past it (NO_WRAP text can't hit-test into the next line via x).
    const auto lineEndHit = pipeline.hitTest(10000, 0);
    ASSERT_TRUE(lineEndHit.has_value());
    EXPECT_LE(*lineEndHit, 2U);

    // A y coordinate one line height down should land on line 1 ("cd",
    // offset 3-5), not line 0.
    const auto secondLineHit = pipeline.hitTest(0, 20);
    ASSERT_TRUE(secondLineHit.has_value());
    EXPECT_GE(*secondLineHit, 3U);
}

TEST(RenderTextSmokeTest, SelectionRangeRendersWithoutErrorAndForcesRedraw) {
    // Same frame-skip technique as CaretOnlyMovementForcesRedrawInsteadOfFrameSkip
    // above, applied to setSelectionRange() (Phase 4b2). Pixel-level
    // highlight correctness is out of scope (existing project policy - see
    // file header).
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2");
    pipeline.setDocument(&doc);

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());
    const auto statsAfterFirst = pipeline.layoutCacheStats();

    pipeline.setCursorVisuals(
        {CursorVisual{.position = 4, .selectionRange = TextRange{.start = 0, .end = 4}}});
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() (selection set) failed: " << neomifes::render::describe(second.error());
    const auto statsAfterSecond = pipeline.layoutCacheStats();
    EXPECT_TRUE(statsAfterSecond.hits != statsAfterFirst.hits ||
                statsAfterSecond.misses != statsAfterFirst.misses)
        << "selection-only change was frame-skipped instead of triggering a redraw";
}

TEST(RenderTextSmokeTest, MultipleCursorVisualsRenderWithoutErrorAndForceRedraw) {
    // Phase 4b7a: setCursorVisuals() replaces the old single-caret/single-
    // selection setters with a full vector, so every SelectionModel cursor
    // (not just the primary) gets drawn. Exercises 3 cursors spread across
    // different lines, some with a selection and some without, mirroring
    // what Alt+click multi-cursor produces. Same "render() succeeds, and
    // changing cursor state alone isn't frame-skipped" scope as the single-
    // cursor tests above - pixel-level highlight correctness is out of
    // scope (see file header).
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2");
    pipeline.setDocument(&doc);
    pipeline.setCursorVisuals({CursorVisual{.position = 0}});

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());
    const auto statsAfterFirst = pipeline.layoutCacheStats();

    // 3 cursors: one plain caret on line 0, one with a selection spanning
    // line 1, one plain caret on line 2 - only this vector changes.
    pipeline.setCursorVisuals({
        CursorVisual{.position = 2},
        CursorVisual{.position = 11, .selectionRange = TextRange{.start = 6, .end = 11}},
        CursorVisual{.position = 17},
    });
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() (3 cursors) failed: " << neomifes::render::describe(second.error());
    const auto statsAfterSecond = pipeline.layoutCacheStats();
    EXPECT_TRUE(statsAfterSecond.hits != statsAfterFirst.hits ||
                statsAfterSecond.misses != statsAfterFirst.misses)
        << "multi-cursor change was frame-skipped instead of triggering a redraw";
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
