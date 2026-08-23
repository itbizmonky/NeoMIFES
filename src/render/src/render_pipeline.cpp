#include "neomifes/render/render_pipeline.h"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/render/d2d_factories.h"
#include "neomifes/render/gutter_math.h"
#include "neomifes/render/indent_guide_math.h"
#include "neomifes/render/resize_math.h"
#include "neomifes/render/viewport_math.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::render {

namespace {
using document::LineNumber;
using document::TextPos;
using document::TextRange;

// Fixed, window-size-independent bound rather than the live client width:
// keeps layout cache entries valid across resize() (a resize never needs to
// invalidate this cache), same spirit as ensureTextFormat()'s 4096-DIP
// probe-layout box. Shared by drawVisibleLines() and hitTest() (Phase 4b2)
// since both fetch line layouts through the same TextLayoutCache.
constexpr float kMaxLayoutWidthDips  = 65536.0F;
constexpr float kMaxLayoutHeightDips = 65536.0F;

// Phase 4b8c: original fixed bookmark-only gutter width. WI-07 step7
// superseded this as the LIVE x-coordinate offset - every consumer that
// used to reference this constant directly now calls
// RenderPipeline::gutterWidthDips() instead (see its own declaration
// comment), which returns this value only as its minWidthDips floor
// (small documents / char width not yet measured). Kept as a named
// constant (not inlined) because gutterWidthDips() itself still needs it.
constexpr float kGutterWidthDips  = 24.0F;
constexpr float kBookmarkDotSizeDips = 8.0F;
// WI-17c: the Git diff gutter marker's left-edge vertical bar - drawn at
// x=0, the only gutter sub-region no other marker (line number/bookmark
// dot/fold chevron) occupies (see drawGutterOnLine()'s own X-coordinate
// choices for those three). kGitDiffDeletedMarkerHeightDips is a short bar
// at the TOP of a line's row rather than the full row height, since a
// Deleted GitDiffMarker is a point marker (git_repository.h's own
// documented convention: "content used to be immediately above this line",
// not a range covering this line).
constexpr float kGitDiffBarWidthDips             = 3.0F;
constexpr float kGitDiffDeletedMarkerHeightDips = 3.0F;

// Phase 7h: top-of-editor Breadcrumb strip height. Same "every y-coordinate
// consumer in this file must agree on this offset" contract kGutterWidthDips
// documents for the x-axis - see reservedTopHeightDips() below (Phase 7o
// centralized the y-coordinate consumers that used to reference this
// constant directly).
constexpr float kBreadcrumbHeightDips = 24.0F;
// Phase 7o: Sticky scroll strip height, directly below the Breadcrumb strip
// when present - see reservedTopHeightDips()/drawStickyScroll().
constexpr float kStickyScrollHeightDips = 24.0F;

// Phase 7v: right-edge minimap strip width - the midpoint of roadmap
// sec.7.4's "100-150px" range, taken as DIPs (an untuned starting point per
// CLAUDE.md rule 10 - this is a look-and-feel choice, not something a
// benchmark determines, so it gets adjusted from real-app viewing rather
// than measurement). Unlike kGutterWidthDips, this is NOT a contract every
// x-coordinate consumer in this file must agree on - see
// RenderPipeline::minimapLeftDips()'s header comment for why.
constexpr float kMinimapWidthDips = 120.0F;
// Roadmap sec.7.4's own specified reduction factor for both row height and
// character width, applied to the already-measured m_lineHeightDips/
// m_charWidthDips (not a separately guessed value - CLAUDE.md rule 3).
constexpr float kMinimapScaleDivisor = 8.0F;
}  // namespace

RenderExpected<void> RenderPipeline::attach(HWND hwnd) noexcept {
    RECT rect{};
    if (::GetClientRect(hwnd, &rect) == 0) {
        return std::unexpected(RenderError{.stage = RenderStage::NotAttached,
                                           .hr = HRESULT_FROM_WIN32(::GetLastError())});
    }
    m_hwnd     = hwnd;
    m_width    = static_cast<std::uint32_t>(rect.right - rect.left);
    m_height   = static_cast<std::uint32_t>(rect.bottom - rect.top);
    m_dpiScale = dpiToScale(::GetDpiForWindow(hwnd));

    auto device = RenderDevice::create(m_hwnd, m_width, m_height);
    if (!device) {
        return std::unexpected(device.error());
    }
    m_device = std::move(*device);
    m_device->setDpi(m_dpiScale);
    m_textBrush.Reset();       // stale binding to whatever device context existed before
    m_selectionBrush.Reset();
    m_matchBrush.Reset();
    m_currentMatchBrush.Reset();
    return {};
}

RenderExpected<void> RenderPipeline::resize(std::uint32_t width, std::uint32_t height,
                                            float dpiScale) noexcept {
    m_width    = width;
    m_height   = height;
    m_dpiScale = dpiScale;
    if (!m_device) {
        return std::unexpected(RenderError{.stage = RenderStage::NotAttached, .hr = E_NOT_VALID_STATE});
    }
    auto result = m_device->resize(width, height);
    if (!result && result.error().isDeviceLost()) {
        return recreateDevice();
    }
    if (result) {
        m_device->setDpi(m_dpiScale);
    }
    return result;
}

RenderPipeline::FrameState RenderPipeline::captureFrameState() const noexcept {
    return FrameState{
        .hasDocument        = m_document != nullptr,
        .documentVersion    = m_document != nullptr ? m_document->version() : 0,
        .documentGeneration = m_documentGeneration,
        .topLine            = m_topLine,
        .width           = m_width,
        .height          = m_height,
        .dpiScale        = m_dpiScale,
        .cursorVisuals   = m_cursorVisuals,
        .matchVisuals    = m_matchVisuals,
        .bookmarkedLines = m_bookmarkedLines,
        .foldRegions     = m_foldRegions,
        .gitDiffMarkers  = m_gitDiffMarkers,
        .leftColumn      = m_leftColumn,
        .imeComposition  = m_imeComposition,
        .themeKind       = m_themeKind,
        .logLevelFilterMask = m_logLevelFilterMask,
    };
}

RenderExpected<void> RenderPipeline::render() noexcept {
    if (!m_device) {
        return std::unexpected(RenderError{.stage = RenderStage::NotAttached, .hr = E_NOT_VALID_STATE});
    }

    // Coarse frame-level skip (Phase 3c, ADR-011): if nothing observable has
    // changed since the last successful frame, skip beginFrame/Clear/
    // drawVisibleLines/endFrame entirely. Safe under the FLIP_DISCARD swap
    // effect + DWM composition (the compositor retains the last presented
    // frame independently of this process; MainWindow::handlePaint() already
    // calls ValidateRect() unconditionally regardless of what the paint
    // handler does, so this cannot cause a WM_PAINT repost loop).
    const FrameState current = captureFrameState();
    if (m_lastRenderedFrameState && *m_lastRenderedFrameState == current) {
        return {};
    }

    auto result = renderOnce();
    if (!result && result.error().isDeviceLost()) {
        auto recreated = recreateDevice();
        if (!recreated) {
            return recreated;
        }
        if (!m_device) {
            // recreateDevice() reported success but left m_device empty -
            // should not happen, but stay honest rather than dereferencing.
            return std::unexpected(
                RenderError{.stage = RenderStage::NotAttached, .hr = E_UNEXPECTED});
        }
        result = renderOnce();
    }
    if (result) {
        m_lastRenderedFrameState = current;
    }
    return result;
}

// WI-09: the 21 device-bound brush ComPtrs this pipeline caches, reset to
// force ensureXxxBrush()'s lazy-create-once guards to run again next frame.
// Shared by two different triggers: recreateDevice() below (the device
// context they were bound to no longer exists) and setTheme() (the device
// is still fine, but every brush was built from the OLD theme's colors and
// must be rebuilt from the new one) - same brush list, same reset
// mechanics, so recreateDevice() calls this instead of repeating it inline.
void RenderPipeline::resetThemeBrushes() noexcept {
    m_textBrush.Reset();
    m_selectionBrush.Reset();
    m_matchBrush.Reset();
    m_currentMatchBrush.Reset();
    m_bookmarkBrush.Reset();
    m_diffAddedBrush.Reset();
    m_diffModifiedBrush.Reset();
    m_diffDeletedBrush.Reset();
    m_keywordBrush.Reset();
    m_typeBrush.Reset();
    m_stringBrush.Reset();
    m_numberBrush.Reset();
    m_commentBrush.Reset();
    m_preprocessorBrush.Reset();
    m_indentGuideBrush.Reset();
    m_activeIndentGuideBrush.Reset();
    m_breadcrumbBackgroundBrush.Reset();
    m_foldMarkerBrush.Reset();
    m_lineNumberBrush.Reset();
    m_minimapBackgroundBrush.Reset();
    m_minimapViewportBrush.Reset();
    m_minimapTextBrush.Reset();
    m_minimapUnpopulatedBrush.Reset();
    m_imeCompositionBackgroundBrush.Reset();
    m_imeTargetClauseBrush.Reset();
    m_logErrorBrush.Reset();
    m_logWarningBrush.Reset();
}

RenderExpected<void> RenderPipeline::recreateDevice() noexcept {
    m_device.reset();
    // WI-09: factored out to resetThemeBrushes() - shared with setTheme()'s
    // brush-invalidation path (same 21 brushes, same reset mechanics).
    resetThemeBrushes();
    // A freshly (re)created swap chain's back buffer is uninitialized - the
    // next render() must not treat "nothing logically changed" as license to
    // skip drawing into it.
    m_lastRenderedFrameState.reset();
    auto device = RenderDevice::create(m_hwnd, m_width, m_height);
    if (!device) {
        return std::unexpected(device.error());
    }
    m_device = std::move(*device);
    m_device->setDpi(m_dpiScale);
    return {};
}

RenderExpected<void> RenderPipeline::refreshDocumentCacheIfStale() noexcept {
    if (m_document == nullptr) {
        m_hasCachedSnapshot = false;
        m_cachedSnapshot.reset();
        m_tokens.clear();
        m_cachedOutline.clear();
        m_minimapLineColors.clear();  // Phase 7w
        return {};
    }
    if (m_hasCachedSnapshot && m_document->version() == m_cachedDocumentVersion) {
        return {};
    }
    // Phase 7l: captured BEFORE m_hasCachedSnapshot is set true below.
    // false here means either the very first refresh ever, or a forced
    // reset via setLanguage() (a new document/language is now attached,
    // e.g. after openDocumentAt() - see setLanguage()'s own comment on
    // forcing m_hasCachedSnapshot false). In both cases, any accumulated
    // EditDelta's on m_document are meaningless to a syntax worker that (if
    // it exists at all) last retained an incremental-parse tree for a
    // DIFFERENT document's text - that tree must be discarded rather than
    // reused, which is exactly what passing this through to
    // SyntaxWorker::requestParse()'s resetIncrementalState does.
    const bool forceFullReparse = !m_hasCachedSnapshot;
    // The one and only Document::snapshot() call site in the render layer -
    // gated on version() having moved, per detailed_design.md sec.4.3's
    // "don't call snapshot() every frame" guardrail (ADR-010).
    m_cachedSnapshot        = m_document->snapshot();
    m_cachedDocumentVersion = m_document->version();
    m_hasCachedSnapshot     = true;
    // Wholesale invalidation - the only granularity available without a
    // per-region change source (ADR-011). Every line's layout is stale once
    // the document has mutated at all, since Document::version() carries no
    // range information.
    m_layoutCache.clear();
    // Phase 7c: clear immediately, re-tokenize off the UI thread. m_tokens
    // is cleared (not left showing the previous parse) - even with Phase
    // 7l's true incremental re-PARSING, this class does not synchronously
    // shift existing m_tokens' offsets by the edit's size, so after ANY
    // edit every existing token's stored range can be wrong until the
    // async result arrives; drawing them meanwhile would risk coloring the
    // wrong characters. applyAsyncSyntaxTokens() repopulates m_tokens once
    // SyntaxWorker's background parse completes; until then the text falls
    // back to the default (uncolored) brush, a deliberate, documented
    // deviation from roadmap sec.7.9's "keep showing old tokens" sketch
    // (which would need synchronous position-shifting, not implemented -
    // see master_roadmap.md sec.7's Phase 7l completion note).
    m_tokens.clear();
    m_cachedOutline.clear();
    // Phase 7w: wholesale re-init (never shifted/patched per-edit, see this
    // member's declaration comment) - every line's cached color is stale
    // once the document has mutated at all, same reasoning m_layoutCache's
    // wholesale clear() above already uses for layouts.
    m_minimapLineColors.assign(m_document->lineCount(), MinimapLineColorState::Unpopulated);
    // Phase 7k: drains every EditDelta recorded since the last drain,
    // unconditionally (even if m_language is nullopt below) - Document
    // accumulates these regardless of whether syntax highlighting is
    // enabled, and leaving them undrained here would grow m_document's
    // internal vector without bound.
    //
    // Phase 7t: staged into m_pendingSyntaxEdits/m_forceFullReparseNextRequest
    // rather than sent to SyntaxWorker directly - this function returns
    // EARLY on a pure scroll (see the version-unchanged check above) and
    // never reaches this point at all in that case, so the actual
    // requestParse() call is centralized in ensureSyntaxTokensCoverVisibleRange()
    // (called unconditionally every frame from renderOnce()), which can also
    // fire a request purely because the visible range moved with no document
    // change.
    std::vector<document::EditDelta> pendingEdits = m_document->takePendingEdits();
    m_pendingSyntaxEdits.insert(m_pendingSyntaxEdits.end(), std::make_move_iterator(pendingEdits.begin()),
                                std::make_move_iterator(pendingEdits.end()));
    m_forceFullReparseNextRequest = m_forceFullReparseNextRequest || forceFullReparse;
    if (m_language.has_value()) {
        // Phase 7h (Breadcrumb): SYNCHRONOUS, unlike token coloring above -
        // extractOutline() is already a deliberately independent, separate
        // parse from token coloring (outline.h's header comment), so this
        // does not share SyntaxWorker's background thread. Recomputing it
        // asynchronously as well would need either a second background
        // worker or extending SyntaxWorker to also produce outlines - a
        // materially bigger scope increase with no benchmark yet showing the
        // synchronous cost actually matters (CLAUDE.md rule 10). Same
        // "ship synchronous first" order Phase 7b (sync tokens) took before
        // Phase 7c (async) was justified by a measured stutter. Known,
        // accepted limitation: on a very large file, Breadcrumb can lag one
        // frame behind the edit that invalidated it.
        m_cachedOutline = syntax::extractOutline(
            m_cachedSnapshot->extract(TextRange{.start = 0, .end = m_cachedSnapshot->length()}), *m_language);
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureTextFormat() noexcept {
    if (m_textFormat) {
        return {};
    }
    auto factory = sharedDWriteFactory();
    if (!factory) {
        return std::unexpected(factory.error());
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    // WI-08: family/size now come from setFontSettings() (defaults match the
    // pre-WI-08 hardcoded L"Consolas"/14.0F exactly). CreateTextFormat()
    // requires a null-terminated wchar_t* - util::toWstringView()'s result
    // is safe to pass directly here since m_fontFamily is a live member
    // (not a temporary), and std::u16string is null-terminated same as
    // std::string.
    const std::wstring_view fontFamilyView = util::toWstringView(m_fontFamily);
    // fontFamilyView aliases m_fontFamily's own buffer (no copy), and
    // std::u16string guarantees a null terminator at data()[size()] same as
    // std::string, so this is safe despite wstring_view itself not carrying
    // that guarantee.
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    HRESULT hr = (*factory)->CreateTextFormat(fontFamilyView.data(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                              m_fontSizeDips, L"en-us", format.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DWriteFactory, .hr = hr});
    }
    // Promoted to a member (Phase 3c) so drawVisibleLines() can hand it to
    // TextLayoutCache::getOrCreate() on a cache miss without re-querying the
    // process-wide singleton every frame.
    m_dwriteFactory = *factory;

    // The default DWRITE_WORD_WRAPPING_WRAP would silently break the fixed
    // topLine*lineHeight row layout drawVisibleLines() relies on (a long
    // line would wrap and push every following row down instead of being
    // clipped at the client edge).
    hr = format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DWriteFactory, .hr = hr});
    }

    // Probe line height via a throwaway layout: representative string, a
    // generously large layout box so nothing clips/wraps during the probe.
    Microsoft::WRL::ComPtr<IDWriteTextLayout> probeLayout;
    hr = (*factory)->CreateTextLayout(L"Ag", 2, format.Get(), 4096.0F, 4096.0F,
                                      probeLayout.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DWriteFactory, .hr = hr});
    }
    DWRITE_LINE_METRICS metrics{};
    UINT32              actualCount = 0;
    hr = probeLayout->GetLineMetrics(&metrics, 1, &actualCount);
    if (FAILED(hr) || actualCount == 0) {
        return std::unexpected(RenderError{.stage = RenderStage::DWriteFactory, .hr = hr});
    }

    // Reuses the same "Ag" probe layout (Phase 4b8e) - the X coordinate
    // right after the first character equals that character's advance
    // width, since HitTestTextPosition() is layout-local (origin 0,0). Only
    // meaningful because this class's entire column math assumes a
    // fixed-pitch font (single measured advance width applied uniformly to
    // every glyph - see drawCaretOnLine()'s comment for where this is
    // consumed). WI-08 makes the font family user-configurable but does NOT
    // lift this assumption: a non-monospace fontFamily will visually
    // misalign columns (caret/selection/indent guides/minimap all still use
    // this one measured width) - a known, pre-existing architectural
    // limitation, not something WI-08 introduces or fixes.
    DWRITE_HIT_TEST_METRICS charMetrics{};
    float                    charX = 0.0F;
    float                    charY = 0.0F;
    hr = probeLayout->HitTestTextPosition(1, FALSE, &charX, &charY, &charMetrics);
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DWriteFactory, .hr = hr});
    }

    // WI-08: makes literal '\t' characters in the document actually render
    // at m_tabWidth columns. Before this, no code in this codebase ever
    // called SetIncrementalTabStop(), so tab glyphs rendered at DirectWrite's
    // own built-in default, completely independent of the (now-unified)
    // kTabWidth value drawIndentGuidesOnLine() uses for its guide-line
    // column math - a latent inconsistency this call closes.
    format->SetIncrementalTabStop(static_cast<float>(m_tabWidth) * charX);

    m_textFormat     = std::move(format);
    m_lineHeightDips = metrics.height;
    m_charWidthDips  = charX;
    return {};
}

RenderExpected<void> RenderPipeline::ensureTextBrush(ID2D1DeviceContext6& dc) noexcept {
    if (m_textBrush) {
        return {};
    }
    // WI-09: color comes from theme.h/theme.cpp's Theme table (was a local
    // hardcoded constexpr before WI-09's Theme system - see kDarkTheme's own
    // comment there, "Windows' conventional selection blue"-style rationale
    // included). Also the caret's color (drawCaretOnLine() reuses
    // m_textBrush directly) - no separate caret brush exists.
    const HRESULT hr = dc.CreateSolidColorBrush(themeForKind(m_themeKind).text, m_textBrush.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureSelectionBrush(ID2D1DeviceContext6& dc) noexcept {
    if (m_selectionBrush) {
        return {};
    }
    // WI-09: see theme.h/theme.cpp for this color's value/rationale (was a
    // local hardcoded constexpr before WI-09's Theme system).
    const HRESULT hr =
        dc.CreateSolidColorBrush(themeForKind(m_themeKind).selection, m_selectionBrush.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureMatchBrushes(ID2D1DeviceContext6& dc) noexcept {
    // WI-09: see theme.h/theme.cpp for match/currentMatch's colors and
    // rationale (were 2 local hardcoded constexpr before WI-09's Theme
    // system).
    const Theme& theme = themeForKind(m_themeKind);
    if (!m_matchBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.match, m_matchBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_currentMatchBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.currentMatch, m_currentMatchBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureBookmarkBrush(ID2D1DeviceContext6& dc) noexcept {
    if (!m_bookmarkBrush) {
        // WI-09: see theme.h/theme.cpp for this color's value/rationale.
        const HRESULT hr = dc.CreateSolidColorBrush(themeForKind(m_themeKind).bookmark, m_bookmarkBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureGitDiffBrushes(ID2D1DeviceContext6& dc) noexcept {
    // WI-17c: see theme.h/theme.cpp for these colors' values/rationale.
    const Theme& theme = themeForKind(m_themeKind);
    if (!m_diffAddedBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.diffAdded, m_diffAddedBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_diffModifiedBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.diffModified, m_diffModifiedBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_diffDeletedBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.diffDeleted, m_diffDeletedBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureFoldMarkerBrush(ID2D1DeviceContext6& dc) noexcept {
    if (!m_foldMarkerBrush) {
        // WI-09: see theme.h/theme.cpp for this color's value/rationale.
        const HRESULT hr =
            dc.CreateSolidColorBrush(themeForKind(m_themeKind).foldMarker, m_foldMarkerBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureLineNumberBrush(ID2D1DeviceContext6& dc) noexcept {
    if (!m_lineNumberBrush) {
        // WI-09: see theme.h/theme.cpp for this color's value/rationale.
        const HRESULT hr =
            dc.CreateSolidColorBrush(themeForKind(m_themeKind).lineNumber, m_lineNumberBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureTokenBrushes(ID2D1DeviceContext6& dc) noexcept {
    // WI-09: colors come from theme.h/theme.cpp's Theme table (were a local
    // hardcoded VSCode Dark+-inspired palette before WI-09's Theme system -
    // see kDarkTheme's own comment there).
    const Theme& theme = themeForKind(m_themeKind);
    if (!m_keywordBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.keyword, m_keywordBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_typeBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.type, m_typeBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_stringBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.string, m_stringBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_numberBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.number, m_numberBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_commentBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.comment, m_commentBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_preprocessorBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.preprocessor, m_preprocessorBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureLogLevelBrushes(ID2D1DeviceContext6& dc) noexcept {
    // WI-14c: see theme.h/theme.cpp for these colors' values/rationale.
    const Theme& theme = themeForKind(m_themeKind);
    if (!m_logErrorBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.logError, m_logErrorBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_logWarningBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.logWarning, m_logWarningBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

ID2D1SolidColorBrush* RenderPipeline::tokenBrush(syntax::TokenKind kind) noexcept {
    switch (kind) {
        case syntax::TokenKind::Keyword:      return m_keywordBrush.Get();
        case syntax::TokenKind::Type:         return m_typeBrush.Get();
        case syntax::TokenKind::String:       return m_stringBrush.Get();
        case syntax::TokenKind::Number:       return m_numberBrush.Get();
        case syntax::TokenKind::Comment:      return m_commentBrush.Get();
        case syntax::TokenKind::Preprocessor: return m_preprocessorBrush.Get();
        // Text/Variable/Punctuation deliberately unstyled - see this
        // function's declaration comment in render_pipeline.h.
        case syntax::TokenKind::Text:
        case syntax::TokenKind::Variable:
        case syntax::TokenKind::Punctuation:
            return nullptr;
    }
    return nullptr;  // unreachable, every TokenKind enumerator handled above
}

ID2D1SolidColorBrush* RenderPipeline::logLevelBrush(logmode::LogLevel level) noexcept {
    switch (level) {
        case logmode::LogLevel::Error:
        case logmode::LogLevel::Fatal:
            return m_logErrorBrush.Get();
        case logmode::LogLevel::Warning:
            return m_logWarningBrush.Get();
        // Trace/Debug/Info/Unknown deliberately unstyled - see this
        // function's declaration comment in render_pipeline.h.
        case logmode::LogLevel::Trace:
        case logmode::LogLevel::Debug:
        case logmode::LogLevel::Info:
        case logmode::LogLevel::Unknown:
            return nullptr;
    }
    return nullptr;  // unreachable, every LogLevel enumerator handled above
}

void RenderPipeline::drawLogLevelOnLine(IDWriteTextLayout& layout, document::LineNumber line,
                                        document::TextPos lineStart, document::TextPos lineEnd) noexcept {
    if (line >= m_logLineLevels.size()) {
        return;
    }
    ID2D1SolidColorBrush* brush = logLevelBrush(m_logLineLevels[line]);
    if (brush == nullptr) {
        return;
    }
    const DWRITE_TEXT_RANGE dwRange{
        .startPosition = 0,
        .length        = static_cast<UINT32>(lineEnd - lineStart),
    };
    layout.SetDrawingEffect(brush, dwRange);
}

RenderExpected<void> RenderPipeline::ensureIndentGuideBrushes(ID2D1DeviceContext6& dc) noexcept {
    // WI-09: see theme.h/theme.cpp for these colors' values/rationale.
    const Theme& theme = themeForKind(m_themeKind);
    if (!m_indentGuideBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.indentGuide, m_indentGuideBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_activeIndentGuideBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.activeIndentGuide, m_activeIndentGuideBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureBreadcrumbBrush(ID2D1DeviceContext6& dc) noexcept {
    if (m_breadcrumbBackgroundBrush) {
        return {};
    }
    // WI-09: see theme.h/theme.cpp for this color's value/rationale.
    const HRESULT hr = dc.CreateSolidColorBrush(themeForKind(m_themeKind).breadcrumbBackground,
                                                 m_breadcrumbBackgroundBrush.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureMinimapBrushes(ID2D1DeviceContext6& dc) noexcept {
    // WI-09: see theme.h/theme.cpp for these colors' values/rationale.
    const Theme& theme = themeForKind(m_themeKind);
    if (!m_minimapBackgroundBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.minimapBackground, m_minimapBackgroundBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_minimapViewportBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.minimapViewport, m_minimapViewportBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_minimapTextBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.minimapText, m_minimapTextBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_minimapUnpopulatedBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.minimapUnpopulated, m_minimapUnpopulatedBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureImeCompositionBrushes(ID2D1DeviceContext6& dc) noexcept {
    // WI-09: see theme.h/theme.cpp for these colors' values/rationale.
    const Theme& theme = themeForKind(m_themeKind);
    if (!m_imeCompositionBackgroundBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.imeCompositionBackground,
                                                     m_imeCompositionBackgroundBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_imeTargetClauseBrush) {
        const HRESULT hr = dc.CreateSolidColorBrush(theme.imeTargetClause, m_imeTargetClauseBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

std::pair<LineNumber, LineNumber> RenderPipeline::visibleLineRange() const noexcept {
    if (!m_cachedSnapshot || m_document == nullptr || m_lineHeightDips <= 0.0F) {
        return {0, 0};
    }
    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0) {
        return {0, 0};
    }
    const LineNumber startLine =
        m_topLine < totalLines ? m_topLine : static_cast<LineNumber>(totalLines - 1);

    // Phase 7h/7o: the Breadcrumb strip (and, when active, the Sticky scroll
    // strip directly below it) occupies the top reservedTopHeightDips() of
    // the client area, so the effective height available to text lines is
    // reduced by that many (DPI-scaled) pixels - mirrors kGutterWidthDips'
    // effect on drawn width, just on the y-axis. computeVisibleLineCount()
    // itself stays a general-purpose pure function unaware of either strip.
    // WI-07 step4: reservedBottomHeightDips() (the status bar) reduces it
    // further, same reasoning applied to the bottom edge.
    const auto reservedPx = static_cast<std::uint32_t>(
        (reservedTopHeightDips() + reservedBottomHeightDips()) * m_dpiScale);
    const std::uint32_t effectiveHeightPx = m_height > reservedPx ? m_height - reservedPx : 0;
    const std::uint32_t visibleCount = computeVisibleLineCount(effectiveHeightPx, m_dpiScale, m_lineHeightDips);
    if (visibleCount == 0) {
        return {startLine, startLine};
    }
    // Phase 7i: walk forward counting only VISIBLE (non-folded-hidden) lines,
    // so a screen holding `visibleCount` rows can span more than
    // `visibleCount` logical lines when folds are active. m_foldRegions
    // empty (folding disabled/no folds yet) makes this identical to the
    // pre-Phase-7i arithmetic (every logical line counts as visible).
    LineNumber    endLineExclusive = startLine;
    std::uint32_t visibleSeen      = 0;
    while (endLineExclusive < totalLines && visibleSeen < visibleCount) {
        if (!isLineHidden(endLineExclusive)) {
            ++visibleSeen;
        }
        ++endLineExclusive;
    }
    return {startLine, endLineExclusive};
}

std::pair<LineNumber, LineNumber> RenderPipeline::widenedVisibleLineRange() const noexcept {
    const auto [startLine, endLineExclusive] = visibleLineRange();
    const auto reservedPx = static_cast<std::uint32_t>(
        (reservedTopHeightDips() + reservedBottomHeightDips()) * m_dpiScale);
    const std::uint32_t  effectiveHeightPx   = m_height > reservedPx ? m_height - reservedPx : 0;
    const std::uint32_t  visibleCount = computeVisibleLineCount(effectiveHeightPx, m_dpiScale, m_lineHeightDips);
    const std::uint64_t  totalLines   = m_document != nullptr ? m_document->lineCount() : 0;
    return widenLineRangeWithMargin(startLine, endLineExclusive, visibleCount, totalLines);
}

document::TextRange RenderPipeline::computeDesiredTokenRange() const noexcept {
    // Phase 7t: layout info not measured yet (pre-first-resize) - fall back
    // to the whole document, a safe default that resolves itself within a
    // frame or two once the window is sized (mirrors what this class
    // effectively did for every request before Phase 7t).
    if (!m_cachedSnapshot || m_document == nullptr || m_lineHeightDips <= 0.0F || m_dpiScale <= 0.0F ||
        m_height == 0) {
        return TextRange{.start = 0, .end = m_cachedSnapshot ? m_cachedSnapshot->length() : 0};
    }
    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0) {
        return TextRange{.start = 0, .end = 0};
    }
    const auto [marginedStart, marginedEnd] = widenedVisibleLineRange();
    const TextPos startOffset = m_document->lineToOffset(static_cast<LineNumber>(marginedStart));
    const TextPos endOffset   = (marginedEnd >= totalLines)
                                     ? m_cachedSnapshot->length()
                                     : m_document->lineToOffset(static_cast<LineNumber>(marginedEnd));
    return TextRange{.start = startOffset, .end = endOffset};
}

void RenderPipeline::ensureSyntaxTokensCoverVisibleRange() noexcept {
    if (!m_language.has_value() || !m_cachedSnapshot || m_document == nullptr) {
        // Nowhere to send staged edits - drop them rather than let
        // m_pendingSyntaxEdits grow unbounded while highlighting is off.
        m_pendingSyntaxEdits.clear();
        m_forceFullReparseNextRequest = false;
        return;
    }
    const TextRange desired     = computeDesiredTokenRange();
    const bool      rangeCovered = m_hasRequestedTokenRange && m_requestedTokenRange.start <= desired.start &&
                               desired.end <= m_requestedTokenRange.end;
    if (rangeCovered && m_pendingSyntaxEdits.empty() && !m_forceFullReparseNextRequest) {
        return;
    }
    if (!m_syntaxWorker.has_value()) {
        m_syntaxWorker.emplace(m_hwnd);
    }
    m_syntaxWorker->requestParse(m_cachedSnapshot, *m_language, std::move(m_pendingSyntaxEdits),
                                  m_forceFullReparseNextRequest, desired);
    m_pendingSyntaxEdits.clear();
    m_forceFullReparseNextRequest = false;
    m_requestedTokenRange         = desired;
    m_hasRequestedTokenRange      = true;
}

void RenderPipeline::drawVisibleLines(ID2D1DeviceContext6& dc) noexcept {
    // WI-06: reset unconditionally before any early return below - see
    // imeCandidateAnchorPx()'s doc comment for why a stale value must never
    // survive past the frame that produced it (e.g. the composition's
    // anchor line scrolling off-screen, or the document being detached
    // mid-composition). Re-set by drawImeCompositionOnLine() later in this
    // same call if the composition's anchor line is actually drawn below.
    m_imeCandidateAnchorPx = std::nullopt;
    if (!m_cachedSnapshot || m_document == nullptr || m_lineHeightDips <= 0.0F ||
        !m_dwriteFactory) {
        return;
    }

    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0) {
        return;
    }
    const auto [startLine, endLineExclusive] = visibleLineRange();
    if (startLine == endLineExclusive) {
        return;
    }

    const TextPos startOffset = m_document->lineToOffset(startLine);
    const TextPos endOffset   = (endLineExclusive >= totalLines)
                                     ? m_cachedSnapshot->length()
                                     : m_document->lineToOffset(endLineExclusive);
    const std::u16string text =
        m_cachedSnapshot->extract(TextRange{.start = startOffset, .end = endOffset});

    const std::vector<CaretDraw> caretDraws = computeCaretDraws();
    std::size_t tokenCursor = 0;  // Phase 7b: threaded forward across the line loop, see drawTokensOnLine()'s comment

    // WI-03: recomputed fresh from the lines actually walked below every
    // call - see maxVisibleLineLength()'s comment for why this deliberately
    // stays windowed rather than a whole-document scan.
    std::uint32_t maxLineLength = 0;

    std::u16string_view remaining(text);
    float                y         = reservedTopHeightDips();  // Phase 7h/7o: reserve the strip(s) above
    TextPos              lineStart = startOffset;
    for (LineNumber line = startLine; line < endLineExclusive; ++line) {
        const auto newlinePos = remaining.find(u'\n');
        const std::u16string_view lineSpan =
            (newlinePos == std::u16string_view::npos) ? remaining : remaining.substr(0, newlinePos);
        const TextPos lineEnd = lineStart + lineSpan.size();

        // Phase 7i: hidden lines (inside a folded region, not its header)
        // are walked (to advance lineStart/remaining correctly) but never
        // drawn, and y does not advance for them - the next visible line
        // simply lands where this one would have.
        if (!isLineHidden(line)) {
            drawTextLine(dc, line, y, lineSpan, lineStart, lineEnd, caretDraws, tokenCursor);
            y += m_lineHeightDips;
            maxLineLength = std::max(maxLineLength, static_cast<std::uint32_t>(lineSpan.size()));
        }
        if (newlinePos == std::u16string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;  // +1 for the '\n' this line's span excluded
        remaining = remaining.substr(newlinePos + 1);
    }
    m_maxVisibleLineLength = maxLineLength;
}

void RenderPipeline::drawTextLine(ID2D1DeviceContext6& dc, LineNumber line, float y,
                                  std::u16string_view lineSpan, TextPos lineStart, TextPos lineEnd,
                                  const std::vector<CaretDraw>& caretDraws,
                                  std::size_t& tokenCursor) noexcept {
    const auto layoutResult =
        m_layoutCache.getOrCreate(line, lineSpan, *m_dwriteFactory.Get(), *m_textFormat.Get(),
                                  kMaxLayoutWidthDips, kMaxLayoutHeightDips);
    // A layout-creation failure for a single line is no worse than the
    // pre-Phase-3c behavior of DrawText() silently failing per-call - it
    // skips just that line, not the whole frame.
    if (!layoutResult.has_value()) {
        return;
    }
    // WI-03: clips everything from here through the folded-header marker
    // below to [kGutterWidthDips, width) so a line scrolled right by
    // leftColumnOffsetDips() never visually bleeds into the gutter -
    // drawGutterOnLine() (called after PopAxisAlignedClip below) paints no
    // opaque background over [0, kGutterWidthDips), unlike the minimap/
    // Breadcrumb/Sticky scroll (all drawn AFTER drawVisibleLines() in
    // renderOnce() and self-overpaint any bleed-through - this asymmetry is
    // why only the gutter needs an explicit clip).
    const float widthDips  = static_cast<float>(m_width) / m_dpiScale;
    const float heightDips = static_cast<float>(m_height) / m_dpiScale;
    dc.PushAxisAlignedClip(D2D1::RectF(gutterWidthDips(), 0.0F, widthDips, heightDips),
                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Drawn before DrawTextLayout so glyphs render on top of the highlight
    // (Phase 4b2, N-cursor generalization Phase 4b7a). Matches drawn first
    // (Phase 5b3a) so an active text selection layers visibly above match
    // highlighting, both still behind the glyphs. Token colors (Phase 7b)
    // are applied to the layout itself (not a background rect), so they
    // must be set before DrawTextLayout - order relative to the two
    // highlight calls above doesn't matter.
    drawMatchesOnLine(dc, **layoutResult, y, lineStart, lineEnd);
    drawSelectionsOnLine(dc, **layoutResult, y, lineStart, lineEnd);
    // Phase 7e: a background element like the two calls above, so it must
    // run before DrawTextLayout too - see this method's declaration comment
    // for the isActiveLine approximation.
    const bool isActiveLine = std::ranges::any_of(
        caretDraws, [line](const CaretDraw& caret) { return caret.line == line; });
    drawIndentGuidesOnLine(dc, y, lineSpan, isActiveLine);
    drawTokensOnLine(**layoutResult, lineStart, lineEnd, tokenCursor);
    // WI-14c: runs after drawTokensOnLine() so a log-severity color always
    // wins over any overlapping token color - see this method's own
    // declaration comment.
    drawLogLevelOnLine(**layoutResult, line, lineStart, lineEnd);
    // WI-03: shifted left by leftColumnOffsetDips() so scrolling right moves
    // the glyphs, not kGutterWidthDips itself (which never changes).
    dc.DrawTextLayout(D2D1::Point2F(gutterWidthDips() - leftColumnOffsetDips(), y), *layoutResult,
                      m_textBrush.Get());
    drawCaretsOnLine(dc, **layoutResult, y, line, caretDraws);
    // WI-06: overlay, drawn on top of the caret(s) above - no-op unless the
    // active composition's anchor is on this line. See
    // drawImeCompositionOnLine()'s own declaration comment.
    drawImeCompositionOnLine(dc, **layoutResult, y, line, lineStart);
    // Phase 7i: a folded header shows its own text (drawn above) plus a
    // short " {...}" marker past it, standing in for the hidden body. Stays
    // inside the clip (WI-03) - it's text-derived content, same as the
    // glyphs it follows.
    const auto foldedHeader = std::ranges::find_if(
        m_foldRegions, [line](const FoldVisual& r) { return r.folded && r.headerLine == line; });
    if (foldedHeader != m_foldRegions.end()) {
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED((*layoutResult)->GetMetrics(&metrics))) {
            drawFoldedHeaderMarker(dc, gutterWidthDips() - leftColumnOffsetDips() + metrics.width, y);
        }
    }
    dc.PopAxisAlignedClip();
    drawGutterOnLine(dc, y, line);
}

bool RenderPipeline::isLineHidden(document::LineNumber line) const noexcept {
    const bool foldHidden = std::ranges::any_of(m_foldRegions, [line](const FoldVisual& region) {
        return region.folded && line > region.headerLine && line <= region.endLineInclusive;
    });
    if (foldHidden) {
        return true;
    }
    // WI-14c: log-level filter. m_logLineLevels being shorter than `line`
    // (log mode disabled, or a stale array from a since-swapped document)
    // means "no filtering opinion" rather than "hidden" - same fail-open
    // default as m_foldRegions being empty.
    if (line < m_logLineLevels.size()) {
        const auto bit = logmode::logLevelFilterBit(m_logLineLevels[line]);
        if ((m_logLevelFilterMask & bit) == 0) {
            return true;
        }
    }
    return false;
}

void RenderPipeline::drawFoldedHeaderMarker(ID2D1DeviceContext6& dc, float x, float y) noexcept {
    if (!m_dwriteFactory || !m_textFormat || !m_textBrush) {
        return;
    }
    // One-off layout, not TextLayoutCache - see drawBreadcrumb()'s identical
    // rationale (this string is synthesized fresh, not keyed by line number).
    constexpr std::u16string_view kMarker = u" {…}";
    const std::wstring_view       wMarker = util::toWstringView(kMarker);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    const HRESULT hr = m_dwriteFactory->CreateTextLayout(
        wMarker.data(), static_cast<UINT32>(wMarker.size()), m_textFormat.Get(), kMaxLayoutWidthDips,
        kMaxLayoutHeightDips, layout.GetAddressOf());
    if (FAILED(hr) || !layout) {
        return;
    }
    dc.DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), m_textBrush.Get());
}

void RenderPipeline::drawImeCompositionOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& realLineLayout,
                                              float y, LineNumber line, TextPos lineStart) noexcept {
    if (!m_imeComposition || !m_document || !m_dwriteFactory || !m_textFormat || !m_textBrush ||
        !m_imeCompositionBackgroundBrush || !m_imeTargetClauseBrush) {
        return;
    }
    const ImeComposition& composition = *m_imeComposition;
    if (m_document->offsetToLine(composition.anchorRange.start) != line) {
        return;
    }
    const auto column = static_cast<std::uint32_t>(composition.anchorRange.start - lineStart);

    // Anchor pixel origin = where the caret sits in the REAL line's layout
    // (same HitTestTextPosition() call drawCaretOnLine() makes) - the
    // composition text is an OVERLAY, not a splice, so it must align with
    // the actual glyphs it's drawn on top of, not with its own disposable
    // layout's coordinate space.
    DWRITE_HIT_TEST_METRICS anchorMetrics{};
    float anchorX = 0.0F;
    float anchorY = 0.0F;
    if (FAILED(realLineLayout.HitTestTextPosition(column, FALSE, &anchorX, &anchorY, &anchorMetrics))) {
        return;
    }
    // See drawCaretOnLine()'s comment - layout-local coordinates need the
    // gutter offset (minus leftColumnOffsetDips(), WI-03) added explicitly.
    const float leftDip = gutterWidthDips() - leftColumnOffsetDips() + anchorX;

    const std::wstring_view wText = util::toWstringView(composition.text);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    const HRESULT hr = m_dwriteFactory->CreateTextLayout(
        wText.data(), static_cast<UINT32>(wText.size()), m_textFormat.Get(), kMaxLayoutWidthDips,
        kMaxLayoutHeightDips, layout.GetAddressOf());
    if (FAILED(hr) || !layout) {
        return;
    }
    if (!wText.empty()) {
        // DirectWrite's native underline formatting - no manual line-drawing
        // needed (unlike drawGutterOnLine()'s fold-marker triangle, the only
        // other hand-drawn-line precedent in this file, which is a
        // decorative glyph rather than text underlining).
        layout->SetUnderline(TRUE, DWRITE_TEXT_RANGE{.startPosition = 0, .length = static_cast<UINT32>(wText.size())});
    }

    DWRITE_TEXT_METRICS compositionMetrics{};
    if (FAILED(layout->GetMetrics(&compositionMetrics))) {
        return;
    }
    // Opaque background sized to the composition's OWN measured width -
    // occludes whatever real trailing glyphs on this line it overlaps. An
    // accepted, documented overlay trade-off (see this method's declaration
    // comment) - not sized to cover a wider pre-existing selection.
    dc.FillRectangle(D2D1::RectF(leftDip, y, leftDip + compositionMetrics.width, y + m_lineHeightDips),
                     m_imeCompositionBackgroundBrush.Get());

    if (composition.targetClauseRange) {
        const auto [clauseStart, clauseEnd] = *composition.targetClauseRange;
        DWRITE_HIT_TEST_METRICS startMetrics{};
        DWRITE_HIT_TEST_METRICS endMetrics{};
        float startX = 0.0F;
        float startY = 0.0F;
        float endX   = 0.0F;
        float endY   = 0.0F;
        if (SUCCEEDED(layout->HitTestTextPosition(clauseStart, FALSE, &startX, &startY, &startMetrics)) &&
            SUCCEEDED(layout->HitTestTextPosition(clauseEnd, FALSE, &endX, &endY, &endMetrics))) {
            dc.FillRectangle(D2D1::RectF(leftDip + startX, y, leftDip + endX, y + m_lineHeightDips),
                             m_imeTargetClauseBrush.Get());
        }
    }

    dc.DrawTextLayout(D2D1::Point2F(leftDip, y), layout.Get(), m_textBrush.Get());

    // Side effect (Phase 7w's m_maxVisibleLineLength precedent: computed
    // once during the draw walk that already has every coordinate needed).
    // DEVICE PIXELS (not DIPs) - MainWindow::setImeCandidatePosition()'s
    // contract - positioned directly below the composition text so the IME
    // candidate list appears under what the user is actively typing.
    const float belowY = y + m_lineHeightDips;
    m_imeCandidateAnchorPx = POINT{
        .x = static_cast<LONG>(leftDip * m_dpiScale),
        .y = static_cast<LONG>(belowY * m_dpiScale),
    };
}

std::vector<RenderPipeline::CaretDraw> RenderPipeline::computeCaretDraws() const noexcept {
    std::vector<CaretDraw> draws;
    draws.reserve(m_cursorVisuals.size());
    for (const CursorVisual& cv : m_cursorVisuals) {
        const LineNumber cursorLine = m_document->offsetToLine(cv.position);
        draws.push_back(CaretDraw{
            .line   = cursorLine,
            .column = static_cast<std::uint32_t>(cv.position - m_document->lineToOffset(cursorLine)),
            .virtualColumnOffset = cv.virtualColumnOffset,
        });
    }
    return draws;
}

void RenderPipeline::drawCaretsOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                      LineNumber line, const std::vector<CaretDraw>& caretDraws) noexcept {
    for (const CaretDraw& caret : caretDraws) {
        if (caret.line == line) {
            drawCaretOnLine(dc, layout, y, caret.column, caret.virtualColumnOffset);
        }
    }
}

void RenderPipeline::drawSelectionsOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                          TextPos lineStart, TextPos lineEnd) noexcept {
    for (const CursorVisual& cv : m_cursorVisuals) {
        if (cv.selectionRange.empty()) {
            continue;
        }
        const TextPos selStart     = std::min(cv.selectionRange.start, cv.selectionRange.end);
        const TextPos selEnd       = std::max(cv.selectionRange.start, cv.selectionRange.end);
        const TextPos overlapStart = std::max(lineStart, selStart);
        const TextPos overlapEnd   = std::min(lineEnd, selEnd);
        if (overlapStart < overlapEnd) {
            drawSelectionOnLine(dc, layout, y, static_cast<std::uint32_t>(overlapStart - lineStart),
                               static_cast<std::uint32_t>(overlapEnd - lineStart));
        }
    }
}

void RenderPipeline::drawMatchesOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                       TextPos lineStart, TextPos lineEnd) noexcept {
    for (const MatchVisual& match : m_matchVisuals) {
        if (match.range.empty()) {
            continue;
        }
        const TextPos overlapStart = std::max(lineStart, match.range.start);
        const TextPos overlapEnd   = std::min(lineEnd, match.range.end);
        if (overlapStart < overlapEnd) {
            drawMatchOnLine(dc, layout, y, static_cast<std::uint32_t>(overlapStart - lineStart),
                           static_cast<std::uint32_t>(overlapEnd - lineStart), match.isCurrent);
        }
    }
}

void RenderPipeline::drawCaretOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                     std::uint32_t column,
                                     std::uint32_t virtualColumnOffset) noexcept {
    if (!m_textBrush) {
        return;
    }
    DWRITE_HIT_TEST_METRICS metrics{};
    float                    caretX = 0.0F;
    float                    caretY = 0.0F;
    const HRESULT hr = layout.HitTestTextPosition(column, FALSE, &caretX, &caretY, &metrics);
    if (FAILED(hr)) {
        return;
    }
    if (virtualColumnOffset > 0) {
        caretX += static_cast<float>(virtualColumnOffset) * m_charWidthDips;
    }
    constexpr float kCaretWidthDips = 1.5F;
    // HitTestTextPosition() returns coordinates local to `layout`'s own
    // origin (0,0), independent of whatever origin DrawTextLayout() is
    // called with - kGutterWidthDips must be added explicitly here to line
    // up with the glyphs, which DO get shifted by DrawTextLayout()'s origin
    // parameter (Phase 4b8c, confirmed by reading the actual D2D/DWrite
    // call sequence - see drawVisibleLines()). WI-03: leftColumnOffsetDips()
    // subtracted the same way DrawTextLayout()'s own origin now is.
    const float leftDip = gutterWidthDips() - leftColumnOffsetDips();
    const D2D1_RECT_F caretRect = D2D1::RectF(leftDip + caretX, y,
                                              leftDip + caretX + kCaretWidthDips,
                                              y + m_lineHeightDips);
    dc.FillRectangle(caretRect, m_textBrush.Get());
}

void RenderPipeline::drawSelectionOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                         std::uint32_t startColumn, std::uint32_t endColumn) noexcept {
    if (!m_selectionBrush) {
        return;
    }
    DWRITE_HIT_TEST_METRICS startMetrics{};
    DWRITE_HIT_TEST_METRICS endMetrics{};
    float   startX = 0.0F;
    float   startY = 0.0F;
    float   endX   = 0.0F;
    float   endY   = 0.0F;
    HRESULT hr = layout.HitTestTextPosition(startColumn, FALSE, &startX, &startY, &startMetrics);
    if (FAILED(hr)) {
        return;
    }
    hr = layout.HitTestTextPosition(endColumn, FALSE, &endX, &endY, &endMetrics);
    if (FAILED(hr)) {
        return;
    }
    // See drawCaretOnLine()'s comment - layout-local coordinates need the
    // gutter offset (minus leftColumnOffsetDips(), WI-03) added explicitly
    // (Phase 4b8c).
    const float leftDip = gutterWidthDips() - leftColumnOffsetDips();
    const D2D1_RECT_F selectionRect = D2D1::RectF(leftDip + startX, y, leftDip + endX,
                                                  y + m_lineHeightDips);
    dc.FillRectangle(selectionRect, m_selectionBrush.Get());
}

void RenderPipeline::drawMatchOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                     std::uint32_t startColumn, std::uint32_t endColumn,
                                     bool isCurrent) noexcept {
    ID2D1SolidColorBrush* brush = isCurrent ? m_currentMatchBrush.Get() : m_matchBrush.Get();
    if (brush == nullptr) {
        return;
    }
    DWRITE_HIT_TEST_METRICS startMetrics{};
    DWRITE_HIT_TEST_METRICS endMetrics{};
    float   startX = 0.0F;
    float   startY = 0.0F;
    float   endX   = 0.0F;
    float   endY   = 0.0F;
    HRESULT hr = layout.HitTestTextPosition(startColumn, FALSE, &startX, &startY, &startMetrics);
    if (FAILED(hr)) {
        return;
    }
    hr = layout.HitTestTextPosition(endColumn, FALSE, &endX, &endY, &endMetrics);
    if (FAILED(hr)) {
        return;
    }
    // See drawCaretOnLine()'s comment - layout-local coordinates need the
    // gutter offset (minus leftColumnOffsetDips(), WI-03) added explicitly
    // (Phase 4b8c).
    const float leftDip = gutterWidthDips() - leftColumnOffsetDips();
    const D2D1_RECT_F matchRect =
        D2D1::RectF(leftDip + startX, y, leftDip + endX, y + m_lineHeightDips);
    dc.FillRectangle(matchRect, brush);
}

void RenderPipeline::drawGutterOnLine(ID2D1DeviceContext6& dc, float y, LineNumber line) noexcept {
    // WI-07 step7: 1-based line number, right-aligned to the same right
    // margin the fold-marker chevron below uses (gutterWidthDips() - 4.0F) -
    // drawn FIRST so the bookmark dot/fold marker layer visibly on top of it
    // (mirrors drawTextLine()'s "background element before glyphs" order for
    // selection/match highlights vs. token-colored text). One-off
    // IDWriteTextLayout per frame, not TextLayoutCache - that cache is keyed
    // by document line number and reused across frames for that line's
    // CONTENT layout (drawTextLine()'s getOrCreate() call); reusing it here
    // for the line-number label would collide with that same key.
    if (m_showLineNumbers && m_dwriteFactory && m_textFormat && m_lineNumberBrush) {
        const std::wstring number       = std::to_wstring(line + 1);
        const float         maxWidthDips = std::max(0.0F, gutterWidthDips() - 4.0F);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> numberLayout;
        const HRESULT hr = m_dwriteFactory->CreateTextLayout(
            number.c_str(), static_cast<UINT32>(number.size()), m_textFormat.Get(), maxWidthDips,
            m_lineHeightDips, numberLayout.GetAddressOf());
        if (SUCCEEDED(hr) && numberLayout) {
            numberLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            dc.DrawTextLayout(D2D1::Point2F(0.0F, y), numberLayout.Get(), m_lineNumberBrush.Get());
        }
    }

    if (m_bookmarkBrush && std::ranges::find(m_bookmarkedLines, line) != m_bookmarkedLines.end()) {
        const float centerX = gutterWidthDips() / 2.0F;
        const float centerY = y + (m_lineHeightDips / 2.0F);
        const float radius  = kBookmarkDotSizeDips / 2.0F;
        const D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(centerX, centerY), radius, radius);
        dc.FillEllipse(dot, m_bookmarkBrush.Get());
    }

    // WI-17c: a thin vertical bar at the gutter's left edge (VSCode/GitLens
    // convention) for any Git diff hunk covering this line. m_gitDiffMarkers
    // is small (hunk-granularity, not one-per-line, same as m_bookmarkedLines/
    // m_foldRegions above) so a linear scan per visible line is fine - same
    // reasoning the bookmark block above already relies on. Placed BEFORE
    // the fold-marker block below deliberately - that block early-returns
    // out of this entire function for any non-fold-header line (the common
    // case), which would make this block unreachable if it came after (a
    // real bug this WI's own dogfooding step caught: markers never painted
    // for any file, even one with diff hunks, because m_foldRegions is
    // empty for a file with no folding and every line hit that early return
    // first).
    for (const GitDiffMarker& marker : m_gitDiffMarkers) {
        if (marker.kind == GitDiffKind::Deleted) {
            if (marker.startLine != line || !m_diffDeletedBrush) {
                continue;
            }
            const D2D1_RECT_F bar =
                D2D1::RectF(0.0F, y, kGitDiffBarWidthDips, y + kGitDiffDeletedMarkerHeightDips);
            dc.FillRectangle(bar, m_diffDeletedBrush.Get());
            continue;
        }
        if (line < marker.startLine || line >= marker.startLine + marker.lineCount) {
            continue;
        }
        ID2D1SolidColorBrush* brush =
            marker.kind == GitDiffKind::Added ? m_diffAddedBrush.Get() : m_diffModifiedBrush.Get();
        if (brush == nullptr) {
            continue;
        }
        const D2D1_RECT_F bar = D2D1::RectF(0.0F, y, kGitDiffBarWidthDips, y + m_lineHeightDips);
        dc.FillRectangle(bar, brush);
    }

    // Phase 7i: a small chevron at the gutter's right edge for any fold
    // header line (folded or not) - drawn with DrawLine() rather than a
    // ID2D1PathGeometry so no COM geometry object is allocated per visible
    // line per frame. ">" (pointing right) while folded, "v" (pointing
    // down, disclosure-triangle convention) while expanded. Click handling
    // is deliberately deferred to a later sub-phase (Phase 7i plan's
    // Context point 6) - this is a visual-only indicator for now.
    if (!m_foldMarkerBrush) {
        return;
    }
    const auto headerRegion = std::ranges::find(m_foldRegions, line, &FoldVisual::headerLine);
    if (headerRegion == m_foldRegions.end()) {
        return;
    }
    constexpr float kMarkerHalfSize = 3.5F;
    const float     markerRight     = gutterWidthDips() - 4.0F;
    const float     markerLeft      = markerRight - (kMarkerHalfSize * 2.0F);
    const float     centerY         = y + (m_lineHeightDips / 2.0F);
    if (headerRegion->folded) {
        dc.DrawLine(D2D1::Point2F(markerLeft, centerY - kMarkerHalfSize), D2D1::Point2F(markerRight, centerY),
                    m_foldMarkerBrush.Get(), 1.5F);
        dc.DrawLine(D2D1::Point2F(markerRight, centerY), D2D1::Point2F(markerLeft, centerY + kMarkerHalfSize),
                    m_foldMarkerBrush.Get(), 1.5F);
    } else {
        const float midX = (markerLeft + markerRight) / 2.0F;
        dc.DrawLine(D2D1::Point2F(markerLeft, centerY - kMarkerHalfSize), D2D1::Point2F(midX, centerY),
                    m_foldMarkerBrush.Get(), 1.5F);
        dc.DrawLine(D2D1::Point2F(markerRight, centerY - kMarkerHalfSize), D2D1::Point2F(midX, centerY),
                    m_foldMarkerBrush.Get(), 1.5F);
    }
}

void RenderPipeline::drawTokensOnLine(IDWriteTextLayout& layout, TextPos lineStart, TextPos lineEnd,
                                      std::size_t& tokenCursor) noexcept {
    // Retire tokens that ended at or before this line's start - m_tokens is
    // sorted left-to-right (see this method's declaration comment), so once
    // a token is behind us it never needs revisiting. A token spanning
    // multiple lines (e.g. a block comment) is NOT retired here - its
    // range.end still lies past lineStart, so it stays at/after tokenCursor
    // and gets reconsidered by the next line's call too.
    while (tokenCursor < m_tokens.size() && m_tokens[tokenCursor].range.end <= lineStart) {
        ++tokenCursor;
    }
    for (std::size_t i = tokenCursor; i < m_tokens.size(); ++i) {
        const syntax::Token& token = m_tokens[i];
        if (token.range.start >= lineEnd) {
            break;  // sorted - nothing from here on can overlap this line either
        }
        ID2D1SolidColorBrush* brush = tokenBrush(token.kind);
        if (brush == nullptr) {
            continue;  // Text/Variable/Punctuation - falls through to DrawTextLayout()'s default brush
        }
        const TextPos overlapStart = std::max(lineStart, token.range.start);
        const TextPos overlapEnd   = std::min(lineEnd, token.range.end);
        if (overlapStart >= overlapEnd) {
            continue;
        }
        const DWRITE_TEXT_RANGE dwRange{
            .startPosition = static_cast<UINT32>(overlapStart - lineStart),
            .length        = static_cast<UINT32>(overlapEnd - overlapStart),
        };
        layout.SetDrawingEffect(brush, dwRange);
    }
}

void RenderPipeline::drawIndentGuidesOnLine(ID2D1DeviceContext6& dc, float y,
                                            std::u16string_view lineSpan, bool isActiveLine) noexcept {
    if (!m_indentGuideBrush || !m_activeIndentGuideBrush) {
        return;
    }
    // WI-08: m_tabWidth (settings-driven, default 4) replaces this
    // function's former local constexpr kTabWidth - see this class's
    // header comment on setTabWidth() for the other former copy
    // (editor_input.cpp's tab<->space conversion) this unifies with.
    constexpr float kIndentGuideWidthDips = 1.0F;
    const std::uint32_t indentColumns = computeIndentColumns(lineSpan, m_tabWidth);
    const std::uint32_t guideCount    = computeIndentGuideCount(indentColumns, m_tabWidth);
    ID2D1SolidColorBrush* brush = isActiveLine ? m_activeIndentGuideBrush.Get() : m_indentGuideBrush.Get();
    for (std::uint32_t level = 1; level <= guideCount; ++level) {
        // WI-03: leftColumnOffsetDips() subtracted, same as every other
        // text-derived X coordinate in this file.
        const float x = gutterWidthDips() - leftColumnOffsetDips() +
                        (static_cast<float>(level * m_tabWidth) * m_charWidthDips);
        const D2D1_RECT_F guideRect =
            D2D1::RectF(x, y, x + kIndentGuideWidthDips, y + m_lineHeightDips);
        dc.FillRectangle(guideRect, brush);
    }
}

void RenderPipeline::drawBreadcrumb(ID2D1DeviceContext6& dc) noexcept {
    // No primary cursor to anchor the path on (e.g. no cursors set at all,
    // as in a bare render-smoke test) - draw nothing, not even the
    // background band, rather than show an empty strip with no meaning.
    const auto primaryIt = std::ranges::find_if(
        m_cursorVisuals, [](const CursorVisual& cursor) { return cursor.isPrimary; });
    if (primaryIt == m_cursorVisuals.end() || !m_breadcrumbBackgroundBrush) {
        return;
    }

    const float widthDips = static_cast<float>(m_width) / m_dpiScale;
    // WI-05: shifted down by m_tabBarHeightDips (0.0F when no tab bar
    // exists, so this is a no-op pre-WI-05) - see setTabBarHeightDips()'s
    // comment for why this class must not draw under the native tab strip.
    dc.FillRectangle(
        D2D1::RectF(0.0F, m_tabBarHeightDips, widthDips, m_tabBarHeightDips + kBreadcrumbHeightDips),
        m_breadcrumbBackgroundBrush.Get());

    const std::vector<const syntax::OutlineNode*> path =
        syntax::findBreadcrumbPath(primaryIt->position, m_cachedOutline);
    if (path.empty() || !m_dwriteFactory || !m_textFormat || !m_textBrush) {
        return;  // background band alone still communicates "no symbol here"
    }

    std::u16string joined;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            joined += u" > ";
        }
        joined += path[i]->name;
    }

    // One-off layout, not TextLayoutCache (that cache is keyed by document
    // line number and reused across frames for unchanged lines - this string
    // is synthesized fresh from m_cachedOutline every frame and has no line
    // number to key on; the cost of laying out one short line per frame is
    // negligible next to drawVisibleLines()' per-visible-line work).
    const std::wstring_view wJoined = util::toWstringView(joined);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    const HRESULT hr = m_dwriteFactory->CreateTextLayout(
        wJoined.data(), static_cast<UINT32>(wJoined.size()), m_textFormat.Get(),
        std::max(0.0F, widthDips - gutterWidthDips()), kBreadcrumbHeightDips, layout.GetAddressOf());
    if (FAILED(hr) || !layout) {
        return;
    }
    dc.DrawTextLayout(D2D1::Point2F(gutterWidthDips(), m_tabBarHeightDips), layout.Get(), m_textBrush.Get());
}

std::optional<FoldVisual> RenderPipeline::stickyScrollRegionAt(LineNumber topLine) const noexcept {
    const FoldVisual* best = nullptr;
    for (const auto& region : m_foldRegions) {
        if (region.folded) {
            continue;  // hidden body - nothing to have "scrolled into"
        }
        if (region.headerLine < topLine && region.endLineInclusive >= topLine &&
            (best == nullptr || region.headerLine > best->headerLine)) {
            best = &region;
        }
    }
    return best != nullptr ? std::optional<FoldVisual>(*best) : std::nullopt;
}

float RenderPipeline::reservedTopHeightDips() const noexcept {
    if (m_document == nullptr) {
        return m_tabBarHeightDips + kBreadcrumbHeightDips;
    }
    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0) {
        return m_tabBarHeightDips + kBreadcrumbHeightDips;
    }
    const LineNumber startLine =
        m_topLine < totalLines ? m_topLine : static_cast<LineNumber>(totalLines - 1);
    const bool hasSticky = stickyScrollRegionAt(startLine).has_value();
    return m_tabBarHeightDips + kBreadcrumbHeightDips + (hasSticky ? kStickyScrollHeightDips : 0.0F);
}

float RenderPipeline::leftColumnOffsetDips() const noexcept {
    return static_cast<float>(m_leftColumn) * m_charWidthDips;
}

float RenderPipeline::gutterWidthDips() const noexcept {
    // WI-08: hidden line numbers fall back to the pre-WI-07 flat width
    // (bookmark/fold-marker column only) instead of growing for digit
    // count that's no longer drawn.
    if (!m_showLineNumbers) {
        return kGutterWidthDips;
    }
    const std::uint64_t totalLines = m_document != nullptr ? m_document->lineCount() : 0;
    return computeGutterWidthDips(totalLines, m_charWidthDips, kGutterWidthDips);
}

float RenderPipeline::minimapWidthDips() const noexcept {
    return m_showMinimap ? kMinimapWidthDips : 0.0F;
}

std::uint32_t RenderPipeline::visibleColumnCount() const noexcept {
    if (m_dpiScale <= 0.0F) {
        return 0;
    }
    const float availableWidthDips =
        (static_cast<float>(m_width) / m_dpiScale) - gutterWidthDips() - minimapWidthDips();
    return computeVisibleColumnCount(availableWidthDips, m_charWidthDips);
}

std::u16string RenderPipeline::extractLineText(LineNumber line) const noexcept {
    const std::uint64_t totalLines = m_document->lineCount();
    const TextPos        lineStart = m_document->lineToOffset(line);
    const LineNumber      nextLine  = line + 1;
    const TextPos lineEnd =
        (nextLine < totalLines) ? m_document->lineToOffset(nextLine) : m_cachedSnapshot->length();
    std::u16string text = m_cachedSnapshot->extract(TextRange{.start = lineStart, .end = lineEnd});
    if (!text.empty() && text.back() == u'\n') {
        text.pop_back();
    }
    return text;
}

void RenderPipeline::drawStickyScroll(ID2D1DeviceContext6& dc) noexcept {
    if (!m_cachedSnapshot || m_document == nullptr || !m_breadcrumbBackgroundBrush) {
        return;
    }
    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0) {
        return;
    }
    const LineNumber startLine =
        m_topLine < totalLines ? m_topLine : static_cast<LineNumber>(totalLines - 1);
    const auto sticky = stickyScrollRegionAt(startLine);
    if (!sticky) {
        return;  // nothing enclosing the current scroll position - draw nothing, reserve no height
    }

    const float widthDips = static_cast<float>(m_width) / m_dpiScale;
    // WI-05: shifted down by m_tabBarHeightDips - same reasoning as
    // drawBreadcrumb()'s own edit above.
    dc.FillRectangle(D2D1::RectF(0.0F, m_tabBarHeightDips + kBreadcrumbHeightDips, widthDips,
                                  m_tabBarHeightDips + kBreadcrumbHeightDips + kStickyScrollHeightDips),
                      m_breadcrumbBackgroundBrush.Get());
    if (!m_dwriteFactory || !m_textFormat || !m_textBrush) {
        return;
    }

    // One-off layout, not TextLayoutCache - same rationale as
    // drawBreadcrumb()'s synthesized-path layout above (this call is cheap
    // relative to drawVisibleLines()' per-visible-line work, and keying a
    // second cache by "whichever line happens to be sticky this frame" would
    // add complexity without a measured need, CLAUDE.md rule 10).
    const std::u16string lineText = extractLineText(sticky->headerLine);
    const std::wstring_view wText = util::toWstringView(lineText);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    const HRESULT hr = m_dwriteFactory->CreateTextLayout(
        wText.data(), static_cast<UINT32>(wText.size()), m_textFormat.Get(),
        std::max(0.0F, widthDips - gutterWidthDips()), kStickyScrollHeightDips, layout.GetAddressOf());
    if (FAILED(hr) || !layout) {
        return;
    }
    dc.DrawTextLayout(D2D1::Point2F(gutterWidthDips(), m_tabBarHeightDips + kBreadcrumbHeightDips),
                      layout.Get(), m_textBrush.Get());
}

float RenderPipeline::minimapLeftDips() const noexcept {
    if (m_dpiScale <= 0.0F) {
        return 0.0F;
    }
    // WI-08: minimapWidthDips() collapses to 0.0F when the minimap is
    // hidden, so this becomes the client-area's own right edge (nothing
    // reserved) rather than leaving a blank strip.
    return (static_cast<float>(m_width) / m_dpiScale) - minimapWidthDips();
}

RenderPipeline::MinimapLineColorState RenderPipeline::classifyTokenKindForMinimap(syntax::TokenKind kind) noexcept {
    switch (kind) {
        case syntax::TokenKind::Keyword:      return MinimapLineColorState::Keyword;
        case syntax::TokenKind::Type:         return MinimapLineColorState::Type;
        case syntax::TokenKind::String:       return MinimapLineColorState::String;
        case syntax::TokenKind::Number:       return MinimapLineColorState::Number;
        case syntax::TokenKind::Comment:      return MinimapLineColorState::Comment;
        case syntax::TokenKind::Preprocessor: return MinimapLineColorState::Preprocessor;
        // Text/Variable/Punctuation deliberately unstyled - mirrors
        // tokenBrush()'s own grouping.
        case syntax::TokenKind::Text:
        case syntax::TokenKind::Variable:
        case syntax::TokenKind::Punctuation:
            return MinimapLineColorState::PlainText;
    }
    return MinimapLineColorState::PlainText;  // unreachable, every TokenKind enumerator handled above
}

ID2D1SolidColorBrush* RenderPipeline::minimapBrushForState(MinimapLineColorState state) noexcept {
    switch (state) {
        case MinimapLineColorState::Unpopulated:  return m_minimapUnpopulatedBrush.Get();
        case MinimapLineColorState::PlainText:    return m_minimapTextBrush.Get();
        case MinimapLineColorState::Keyword:      return m_keywordBrush.Get();
        case MinimapLineColorState::Type:         return m_typeBrush.Get();
        case MinimapLineColorState::String:       return m_stringBrush.Get();
        case MinimapLineColorState::Number:       return m_numberBrush.Get();
        case MinimapLineColorState::Comment:      return m_commentBrush.Get();
        case MinimapLineColorState::Preprocessor: return m_preprocessorBrush.Get();
    }
    return m_minimapUnpopulatedBrush.Get();  // unreachable, every state handled above
}

document::TextRange RenderPipeline::minimapLineSpan(LineNumber line, std::uint64_t totalLines) const noexcept {
    const TextPos lineStart    = m_document->lineToOffset(line);
    const bool     hasNextLine = (line + 1) < totalLines;
    const TextPos lineEndIncNl = hasNextLine ? m_document->lineToOffset(line + 1) : m_cachedSnapshot->length();
    // Same "drop the trailing '\n'" convention extractLineText() uses.
    const TextPos lineEnd = (hasNextLine && lineEndIncNl > lineStart) ? lineEndIncNl - 1 : lineEndIncNl;
    return TextRange{.start = lineStart, .end = lineEnd};
}

RenderPipeline::MinimapLineColorState RenderPipeline::classifyLineForMinimap(TextPos lineStart, TextPos lineEnd,
                                                                              std::size_t& tokenCursor) noexcept {
    if (lineEnd <= lineStart) {
        return MinimapLineColorState::PlainText;  // width-based skip happens at draw time either way
    }
    // Same "monotonic sweep over sorted m_tokens" contract drawTokensOnLine()
    // uses - populateMinimapColorsForRequestedRange() walks its range in
    // ascending line order so this holds.
    while (tokenCursor < m_tokens.size() && m_tokens[tokenCursor].range.end <= lineStart) {
        ++tokenCursor;
    }
    for (std::size_t i = tokenCursor; i < m_tokens.size() && m_tokens[i].range.start < lineEnd; ++i) {
        const MinimapLineColorState state = classifyTokenKindForMinimap(m_tokens[i].kind);
        if (state != MinimapLineColorState::PlainText) {
            return state;
        }
    }
    return MinimapLineColorState::PlainText;
}

void RenderPipeline::populateMinimapColorsForRequestedRange() noexcept {
    if (m_document == nullptr || !m_hasRequestedTokenRange || !m_cachedSnapshot) {
        return;
    }
    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0 || m_minimapLineColors.size() != totalLines) {
        return;  // no document, or a stale response predating the last version-triggered clear/resize
    }
    const LineNumber lineStart = m_document->offsetToLine(m_requestedTokenRange.start);
    const auto        lineEndExclusive = static_cast<LineNumber>(
        std::min(totalLines, static_cast<std::uint64_t>(m_document->offsetToLine(m_requestedTokenRange.end)) + 1));
    std::size_t tokenCursor = 0;
    for (LineNumber line = lineStart; line < lineEndExclusive; ++line) {
        const TextRange span      = minimapLineSpan(line, totalLines);
        m_minimapLineColors[line] = classifyLineForMinimap(span.start, span.end, tokenCursor);
    }
}

void RenderPipeline::applyAsyncSyntaxTokens(std::vector<syntax::Token> tokens) noexcept {
    m_tokens = std::move(tokens);
    m_lastRenderedFrameState.reset();
    populateMinimapColorsForRequestedRange();
}

ID2D1SolidColorBrush* RenderPipeline::minimapLineBrush(LineNumber line, TextPos lineStart,
                                                        TextPos lineEnd) noexcept {
    if (lineEnd <= lineStart) {
        return nullptr;  // empty line - nothing to draw, regardless of classification state
    }
    const MinimapLineColorState state =
        line < m_minimapLineColors.size() ? m_minimapLineColors[line] : MinimapLineColorState::Unpopulated;
    return minimapBrushForState(state);
}

void RenderPipeline::drawMinimapLines(ID2D1DeviceContext6& dc, float left, float heightDips, float charWidthDips,
                                      std::uint64_t totalLines) noexcept {
    if (!m_minimapTextBrush || !m_minimapUnpopulatedBrush) {
        return;
    }
    const float          minRowHeightDips = m_lineHeightDips / kMinimapScaleDivisor;
    const std::uint64_t  bucketCount      = computeMinimapBucketCount(heightDips, minRowHeightDips, totalLines);
    if (bucketCount == 0) {
        return;
    }
    const float     rowHeightDips = heightDips / static_cast<float>(bucketCount);
    constexpr float kPaddingDips  = 4.0F;
    const float     maxBarWidth   = std::max(0.0F, kMinimapWidthDips - (2.0F * kPaddingDips));
    for (std::uint64_t bucket = 0; bucket < bucketCount; ++bucket) {
        const auto      line = static_cast<LineNumber>(minimapBucketStartLine(bucket, bucketCount, totalLines));
        const TextRange span = minimapLineSpan(line, totalLines);
        ID2D1SolidColorBrush* brush = minimapLineBrush(line, span.start, span.end);
        if (brush == nullptr) {
            continue;
        }
        const float y        = static_cast<float>(bucket) * rowHeightDips;
        const float barWidth = std::min(static_cast<float>(span.end - span.start) * charWidthDips, maxBarWidth);
        dc.FillRectangle(D2D1::RectF(left + kPaddingDips, y, left + kPaddingDips + barWidth,
                                     y + std::max(1.0F, rowHeightDips - 1.0F)),
                         brush);
    }
}

void RenderPipeline::drawMinimapViewportHighlight(ID2D1DeviceContext6& dc, float left, float widthDips,
                                                   float heightDips, std::uint64_t totalLines) noexcept {
    const auto [visStart, visEnd] = visibleLineRange();
    if (visStart >= visEnd || totalLines == 0) {
        return;
    }
    const auto  total   = static_cast<float>(totalLines);
    const float rectTop = (static_cast<float>(visStart) / total) * heightDips;
    // A viewport of a handful of lines inside a 1,000,000-line document
    // would otherwise round to a sub-pixel (invisible) sliver - matches the
    // "a scrollbar thumb never shrinks to literally 0px" convention (Phase
    // 7w, untuned initial value per CLAUDE.md rule 10).
    constexpr float kMinHighlightHeightDips = 2.0F;
    const float      rectBottom             = (static_cast<float>(visEnd) / total) * heightDips;
    const float      clampedBottom          = std::max(rectBottom, rectTop + kMinHighlightHeightDips);
    dc.FillRectangle(D2D1::RectF(left, rectTop, widthDips, clampedBottom), m_minimapViewportBrush.Get());
}

void RenderPipeline::drawMinimap(ID2D1DeviceContext6& dc) noexcept {
    // WI-08: !m_showMinimap must be checked explicitly (not merely implied
    // by minimapWidthDips()==0.0F) - without this, minimapLeftDips() would
    // equal the client area's own right edge when hidden, which is NOT
    // <= gutterWidthDips() on any normal-sized window, so the "too narrow"
    // guard below wouldn't catch it and a full-width strip would be drawn.
    if (!m_showMinimap || !m_cachedSnapshot || m_document == nullptr || m_lineHeightDips <= 0.0F ||
        m_dpiScale <= 0.0F || !m_minimapBackgroundBrush || !m_minimapViewportBrush) {
        return;
    }
    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0) {
        return;
    }
    const float left = minimapLeftDips();
    // Window too narrow (strip would collide with the gutter) - drawing
    // neither is safer than drawing both overlapping.
    if (left <= gutterWidthDips()) {
        return;
    }
    const float widthDips  = static_cast<float>(m_width) / m_dpiScale;
    const float heightDips = static_cast<float>(m_height) / m_dpiScale;
    dc.FillRectangle(D2D1::RectF(left, 0.0F, widthDips, heightDips), m_minimapBackgroundBrush.Get());

    // Phase 7w: "whole document overview" - the strip always represents
    // [0, totalLines), independent of m_topLine/widenedVisibleLineRange().
    const float charWidthDips = m_charWidthDips / kMinimapScaleDivisor;
    drawMinimapLines(dc, left, heightDips, charWidthDips, totalLines);
    drawMinimapViewportHighlight(dc, left, widthDips, heightDips, totalLines);
}

std::optional<document::TextPos> RenderPipeline::hitTest(std::int32_t xPx, std::int32_t yPx) noexcept {
    if (!m_cachedSnapshot || m_document == nullptr || m_lineHeightDips <= 0.0F || !m_dwriteFactory ||
        m_dpiScale <= 0.0F) {
        return std::nullopt;
    }
    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0) {
        return std::nullopt;
    }

    // Clicks within the gutter strip itself clamp to column 0 of that line -
    // no separate "toggle bookmark on gutter click" interaction exists yet
    // (Phase 4b8c, deliberately deferred). WI-03: leftColumnOffsetDips()
    // added back AFTER the gutter clamp - the inverse of drawTextLine()'s
    // `kGutterWidthDips - leftColumnOffsetDips()` glyph-origin shift.
    const float xDip =
        std::max(0.0F, (static_cast<float>(xPx) / m_dpiScale) - gutterWidthDips()) + leftColumnOffsetDips();
    // Phase 7h/7o: clicks within the Breadcrumb/Sticky scroll strip(s) clamp
    // to the first visible line's row offset - same "clamp to a sane
    // default" convention as the gutter's xDip clamp above.
    const float yDip = std::max(0.0F, (static_cast<float>(yPx) / m_dpiScale) - reservedTopHeightDips());

    const LineNumber startLine =
        m_topLine < totalLines ? m_topLine : static_cast<LineNumber>(totalLines - 1);
    const LineNumber rowOffset =
        yDip >= 0.0F ? static_cast<LineNumber>(yDip / m_lineHeightDips) : LineNumber{0};
    // Phase 7i/7j: resolves `rowOffset` (a drawn-row count) to the same
    // logical line drawVisibleLines() actually drew there, skipping
    // folded-hidden lines - see visibleLineAtRow()'s comment.
    const LineNumber targetLine = visibleLineAtRow(startLine, rowOffset);

    const TextPos lineStart = m_document->lineToOffset(targetLine);
    const TextPos lineEndExclusive = (targetLine + 1 >= totalLines)
                                          ? m_cachedSnapshot->length()
                                          : m_document->lineToOffset(targetLine + 1);
    const std::u16string lineText =
        m_cachedSnapshot->extract(TextRange{.start = lineStart, .end = lineEndExclusive});
    std::u16string_view lineSpan(lineText);
    const auto newlinePos = lineSpan.find(u'\n');
    if (newlinePos != std::u16string_view::npos) {
        lineSpan = lineSpan.substr(0, newlinePos);
    }

    const auto layoutResult =
        m_layoutCache.getOrCreate(targetLine, lineSpan, *m_dwriteFactory.Get(), *m_textFormat.Get(),
                                  kMaxLayoutWidthDips, kMaxLayoutHeightDips);
    if (!layoutResult.has_value()) {
        return std::nullopt;
    }

    BOOL                     isTrailingHit = FALSE;
    BOOL                     isInside      = FALSE;
    DWRITE_HIT_TEST_METRICS  metrics{};
    const HRESULT hr = (*layoutResult)->HitTestPoint(xDip, 0.0F, &isTrailingHit, &isInside, &metrics);
    if (FAILED(hr)) {
        return std::nullopt;
    }
    const std::uint32_t column =
        isTrailingHit ? (metrics.textPosition + metrics.length) : metrics.textPosition;
    return lineStart + column;
}

document::LineNumber RenderPipeline::visibleLineAtRow(LineNumber startLine,
                                                       LineNumber visibleRowOffset) const noexcept {
    const std::uint64_t totalLines = m_document->lineCount();
    LineNumber          targetLine   = startLine;
    LineNumber          visibleSteps = 0;
    for (;;) {
        if (!isLineHidden(targetLine)) {
            if (visibleSteps == visibleRowOffset) {
                break;
            }
            ++visibleSteps;
        }
        if (targetLine + 1 >= totalLines) {
            break;
        }
        ++targetLine;
    }
    return targetLine;
}

std::optional<document::LineNumber> RenderPipeline::hitTestFoldMarker(std::int32_t xPx,
                                                                       std::int32_t yPx) noexcept {
    if (!m_cachedSnapshot || m_document == nullptr || m_lineHeightDips <= 0.0F || m_dpiScale <= 0.0F) {
        return std::nullopt;
    }
    const float xDip = static_cast<float>(xPx) / m_dpiScale;
    if (xDip < 0.0F || xDip >= gutterWidthDips()) {
        return std::nullopt;  // click landed outside the gutter strip entirely
    }
    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0) {
        return std::nullopt;
    }
    const float yDip = std::max(0.0F, (static_cast<float>(yPx) / m_dpiScale) - reservedTopHeightDips());
    const LineNumber startLine =
        m_topLine < totalLines ? m_topLine : static_cast<LineNumber>(totalLines - 1);
    const auto        rowOffset  = static_cast<LineNumber>(yDip / m_lineHeightDips);
    const LineNumber   targetLine = visibleLineAtRow(startLine, rowOffset);
    if (std::ranges::find(m_foldRegions, targetLine, &FoldVisual::headerLine) == m_foldRegions.end()) {
        return std::nullopt;
    }
    return targetLine;
}

std::optional<document::LineNumber> RenderPipeline::minimapLineAtY(std::int32_t yPx) const noexcept {
    if (!m_cachedSnapshot || m_document == nullptr || m_lineHeightDips <= 0.0F || m_dpiScale <= 0.0F) {
        return std::nullopt;
    }
    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0) {
        return std::nullopt;
    }
    const float heightDips = static_cast<float>(m_height) / m_dpiScale;
    if (heightDips <= 0.0F) {
        return std::nullopt;
    }
    // Phase 7w: "whole document overview" - direct proportion against the
    // WHOLE document, not any windowed/margined range (see this method's
    // header comment).
    const float yDip = std::clamp(static_cast<float>(yPx) / m_dpiScale, 0.0F, heightDips);
    const auto  line = static_cast<LineNumber>((yDip / heightDips) * static_cast<float>(totalLines));
    return std::min(line, static_cast<LineNumber>(totalLines - 1));
}

std::optional<document::LineNumber> RenderPipeline::hitTestMinimap(std::int32_t xPx,
                                                                    std::int32_t yPx) const noexcept {
    const float left = minimapLeftDips();
    if (left <= gutterWidthDips()) {
        return std::nullopt;  // matches drawMinimap()'s own "too narrow" bail-out
    }
    const float xDip = static_cast<float>(xPx) / m_dpiScale;
    if (xDip < left) {
        return std::nullopt;  // click landed in the text area, not the minimap
    }
    return minimapLineAtY(yPx);
}

RenderExpected<void> RenderPipeline::renderOnce() noexcept {
    // render() already checks isAttached() before calling this, but
    // renderOnce() is a private helper reachable from two call sites in
    // render() - re-checking here keeps it self-contained rather than
    // relying on caller discipline for a std::optional dereference.
    if (!m_device) {
        return std::unexpected(RenderError{.stage = RenderStage::NotAttached, .hr = E_NOT_VALID_STATE});
    }
    // Bound once, right after the check above: refreshDocumentCacheIfStale()/
    // ensureTextFormat() below are opaque member calls from a static
    // analyzer's point of view, so re-dereferencing m_device-> after them
    // would look like an unchecked std::optional access even though neither
    // call touches m_device.
    RenderDevice& device = *m_device;

    auto docResult = refreshDocumentCacheIfStale();
    if (!docResult) {
        return docResult;
    }
    // Phase 7t: unconditional every frame (unlike refreshDocumentCacheIfStale()
    // above, which can return before reaching its own body on a pure scroll -
    // see that function's comment) - this is what lets scrolling into a
    // not-yet-tokenized area trigger a new request with no document edit at
    // all.
    ensureSyntaxTokensCoverVisibleRange();
    auto formatResult = ensureTextFormat();
    if (!formatResult) {
        return formatResult;
    }

    auto beginResult = device.beginFrame();
    if (!beginResult) {
        return std::unexpected(beginResult.error());
    }
    ID2D1DeviceContext6* dc = *beginResult;

    auto brushResult = ensureTextBrush(*dc);
    if (!brushResult) {
        // Best-effort close; frame content is moot on this error path, but
        // the original brush failure is still what gets reported.
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return brushResult;
    }
    auto selectionBrushResult = ensureSelectionBrush(*dc);
    if (!selectionBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return selectionBrushResult;
    }
    auto matchBrushResult = ensureMatchBrushes(*dc);
    if (!matchBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return matchBrushResult;
    }
    auto bookmarkBrushResult = ensureBookmarkBrush(*dc);
    if (!bookmarkBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return bookmarkBrushResult;
    }
    auto gitDiffBrushResult = ensureGitDiffBrushes(*dc);
    if (!gitDiffBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return gitDiffBrushResult;
    }
    auto tokenBrushResult = ensureTokenBrushes(*dc);
    if (!tokenBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return tokenBrushResult;
    }
    auto logLevelBrushResult = ensureLogLevelBrushes(*dc);
    if (!logLevelBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return logLevelBrushResult;
    }
    auto indentGuideBrushResult = ensureIndentGuideBrushes(*dc);
    if (!indentGuideBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return indentGuideBrushResult;
    }
    auto breadcrumbBrushResult = ensureBreadcrumbBrush(*dc);
    if (!breadcrumbBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return breadcrumbBrushResult;
    }
    auto foldMarkerBrushResult = ensureFoldMarkerBrush(*dc);
    if (!foldMarkerBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return foldMarkerBrushResult;
    }
    auto lineNumberBrushResult = ensureLineNumberBrush(*dc);
    if (!lineNumberBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return lineNumberBrushResult;
    }
    auto minimapBrushResult = ensureMinimapBrushes(*dc);
    if (!minimapBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return minimapBrushResult;
    }
    auto imeCompositionBrushResult = ensureImeCompositionBrushes(*dc);
    if (!imeCompositionBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return imeCompositionBrushResult;
    }

    // WI-09: see theme.h/theme.cpp for this color's value/rationale (was a
    // local hardcoded constexpr matching the previous GDI placeholder fill
    // before WI-09's Theme system).
    dc->Clear(themeForKind(m_themeKind).background);
    drawVisibleLines(*dc);
    drawBreadcrumb(*dc);
    drawStickyScroll(*dc);
    drawMinimap(*dc);

    return device.endFrame();
}

}  // namespace neomifes::render
