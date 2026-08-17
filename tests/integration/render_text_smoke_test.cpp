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

#include <memory>
#include <string>

#include "neomifes/document/document.h"
#include "neomifes/logmode/log_pattern.h"
#include "neomifes/render/render_error.h"
#include "neomifes/render/render_pipeline.h"
#include "neomifes/render/theme.h"

namespace {

using neomifes::document::Document;
using neomifes::document::TextRange;
using neomifes::logmode::LogLevel;
using neomifes::logmode::logLevelFilterBit;
using neomifes::render::CursorVisual;
using neomifes::render::ImeComposition;
using neomifes::render::RenderPipeline;
using neomifes::render::ThemeKind;
using neomifes::syntax::Language;

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

// WI-02 dogfooding bug fix: a document SWAP (setDocument() to a brand-new
// Document, as openAndResetTo()/Ctrl+O do) whose new Document's version()
// coincidentally matches the previous cached value must still force a
// redraw. Both documents here get exactly one insertText() call, so both
// sit at version()==1 - without m_documentGeneration (see
// RenderPipeline::setLanguage()'s comment), every other FrameState field
// would also be unchanged (topLine/cursor/matches/bookmarks/folds all at
// their defaults), and the Phase 3c coarse frame-skip would incorrectly
// treat the second document's content as never having been drawn. Same
// "stats must move" technique as CaretOnlyMovementForcesRedrawInsteadOfFrameSkip
// above.
TEST(RenderTextSmokeTest, DocumentSwapWithCoincidentallyMatchingVersionForcesRedraw) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document firstDoc;
    firstDoc.insertText(0, u"line0\nline1\nline2");
    ASSERT_EQ(firstDoc.version(), 1U);
    pipeline.setDocument(&firstDoc);
    pipeline.setLanguage(Language::Cpp);  // mirrors openAndResetTo()'s unconditional call

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());
    const auto statsAfterFirst = pipeline.layoutCacheStats();

    // A brand-new Document, one insertText() call, so version()==1 again -
    // the exact coincidence this test exists to guard against - but with
    // DIFFERENT content, proving this isn't the same document being
    // re-rendered by chance.
    Document secondDoc;
    secondDoc.insertText(0, u"AAAA\nBBBB\nCCCC");
    ASSERT_EQ(secondDoc.version(), 1U);
    pipeline.setDocument(&secondDoc);
    pipeline.setLanguage(Language::Cpp);  // mirrors openAndResetTo()'s unconditional call

    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() (after document swap) failed: " << neomifes::render::describe(second.error());
    const auto statsAfterSecond = pipeline.layoutCacheStats();
    EXPECT_TRUE(statsAfterSecond.hits != statsAfterFirst.hits ||
                statsAfterSecond.misses != statsAfterFirst.misses)
        << "document swap with a coincidentally-matching version() was frame-skipped instead of "
           "triggering a redraw";
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

    // A y coordinate past the Breadcrumb strip (Phase 7h: hitTest() now
    // clamps/subtracts kBreadcrumbHeightDips before converting to a row -
    // see hitTest()'s comment) plus one line height should land on line 1
    // ("cd", offset 3-5), not line 0.
    const auto secondLineHit = pipeline.hitTest(0, 50);
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

TEST(RenderTextSmokeTest, SyntaxHighlightingEnabledRendersWithoutError) {
    // Phase 7b: exercises the refreshDocumentCacheIfStale() -> parseCpp() ->
    // drawTokensOnLine()/SetDrawingEffect() path end-to-end with real C++
    // content (keyword, identifier, string, number, comment, preprocessor
    // directive all present) - same "render() succeeds, no pixel-level
    // assertion" scope as the rest of this file (see file header).
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"#include <cstdint>\n// leading comment\nint main() { return 42; }\n");
    pipeline.setDocument(&doc);
    pipeline.setLanguage(Language::Cpp);

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() with syntax highlighting enabled failed: "
        << neomifes::render::describe(rendered.error());
}

// Phase 7l: exercises refreshDocumentCacheIfStale()'s new
// takePendingEdits() -> SyntaxWorker::requestParse() wiring end-to-end after
// a REAL edit (not just the initial full parse the test above covers) -
// same "render() succeeds, no pixel-level assertion, no waiting for the
// async result" scope as the rest of this file. Correctness of the
// delivered tokens themselves (accumulation, reset-on-document-switch) is
// covered by render_syntax_worker_test.cpp's deeper, pumped tests; this one
// only proves the render()-driven path doesn't crash or return an error.
TEST(RenderTextSmokeTest, SyntaxHighlightingRendersWithoutErrorAfterAnEdit) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"int x = 1;\n");
    pipeline.setDocument(&doc);
    pipeline.setLanguage(Language::Cpp);

    const auto firstRender = pipeline.render();
    ASSERT_TRUE(firstRender.has_value())
        << "initial render() failed: " << neomifes::render::describe(firstRender.error());

    doc.insertText(doc.length(), u"int y = 2;\n");
    const auto secondRender = pipeline.render();
    ASSERT_TRUE(secondRender.has_value())
        << "render() after an edit failed: " << neomifes::render::describe(secondRender.error());
}

// Phase 7t: exercises ensureSyntaxTokensCoverVisibleRange()'s scroll-driven
// trigger - a plain scroll (setTopLine(), no document edit at all) into an
// area outside whatever narrow range the first render() requested must
// still complete render() without error. Correctness of what tokens
// SyntaxWorker eventually delivers is covered by
// render_syntax_worker_test.cpp/syntax_incremental_parser_test.cpp; this
// one only proves the render()-driven scroll path doesn't crash or return
// an error, same "render() succeeds, no pixel-level assertion, no waiting
// for the async result" scope as the rest of this file.
TEST(RenderTextSmokeTest, ScrollingFarBeyondTheInitiallyRequestedRangeStillRendersWithoutError) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document      doc;
    std::u16string content;
    for (int i = 0; i < 500; ++i) {
        content += u"int line";
        content += static_cast<char16_t>(u'0' + (i % 10));
        content += u" = 0;\n";
    }
    doc.insertText(0, content);
    pipeline.setDocument(&doc);
    pipeline.setLanguage(Language::Cpp);

    const auto firstRender = pipeline.render();
    ASSERT_TRUE(firstRender.has_value())
        << "initial render() failed: " << neomifes::render::describe(firstRender.error());

    // No document edit at all - purely a scroll far past whatever narrow
    // range the first render() requested (this window is 200x100px, so the
    // initially visible+margin window is well under 500 lines).
    pipeline.setTopLine(400);
    const auto secondRender = pipeline.render();
    ASSERT_TRUE(secondRender.has_value())
        << "render() after scrolling into an uncovered range failed: "
        << neomifes::render::describe(secondRender.error());
}

// WI-06: exercises setImeComposition() -> render() -> drawImeCompositionOnLine()
// end-to-end with a real in-progress composition (target clause set) - same
// "render() succeeds, no pixel-level assertion" scope as the rest of this
// file (see file header). Live MS-IME behavior (candidate window position,
// underline/highlight correctness) is verified manually (WI-06 step 4), not
// here.
TEST(RenderTextSmokeTest, ImeCompositionRendersWithoutError) {
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

    pipeline.setImeComposition(ImeComposition{
        .anchorRange       = TextRange{.start = 6, .end = 6},  // start of "line1"
        .text              = u"にほんご",      // "にほんご"
        .targetClauseRange = std::pair<std::uint32_t, std::uint32_t>(0, 2),
    });

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() with an in-progress IME composition failed: "
        << neomifes::render::describe(rendered.error());
}

// WI-06 regression test, same technique as
// CaretOnlyMovementForcesRedrawInsteadOfFrameSkip above: FrameState::
// imeComposition exists specifically so that a composition-only state
// change (Document/topLine/cursor/etc. all unchanged) is not swallowed by
// the Phase 3c coarse frame-skip - without it, typing the FIRST character
// of a composition (with the caret not otherwise moving) would silently
// fail to redraw.
TEST(RenderTextSmokeTest, ImeCompositionOnlyChangeForcesRedrawInsteadOfFrameSkip) {
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

    // Composition only - Document/topLine/cursor/etc. all stay identical.
    pipeline.setImeComposition(ImeComposition{
        .anchorRange       = TextRange{.start = 0, .end = 0},
        .text              = u"あ",  // "あ"
        .targetClauseRange = std::nullopt,
    });
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() (composition set) failed: " << neomifes::render::describe(second.error());
    const auto statsAfterSecond = pipeline.layoutCacheStats();
    EXPECT_TRUE(statsAfterSecond.hits != statsAfterFirst.hits ||
                statsAfterSecond.misses != statsAfterFirst.misses)
        << "IME composition-only change was frame-skipped instead of triggering a redraw";
}

// Phase 7d: same shape as the C++ case above, confirming setLanguage()'s
// generalized parameter actually reaches Python parsing/coloring end-to-end
// (not just that it compiles) - render_syntax_worker_test.cpp already covers
// the worker-thread dispatch itself in isolation; this exercises the whole
// RenderPipeline path (refreshDocumentCacheIfStale() -> requestParse() ->
// applyAsyncSyntaxTokens() -> drawTokensOnLine()) with real Python content.
TEST(RenderTextSmokeTest, PythonSyntaxHighlightingRendersWithoutError) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"# leading comment\ndef foo(x):\n    return x + 1\n");
    pipeline.setDocument(&doc);
    pipeline.setLanguage(Language::Python);

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() with Python syntax highlighting enabled failed: "
        << neomifes::render::describe(rendered.error());
}

// Phase 7e: exercises drawIndentGuidesOnLine() end-to-end with a multi-level
// nested snippet (several distinct indent-guide levels) and a cursor placed
// on one of the indented lines, so both the regular and active-line brush
// paths run. Same "render() succeeds, no pixel-level assertion" scope as the
// rest of this file.
TEST(RenderTextSmokeTest, IndentGuidesRenderWithoutError) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"void f() {\n    if (x) {\n        y();\n    }\n}\n");
    pipeline.setDocument(&doc);
    // Cursor on the deepest-indented line ("        y();") so the active-
    // guide brush path is exercised too.
    pipeline.setCursorVisuals({CursorVisual{.position = 25}});

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() with indent guides failed: " << neomifes::render::describe(rendered.error());
}

// Phase 7h: exercises drawBreadcrumb() end-to-end - a nested namespace/class/
// method C++ snippet, a primary cursor placed inside the innermost method
// body (so refreshDocumentCacheIfStale()'s synchronous extractOutline() +
// findBreadcrumbPath() actually resolve a non-empty 3-level path), confirming
// the whole path renders without error. Same "render() succeeds, no pixel-
// level assertion" scope as the rest of this file.
TEST(RenderTextSmokeTest, BreadcrumbRendersWithoutError) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0,
                    u"namespace outer {\n"
                    u"    class Widget {\n"
                    u"    public:\n"
                    u"        int getValue() { return 0; }\n"
                    u"    };\n"
                    u"}\n");
    pipeline.setDocument(&doc);
    pipeline.setLanguage(Language::Cpp);
    // Position inside getValue()'s body ("return 0") - deep enough to land
    // inside all three nesting levels (outer/Widget/getValue).
    pipeline.setCursorVisuals({CursorVisual{.position = 78, .isPrimary = true}});

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() with Breadcrumb failed: " << neomifes::render::describe(rendered.error());
}

// Phase 7i: exercises the drawVisibleLines()/hitTest() hidden-line-skipping
// paths end-to-end with one folded region - confirms render() still succeeds
// (including the folded-header "{...}" marker draw) and that hitTest() keeps
// resolving to a line that was actually drawn (never a hidden one). Same
// "render() succeeds, no pixel-level assertion" scope as the rest of this
// file.
TEST(RenderTextSmokeTest, FoldedRegionRendersWithoutErrorAndHitTestSkipsHiddenLines) {
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
    // Folds lines 1-2 (header line 0 stays visible, endLineInclusive=2).
    pipeline.setFoldRegions({neomifes::render::FoldVisual{
        .headerLine = 0, .endLineInclusive = 2, .folded = true}});

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() with a folded region failed: " << neomifes::render::describe(rendered.error());

    // A click below the header line's row must land on line3 (offset 18-23),
    // never on the hidden line1/line2, since hitTest() only ever walks lines
    // drawVisibleLines() actually drew.
    const auto belowFold = pipeline.hitTest(0, 50);
    ASSERT_TRUE(belowFold.has_value());
    EXPECT_GE(*belowFold, 18U);
}

// Phase 7j: exercises hitTestFoldMarker() end-to-end. Unlike hitTest(), a
// click needs no pixel-precise x - the whole gutter width counts as the
// clickable target for a foldable row (see hitTestFoldMarker()'s own
// comment for why). folded=false is used here deliberately: the chevron
// marker itself is drawn for a fold header regardless of folded state, so
// the hit test must resolve the same way either way.
TEST(RenderTextSmokeTest, HitTestFoldMarkerReturnsHeaderLineForGutterClickOnFoldableRow) {
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
    pipeline.setFoldRegions({neomifes::render::FoldVisual{
        .headerLine = 0, .endLineInclusive = 2, .folded = false}});

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    const auto headerHit = pipeline.hitTestFoldMarker(5, 0);
    ASSERT_TRUE(headerHit.has_value());
    EXPECT_EQ(*headerHit, 0U);
}

TEST(RenderTextSmokeTest, HitTestFoldMarkerReturnsNulloptOutsideGutterXRange) {
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
    pipeline.setFoldRegions({neomifes::render::FoldVisual{
        .headerLine = 0, .endLineInclusive = 2, .folded = false}});

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    // x=100 is well past kGutterWidthDips - the click landed in the text
    // area, not the gutter, so this must never resolve to a fold header.
    EXPECT_FALSE(pipeline.hitTestFoldMarker(100, 0).has_value());
}

TEST(RenderTextSmokeTest, HitTestFoldMarkerReturnsNulloptOnNonFoldableRow) {
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
    // Only line0 is a fold header - line1's row must not resolve.
    pipeline.setFoldRegions({neomifes::render::FoldVisual{
        .headerLine = 0, .endLineInclusive = 2, .folded = false}});

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    // y=50 lands on line1's row (see HitTestReturnsPositionsWithinKnownLineBounds
    // above for why this y value is past the header row).
    EXPECT_FALSE(pipeline.hitTestFoldMarker(5, 50).has_value());
}

TEST(RenderTextSmokeTest, HitTestFoldMarkerReturnsNulloptWhenNoFoldRegionsSet) {
    // Phase 7i-and-earlier behavior must be fully preserved when folding
    // isn't in use: no setFoldRegions() call at all (stays empty).
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

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    EXPECT_FALSE(pipeline.hitTestFoldMarker(5, 0).has_value());
}

TEST(RenderTextSmokeTest, TogglingSyntaxHighlightingOffAgainStillRendersCorrectly) {
    // Phase 7b: enabling then disabling must not leave the pipeline in a bad
    // state (m_tokens must actually clear, not just stop being consulted).
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"int x = 1;\n");
    pipeline.setDocument(&doc);
    pipeline.setLanguage(Language::Cpp);

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() (highlighting on) failed: " << neomifes::render::describe(first.error());

    pipeline.setLanguage(std::nullopt);
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() (highlighting off) failed: " << neomifes::render::describe(second.error());
}

// Phase 7o: exercises drawStickyScroll()/reservedTopHeightDips() end-to-end.
// An unfolded fold region spans lines 0-4; scrolling topLine to 2 (strictly
// inside the region) must both render successfully AND actually reserve the
// extra kStickyScrollHeightDips band - verified indirectly via hitTest(),
// same technique HitTestReturnsPositionsWithinKnownLineBounds uses for the
// Breadcrumb strip: a click at a y that would land on the wrong line under
// the OLD (Breadcrumb-only) offset, but the right line under the new
// (Breadcrumb+Sticky scroll) offset, proves reservedTopHeightDips() grew.
TEST(RenderTextSmokeTest, StickyScrollRendersHeaderLineWhenScrolledPastItsHeader) {
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
    pipeline.setFoldRegions({neomifes::render::FoldVisual{
        .headerLine = 0, .endLineInclusive = 4, .folded = false}});
    pipeline.setTopLine(2);  // strictly inside (0, 4] - sticky scroll should activate

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() with an active sticky-scroll region failed: "
        << neomifes::render::describe(rendered.error());

    // With both the Breadcrumb (24 DIPs) and Sticky scroll (24 DIPs) strips
    // reserved, the text area starts at y=48 DIPs. A click just below that
    // (y=50 device px at the default 1.0 DPI scale a hidden STATIC window
    // reports) must resolve to line2 (the current topLine, offset 12-17),
    // not line1 (which the OLD Breadcrumb-only 24 DIP offset would have hit).
    const auto hit = pipeline.hitTest(0, 50);
    ASSERT_TRUE(hit.has_value());
    EXPECT_GE(*hit, 12U) << "expected a hit on line2 (sticky scroll's extra offset not applied?)";
    EXPECT_LE(*hit, 17U);
}

TEST(RenderTextSmokeTest, StickyScrollReservesNoExtraSpaceWhenTopLineIsAtHeaderItself) {
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
    pipeline.setFoldRegions({neomifes::render::FoldVisual{
        .headerLine = 0, .endLineInclusive = 4, .folded = false}});
    pipeline.setTopLine(0);  // AT the header itself, not past it - no sticky region

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    // Only the Breadcrumb strip's 24 DIPs should be reserved - a click at
    // y=30 (past the Breadcrumb band, well short of where a second 24-DIP
    // sticky band would end) must already resolve to line0 (offset 0-5).
    const auto hit = pipeline.hitTest(0, 30);
    ASSERT_TRUE(hit.has_value());
    EXPECT_LE(*hit, 5U) << "sticky scroll band appears reserved even though topLine is at the header itself";
}

TEST(RenderTextSmokeTest, StickyScrollExcludesFoldedRegions) {
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
    // Same span as StickyScrollRendersHeaderLineWhenScrolledPastItsHeader,
    // but folded=true this time - stickyScrollRegionAt() must exclude it
    // (its body is hidden, so nothing was actually "scrolled into"). Setting
    // topLine=2 here is itself an unreachable-in-practice state (a real
    // core::Viewport can never scroll INTO a folded region's hidden body -
    // there is nothing there to scroll to), exercised purely for crash-
    // safety/defensive-code coverage of stickyScrollRegionAt()'s `folded`
    // check in isolation. hitTest()'s own resolution of a hidden topLine is
    // a separate, already-covered concern (see
    // FoldedRegionRendersWithoutErrorAndHitTestSkipsHiddenLines above), so
    // this test only asserts render() doesn't crash/error - not a specific
    // hitTest() offset, which would pin an incidental fallback rather than
    // stickyScrollRegionAt()'s actual contract.
    pipeline.setFoldRegions({neomifes::render::FoldVisual{
        .headerLine = 0, .endLineInclusive = 4, .folded = true}});
    pipeline.setTopLine(2);

    const auto rendered = pipeline.render();
    EXPECT_TRUE(rendered.has_value())
        << "render() with topLine inside a folded region's hidden span failed: "
        << neomifes::render::describe(rendered.error());
}

TEST(RenderTextSmokeTest, StickyScrollSelectsInnermostNestedRegion) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    // outer: headerLine=0, endLineInclusive=4. inner: headerLine=1,
    // endLineInclusive=3. topLine=2 sits inside BOTH - stickyScrollRegionAt()
    // must pick the innermost (largest headerLine = 1), not the outer one.
    doc.insertText(0, u"outer {\ninner {\nbody\n}\n}");
    pipeline.setDocument(&doc);
    pipeline.setFoldRegions({
        neomifes::render::FoldVisual{.headerLine = 0, .endLineInclusive = 4, .folded = false},
        neomifes::render::FoldVisual{.headerLine = 1, .endLineInclusive = 3, .folded = false},
    });
    pipeline.setTopLine(2);

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() with nested sticky-scroll regions failed: "
        << neomifes::render::describe(rendered.error());

    // Same "extra offset actually reserved" proof as the single-region test
    // above - only ONE sticky band (24 DIPs) is ever reserved regardless of
    // nesting depth (Phase 7o's v1 scope: single innermost row, not a
    // stack - see the Phase 7o plan's scope-out section), so the total
    // reserved height is still 48 DIPs whether one or two regions match.
    const auto hit = pipeline.hitTest(0, 50);
    ASSERT_TRUE(hit.has_value());
    EXPECT_GE(*hit, 12U);  // line2 ("body", offset 12-16), not line1
}

// Phase 7v: exercises drawMinimap() end-to-end - a document with several
// distinct token kinds (keyword/string/number/comment) so minimapLineBrush()
// picks a real color for at least some rows, not just the empty-line/no-
// color fallback path. Same "render() succeeds, no pixel-level assertion"
// scope as the rest of this file.
TEST(RenderTextSmokeTest, MinimapRendersWithoutError) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"#include <cstdint>\n// comment\nint x = 42;\nstd::string s = \"hi\";\n");
    pipeline.setDocument(&doc);
    pipeline.setLanguage(Language::Cpp);

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() with the minimap enabled failed: " << neomifes::render::describe(rendered.error());
}

// Phase 7v/7w: pins the design decision that the minimap's window is
// [0, totalLines) - m_document->lineCount() directly, NOT m_requestedTokenRange
// - that member stays unset whenever setLanguage() is never called
// (ensureSyntaxTokensCoverVisibleRange()'s early return), which would
// otherwise leave the minimap's window silently stuck at {0,0}. With
// highlighting off, m_minimapLineColors is populated but never gets past
// Unpopulated (applyAsyncSyntaxTokens() is never called) - the whole strip
// renders in the "not computed" gray, same as it did in Phase 7v.
TEST(RenderTextSmokeTest, MinimapRendersWithoutErrorWhenSyntaxHighlightingIsDisabled) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2\nline3\nline4\n");
    pipeline.setDocument(&doc);
    // setLanguage() deliberately never called.

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() with the minimap but no language set failed: "
        << neomifes::render::describe(rendered.error());
}

// Phase 7v: exercises hitTestMinimap()'s X-range check - this 200px-wide
// HiddenWindow puts the strip's left edge at 200-120=80 DIPs (at the default
// 1.0 DPI scale a hidden STATIC window reports), so x=150 lands solidly
// inside it.
TEST(RenderTextSmokeTest, HitTestMinimapReturnsLineForClickInsideTheStrip) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2\nline3\nline4\n");
    pipeline.setDocument(&doc);

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    EXPECT_TRUE(pipeline.hitTestMinimap(150, 10).has_value());
}

TEST(RenderTextSmokeTest, HitTestMinimapReturnsNulloptForClickInTextArea) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2\nline3\nline4\n");
    pipeline.setDocument(&doc);

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    // x=10 is well left of the 80-DIP strip boundary - lands in the text area.
    EXPECT_FALSE(pipeline.hitTestMinimap(10, 10).has_value());
}

// Phase 7v: pins minimapLineAtY()'s deliberate X-independence - onMouseDrag
// calls this (not hitTestMinimap()) once a minimap drag has started, so the
// cursor drifting outside the strip's X range during the drag must not stop
// tracking (same convention an ordinary Win32 scrollbar thumb drag follows).
TEST(RenderTextSmokeTest, MinimapLineAtYIgnoresHorizontalPositionDuringDrag) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2\nline3\nline4\n");
    pipeline.setDocument(&doc);

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    // A far-outside-the-strip X fails hitTestMinimap()'s own X gate...
    EXPECT_FALSE(pipeline.hitTestMinimap(10, 10).has_value());
    // ...but minimapLineAtY() (what onMouseDrag actually calls once a drag
    // has started) still resolves the same Y to a valid line, since it takes
    // no X at all.
    EXPECT_TRUE(pipeline.minimapLineAtY(10).has_value());
}

// Phase 7w: the minimap's window is always [0, totalLines) regardless of
// topLine (see MinimapOverviewWindowCoversWholeDocumentRegardlessOfTopLine
// below for the direct regression test of that fact) - so a click at the
// very top of the strip (y=0) must resolve to line 0 unconditionally, the
// same self-consistency drawMinimapViewportHighlight()'s coordinate math
// relies on. topLine is deliberately set to 0 here too, but that's now
// incidental rather than load-bearing.
TEST(RenderTextSmokeTest, HitTestMinimapAtWindowTopReturnsWindowStartLine) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2\nline3\nline4\n");
    pipeline.setDocument(&doc);
    pipeline.setTopLine(0);

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    const auto hit = pipeline.hitTestMinimap(150, 0);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(*hit, 0U);
}

// Phase 7v: exercises drawVisibleLines()+drawMinimap() together with a
// folded region present - confirms the minimap's own (deliberately
// fold-unaware, see the Phase 7v plan's scope-out section) line walk
// doesn't crash when isLineHidden() is in play elsewhere in the same frame.
TEST(RenderTextSmokeTest, MinimapRendersWithoutErrorWithFoldedRegions) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2\nline3\nline4\n");
    pipeline.setDocument(&doc);
    pipeline.setFoldRegions({neomifes::render::FoldVisual{
        .headerLine = 0, .endLineInclusive = 3, .folded = true}});

    const auto rendered = pipeline.render();
    EXPECT_TRUE(rendered.has_value())
        << "render() with the minimap and a folded region failed: "
        << neomifes::render::describe(rendered.error());
}

// Phase 7w: pins minimapLineAtY()'s own totalLines-1 clamp - in whole-
// document-overview mode there is no windowed range to clamp against
// (Phase 7v's widenLineRangeWithMargin()-derived clamp no longer applies
// here), so this now exercises minimapLineAtY()'s direct
// `std::min(line, totalLines - 1)` instead. topLine is set near the
// document end for continuity with the original test, but (per Phase 7w)
// no longer affects the minimap's own window at all.
TEST(RenderTextSmokeTest, MinimapWindowClampsNearDocumentEnd) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document      doc;
    std::u16string content;
    for (int i = 0; i < 20; ++i) {
        content += u"line";
        content += static_cast<char16_t>(u'0' + (i % 10));
        content += u"\n";
    }
    doc.insertText(0, content);
    // 20 newline-terminated lines produce 21 logical lines (0-20) - the
    // trailing '\n' starts an empty 21st line, same convention
    // Document::lineCount() uses throughout this codebase.
    pipeline.setDocument(&doc);
    pipeline.setTopLine(20);  // the very last line

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    // A click far down the strip (well past where the window's actual
    // content ends) must still resolve to a valid line at or before the last
    // line (20), never past it - minimapLineAtY()'s own clamp
    // (`target < windowEnd ? target : windowEnd - 1`).
    const auto hit = pipeline.minimapLineAtY(5000);
    ASSERT_TRUE(hit.has_value());
    EXPECT_LE(*hit, 20U);
}

// Phase 7w: the single most important regression test for the "whole
// document overview" redesign - Phase 7v's window tracked topLine (a click
// near the strip's top always meant "near wherever we've scrolled to");
// Phase 7w's window is always [0, totalLines) regardless of topLine. Split
// into two single-assertion-pair tests (top/bottom) sharing a small fixture
// helper, rather than one test checking both ends, to keep each TestBody's
// cognitive complexity down - same shape as this file's other focused tests.
[[nodiscard]] std::unique_ptr<RenderPipeline> setUpScrolledMinimapOverviewFixture(HWND hwnd, Document& doc) {
    auto pipeline = std::make_unique<RenderPipeline>();
    if (!pipeline->attach(hwnd).has_value()) {
        return nullptr;
    }
    std::u16string content;
    for (int i = 0; i < 100; ++i) {
        content += u"line\n";
    }
    doc.insertText(0, content);
    pipeline->setDocument(&doc);
    pipeline->setTopLine(60);  // deep into the document, away from both ends
    return pipeline;
}

TEST(RenderTextSmokeTest, MinimapOverviewTopOfStripResolvesNearLineZeroRegardlessOfTopLine) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    auto     pipeline = setUpScrolledMinimapOverviewFixture(window.get(), doc);
    if (!pipeline) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment";
    }

    const auto rendered = pipeline->render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    const auto topHit = pipeline->minimapLineAtY(0);
    ASSERT_TRUE(topHit.has_value());
    EXPECT_LE(*topHit, 5U) << "a click at the very top of the strip must resolve near line 0, "
                              "not near topLine (60)";
}

TEST(RenderTextSmokeTest, MinimapOverviewBottomOfStripResolvesNearLastLineRegardlessOfTopLine) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    auto     pipeline = setUpScrolledMinimapOverviewFixture(window.get(), doc);
    if (!pipeline) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment";
    }

    const auto rendered = pipeline->render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    const auto bottomHit = pipeline->minimapLineAtY(10000);
    ASSERT_TRUE(bottomHit.has_value());
    EXPECT_GE(*bottomHit, 90U) << "a click at the very bottom of the strip must resolve near the "
                                  "document's last line";
}

// Phase 7w: bucketed rendering with a document large enough that
// computeMinimapBucketCount() actually caps the bucket count below
// totalLines - primarily an ASan/UBSan boundary-safety check (bucket->line
// mapping, minimapLineSpan()'s lineToOffset() calls) rather than a visual one.
TEST(RenderTextSmokeTest, MinimapRendersWithoutErrorOnLargeSyntheticDocument) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document      doc;
    std::u16string content;
    content.reserve(40000);
    for (int i = 0; i < 5000; ++i) {
        content += u"int line = 42; // comment\n";
    }
    doc.insertText(0, content);
    pipeline.setDocument(&doc);
    pipeline.setLanguage(Language::Cpp);

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() on a 5,000-line document failed: " << neomifes::render::describe(rendered.error());
}

// Phase 7w: scrolling through several distinct regions triggers multiple
// SyntaxWorker requests over the document's lifetime, each of which (once
// delivered) calls populateMinimapColorsForRequestedRange() for a different
// line range - confirms repeated partial writes into m_minimapLineColors
// across several render() calls don't crash.
TEST(RenderTextSmokeTest, MinimapRendersWithoutErrorWhenScrollingThroughSeveralDistinctRegions) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document      doc;
    std::u16string content;
    for (int i = 0; i < 500; ++i) {
        content += u"line\n";
    }
    doc.insertText(0, content);
    pipeline.setDocument(&doc);
    pipeline.setLanguage(Language::Cpp);

    for (const auto topLine : {0U, 100U, 250U, 400U, 50U}) {
        pipeline.setTopLine(topLine);
        const auto rendered = pipeline.render();
        ASSERT_TRUE(rendered.has_value())
            << "render() after scrolling to line " << topLine
            << " failed: " << neomifes::render::describe(rendered.error());
    }
}

// Phase 7w: exercises populateMinimapColorsForRequestedRange()'s
// offsetToLine(m_requestedTokenRange.end) + 1 boundary arithmetic directly -
// applyAsyncSyntaxTokens() is public and callable without going through the
// SyntaxWorker's background thread (same "call it directly" approach
// render_syntax_worker_test.cpp uses for the worker itself, just one layer
// up). After a real render() has set m_requestedTokenRange (typically
// covering the whole small document here), simulating the async response
// arriving must not read/write past m_minimapLineColors's bounds under
// ASan/UBSan.
TEST(RenderTextSmokeTest, ApplyAsyncSyntaxTokensDirectlyPopulatesWithoutCrashingNearDocumentBoundary) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2\nline3\nline4\n");
    pipeline.setDocument(&doc);
    pipeline.setLanguage(Language::Cpp);

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    // Simulates the async parse response arriving - m_requestedTokenRange was
    // already set by render()'s own ensureSyntaxTokensCoverVisibleRange()
    // call above.
    pipeline.applyAsyncSyntaxTokens({});

    const auto secondRender = pipeline.render();
    EXPECT_TRUE(secondRender.has_value())
        << "render() after applyAsyncSyntaxTokens() failed: "
        << neomifes::render::describe(secondRender.error());
}

// Phase 7w: extends Phase 7v's "highlighting disabled" smoke test across
// several render() calls - since m_minimapLineColors is never populated past
// Unpopulated in this mode (applyAsyncSyntaxTokens() is never called), this
// confirms repeatedly drawing an all-"not computed" strip doesn't crash.
TEST(RenderTextSmokeTest, MinimapRendersWithoutErrorWhenSyntaxHighlightingIsDisabledAfterFirstRender) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"line0\nline1\nline2\nline3\nline4\n");
    pipeline.setDocument(&doc);
    // setLanguage() deliberately never called.

    for (int i = 0; i < 3; ++i) {
        const auto rendered = pipeline.render();
        ASSERT_TRUE(rendered.has_value())
            << "render() #" << i << " with no language set failed: "
            << neomifes::render::describe(rendered.error());
    }
}

// Phase 7w: m_minimapLineColors is sized off m_document->lineCount(), not the
// window's physical dimensions - but drawMinimapLines()'s bucket count DOES
// depend on the strip's height (computeMinimapBucketCount()). Resizing after
// an initial render() (so the bucket count for the next frame differs from
// the first) must not crash.
TEST(RenderTextSmokeTest, MinimapWindowSurvivesResize) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document      doc;
    std::u16string content;
    for (int i = 0; i < 200; ++i) {
        content += u"line\n";
    }
    doc.insertText(0, content);
    pipeline.setDocument(&doc);
    pipeline.setLanguage(Language::Cpp);

    const auto firstRender = pipeline.render();
    ASSERT_TRUE(firstRender.has_value())
        << "initial render() failed: " << neomifes::render::describe(firstRender.error());

    const auto resized = pipeline.resize(400, 800, 1.0F);
    ASSERT_TRUE(resized.has_value())
        << "resize() failed: " << neomifes::render::describe(resized.error());

    const auto secondRender = pipeline.render();
    EXPECT_TRUE(secondRender.has_value())
        << "render() after resize() failed: " << neomifes::render::describe(secondRender.error());
}

// WI-03: horizontal scroll. A 1200-character single line stands in for the
// DoD's "1000-char line reachable" case (this codebase has no
// pixel-capture, per this file's header - "reachable" here means render()
// succeeds and hitTest() resolves near the intended column once scrolled,
// not that glyphs are visually verified).
TEST(RenderTextSmokeTest, HorizontalScrollRendersWithoutErrorForLongLine) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, std::u16string(1200, u'x'));
    pipeline.setDocument(&doc);

    const auto firstRender = pipeline.render();
    ASSERT_TRUE(firstRender.has_value())
        << "initial render() failed: " << neomifes::render::describe(firstRender.error());

    // Several distinct scroll positions, including one past the whole line's
    // length - render() must never error regardless of leftColumn.
    for (const std::uint32_t column : {0U, 1U, 500U, 1199U, 5000U}) {
        pipeline.setLeftColumn(column);
        const auto rendered = pipeline.render();
        EXPECT_TRUE(rendered.has_value())
            << "render() at leftColumn=" << column
            << " failed: " << neomifes::render::describe(rendered.error());
    }
}

// WI-03: maxVisibleLineLength() (the horizontal scrollbar's nMax source,
// main.cpp's syncHorizontalScrollBar()) must track the longest line among
// those actually drawn - a cheap, windowed approximation (see this method's
// header comment on why a whole-document scan is unacceptable).
TEST(RenderTextSmokeTest, MaxVisibleLineLengthReflectsLongestVisibleLine) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"short\n" + std::u16string(300, u'x') + u"\nshort2");
    pipeline.setDocument(&doc);

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() failed: " << neomifes::render::describe(rendered.error());

    EXPECT_GE(pipeline.maxVisibleLineLength(), 300U);
}

// WI-03: hitTest()'s xDip->TextPos conversion must account for the current
// horizontal scroll offset (leftColumnOffsetDips()) - see hitTest()'s
// comment. Same "plausible bounds, not exact pixel round-trip" limitation
// HitTestReturnsPositionsWithinKnownLineBounds documents (Consolas' actual
// glyph metrics aren't asserted here), but monospace means every column
// advances by the same width, so a ~10-column tolerance is safely
// distinguishable from "leftColumn was ignored entirely" (which would land
// back near column 0).
TEST(RenderTextSmokeTest, HitTestAccountsForLeftColumnWhenScrolledHorizontally) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, std::u16string(1200, u'x'));
    pipeline.setDocument(&doc);

    const auto firstRender = pipeline.render();
    ASSERT_TRUE(firstRender.has_value())
        << "initial render() failed: " << neomifes::render::describe(firstRender.error());

    // x=0 with no horizontal scroll should hit near the very start of the line.
    const auto unscrolledHit = pipeline.hitTest(0, 0);
    ASSERT_TRUE(unscrolledHit.has_value());
    EXPECT_LE(*unscrolledHit, 2U);

    // Scroll 50 columns right, re-render, then hit-test the SAME x=0 - it
    // should now resolve near column 50, not column 0.
    pipeline.setLeftColumn(50);
    const auto secondRender = pipeline.render();
    ASSERT_TRUE(secondRender.has_value())
        << "render() after setLeftColumn() failed: " << neomifes::render::describe(secondRender.error());

    const auto scrolledHit = pipeline.hitTest(0, 0);
    ASSERT_TRUE(scrolledHit.has_value());
    EXPECT_GE(*scrolledHit, 40U);
    EXPECT_LE(*scrolledHit, 60U);
}

// WI-03: the gutter (bookmark dots, fold chevrons) must stay visually fixed
// regardless of horizontal scroll - hitTestFoldMarker() only ever checks the
// gutter's own X range ([0, kGutterWidthDips)), unrelated to leftColumn, so
// it must resolve identically before and after scrolling.
TEST(RenderTextSmokeTest, GutterFoldMarkerHitTestIsUnaffectedByHorizontalScroll) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, std::u16string(200, u'x') + u"\nline1\nline2");
    pipeline.setDocument(&doc);
    pipeline.setFoldRegions({neomifes::render::FoldVisual{
        .headerLine = 0, .endLineInclusive = 2, .folded = false}});

    const auto firstRender = pipeline.render();
    ASSERT_TRUE(firstRender.has_value())
        << "initial render() failed: " << neomifes::render::describe(firstRender.error());
    const auto unscrolledHit = pipeline.hitTestFoldMarker(5, 0);
    ASSERT_TRUE(unscrolledHit.has_value());
    EXPECT_EQ(*unscrolledHit, 0U);

    pipeline.setLeftColumn(80);
    const auto secondRender = pipeline.render();
    ASSERT_TRUE(secondRender.has_value())
        << "render() after setLeftColumn() failed: " << neomifes::render::describe(secondRender.error());
    const auto scrolledHit = pipeline.hitTestFoldMarker(5, 0);
    ASSERT_TRUE(scrolledHit.has_value())
        << "gutter fold-marker hit test was affected by horizontal scroll";
    EXPECT_EQ(*scrolledHit, 0U);
}

// WI-03: FrameState.leftColumn must be included in the coarse frame-skip
// comparison (Phase 3c/ADR-011) - the exact same hazard
// m_documentGeneration was added to fix earlier this session
// (see FrameState::leftColumn's declaration comment): a horizontal-only
// scroll (e.g. dragging the new scrollbar) with topLine/cursor/selection/etc
// all unchanged must not be silently swallowed by the frame-skip. Same
// "stats must move" technique as CaretOnlyMovementForcesRedrawInsteadOfFrameSkip
// above.
TEST(RenderTextSmokeTest, LeftColumnOnlyChangeForcesRedraw) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, std::u16string(200, u'x'));
    pipeline.setDocument(&doc);

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());
    const auto statsAfterFirst = pipeline.layoutCacheStats();

    // leftColumn only - Document/topLine/size/cursor/selection/etc all
    // unchanged.
    pipeline.setLeftColumn(50);
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() (leftColumn moved) failed: " << neomifes::render::describe(second.error());
    const auto statsAfterSecond = pipeline.layoutCacheStats();
    EXPECT_TRUE(statsAfterSecond.hits != statsAfterFirst.hits ||
                statsAfterSecond.misses != statsAfterFirst.misses)
        << "leftColumn-only change was frame-skipped instead of triggering a redraw";
}

// WI-09: FrameState::themeKind must be included in the coarse frame-skip
// comparison (Phase 3c/ADR-011) - the exact same hazard m_leftColumn/
// m_imeComposition were added to fix earlier in this project's history (see
// FrameState::imeComposition's declaration comment): calling setTheme()
// alone (Document/topLine/cursor/etc. all unchanged) must not be swallowed
// by the frame-skip, or the newly-reset (nulled) brushes would sit
// uninitialized until some unrelated state change eventually triggers a
// real repaint. Same "stats must move" technique as
// LeftColumnOnlyChangeForcesRedraw above.
TEST(RenderTextSmokeTest, ThemeOnlyChangeForcesRedrawInsteadOfFrameSkip) {
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

    // Theme only - Document/topLine/cursor/etc. all stay identical.
    pipeline.setTheme(ThemeKind::Light);
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() (theme changed) failed: " << neomifes::render::describe(second.error());
    const auto statsAfterSecond = pipeline.layoutCacheStats();
    EXPECT_TRUE(statsAfterSecond.hits != statsAfterFirst.hits ||
                statsAfterSecond.misses != statsAfterFirst.misses)
        << "theme-only change was frame-skipped instead of triggering a redraw";
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

// WI-08: setFontSettings() forces m_textFormat/m_charWidthDips/
// m_lineHeightDips/m_layoutCache to be rebuilt from the new font on the
// next render() - this only checks that the rebuild completes without
// error (pixel-level verification is out of scope for this file, same as
// every other test here).
TEST(RenderTextSmokeTest, SetFontSettingsThenRenderStillSucceeds) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"int main() {\n\treturn 0;\n}\n");
    pipeline.setDocument(&doc);

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());

    pipeline.setFontSettings(u"Courier New", 18.0F);
    const auto second = pipeline.render();
    EXPECT_TRUE(second.has_value())
        << "render() after setFontSettings() failed: " << neomifes::render::describe(second.error());
}

// WI-08: setTabWidth() rebuilds the indent-guide column math and (once a
// text format already exists) mutates the live IDWriteTextFormat's
// incremental tab stop directly - same "rebuild completes without error"
// scope as the font test above.
TEST(RenderTextSmokeTest, SetTabWidthThenRenderStillSucceeds) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"if (x) {\n\treturn;\n}\n");
    pipeline.setDocument(&doc);

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());

    pipeline.setTabWidth(8);
    const auto second = pipeline.render();
    EXPECT_TRUE(second.has_value())
        << "render() after setTabWidth() failed: " << neomifes::render::describe(second.error());
}

// WI-09: setTheme() forces every ensureXxxBrush() to rebuild via
// resetThemeBrushes() - this only checks that the rebuild completes without
// error (pixel-level verification is out of scope for this file, same as
// every other test here).
TEST(RenderTextSmokeTest, SetThemeThenRenderStillSucceeds) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"int main() {\n\treturn 0;\n}\n");
    pipeline.setDocument(&doc);

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());

    pipeline.setTheme(ThemeKind::Light);
    const auto second = pipeline.render();
    EXPECT_TRUE(second.has_value())
        << "render() after setTheme() failed: " << neomifes::render::describe(second.error());
}

// WI-14c: setLogLineLevels() forces drawLogLevelOnLine() to run for every
// visible line on the next render() - this only checks that the rebuild
// completes without error (pixel-level verification is out of scope for
// this file, same as every other test here).
TEST(RenderTextSmokeTest, SetLogLineLevelsThenRenderStillSucceeds) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"2026-08-17 10:00:00 INFO  started\n2026-08-17 10:00:01 ERROR  failed\n");
    pipeline.setDocument(&doc);

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());

    pipeline.setLogLineLevels({LogLevel::Info, LogLevel::Error});
    const auto second = pipeline.render();
    EXPECT_TRUE(second.has_value())
        << "render() after setLogLineLevels() failed: " << neomifes::render::describe(second.error());
}

// WI-14c: exercises isLineHidden()'s log-level filter branch end-to-end -
// same "click below the filtered-out line must land on the next visible
// one" technique FoldedRegionRendersWithoutErrorAndHitTestSkipsHiddenLines
// above uses for folding. line1 (Info) is filtered out by an errors-only
// mask, so a click at that row must resolve to line2 instead.
TEST(RenderTextSmokeTest, LogLevelFilterHidesMatchingLines) {
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
    pipeline.setLogLineLevels({LogLevel::Error, LogLevel::Info, LogLevel::Error});
    pipeline.setLogLevelFilter(logLevelFilterBit(LogLevel::Error));

    const auto rendered = pipeline.render();
    ASSERT_TRUE(rendered.has_value())
        << "render() with a log-level filter failed: " << neomifes::render::describe(rendered.error());

    // Same y=50/EXPECT_GE looseness as
    // FoldedRegionRendersWithoutErrorAndHitTestSkipsHiddenLines above (row
    // height in dips isn't pinned down here) - the row where line1 would be
    // must resolve to line2 (offset 12-17) or later, never to the
    // filtered-out line1, since hitTest() only ever walks lines
    // drawVisibleLines() actually drew.
    const auto rowOneHit = pipeline.hitTest(0, 50);
    ASSERT_TRUE(rowOneHit.has_value());
    EXPECT_GE(*rowOneHit, 12U);
}

// WI-14c: FrameState::logLevelFilterMask must be included in the coarse
// frame-skip comparison (Phase 3c/ADR-011) - same hazard class
// LeftColumnOnlyChangeForcesRedraw/ThemeOnlyChangeForcesRedrawInsteadOfFrameSkip
// above guard against. This test instead exercises setLogLineLevels()'s own
// m_lastRenderedFrameState.reset() call (the array itself is deliberately
// NOT part of FrameState - see that method's declaration comment) using the
// same "layout cache stats must move" technique.
TEST(RenderTextSmokeTest, LogLineLevelsOnlyChangeForcesRedrawInsteadOfFrameSkip) {
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

    // Log line levels only - Document/topLine/cursor/etc. all stay
    // identical.
    pipeline.setLogLineLevels({LogLevel::Error, LogLevel::Warning, LogLevel::Info});
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "second render() (log line levels changed) failed: " << neomifes::render::describe(second.error());
    const auto statsAfterSecond = pipeline.layoutCacheStats();
    EXPECT_TRUE(statsAfterSecond.hits != statsAfterFirst.hits ||
                statsAfterSecond.misses != statsAfterFirst.misses)
        << "log-line-levels-only change was frame-skipped instead of triggering a redraw";
}

// WI-08: setLineNumbersVisible(false) shrinks gutterWidthDips() back to the
// pre-WI-07 flat bookmark/fold-marker-only width. gutterWidthDips() itself
// is private, so this asserts through visibleColumnCount() (public), which
// subtracts it from the available width - the same indirect-assertion
// shape as SetMinimapVisibleFalseWidensVisibleColumnCount below.
TEST(RenderTextSmokeTest, SetLineNumbersVisibleFalseWidensVisibleColumnCount) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    // Enough lines (4 digits) that computeGutterWidthDips() grows past its
    // minWidthDips floor - built as one bulk insert, not one insertText()
    // call per line (existing tests' loops top out around 5000 individual
    // calls; this needs more lines than that just for the digit count).
    std::u16string manyLines;
    manyLines.reserve(2000 * 2);
    for (int i = 0; i < 2000; ++i) {
        manyLines += u"x\n";
    }
    doc.insertText(0, manyLines);
    pipeline.setDocument(&doc);

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());
    const std::uint32_t columnsWithNumbers = pipeline.visibleColumnCount();

    pipeline.setLineNumbersVisible(false);
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "render() after setLineNumbersVisible(false) failed: "
        << neomifes::render::describe(second.error());
    EXPECT_GT(pipeline.visibleColumnCount(), columnsWithNumbers)
        << "hiding line numbers did not shrink the gutter";
}

// WI-08: setMinimapVisible(false) reclaims the minimap's reserved width -
// directly assertable via visibleColumnCount() (public) without needing
// pixel inspection.
TEST(RenderTextSmokeTest, SetMinimapVisibleFalseWidensVisibleColumnCount) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    RenderPipeline pipeline;
    auto attached = pipeline.attach(window.get());
    if (!attached.has_value()) {
        GTEST_SKIP() << "RenderPipeline::attach() failed in this environment: "
                     << neomifes::render::describe(attached.error());
    }

    Document doc;
    doc.insertText(0, u"hello world\n");
    pipeline.setDocument(&doc);

    const auto first = pipeline.render();
    ASSERT_TRUE(first.has_value())
        << "first render() failed: " << neomifes::render::describe(first.error());
    const std::uint32_t columnsWithMinimap = pipeline.visibleColumnCount();

    pipeline.setMinimapVisible(false);
    const auto second = pipeline.render();
    ASSERT_TRUE(second.has_value())
        << "render() after setMinimapVisible(false) failed: "
        << neomifes::render::describe(second.error());
    EXPECT_GT(pipeline.visibleColumnCount(), columnsWithMinimap)
        << "hiding the minimap did not widen the visible column count";
}

}  // namespace
