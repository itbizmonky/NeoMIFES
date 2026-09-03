#include <gtest/gtest.h>

#include "neomifes/render/d2d_factories.h"
#include "neomifes/render/visual_row_layout.h"
#include "neomifes/util/wchar_cast.h"

namespace {

using neomifes::render::computeVisualRows;
using neomifes::render::sharedDWriteFactory;
using neomifes::render::VisualRowSpan;
using neomifes::util::toWstringView;

// Same "real DirectWrite objects, no HWND/D3D device needed" testability
// tier render_text_layout_cache_test.cpp already establishes for this
// codebase - IDWriteTextLayout creation/inspection needs only the
// process-wide DirectWrite factory.
class VisualRowLayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto factory = sharedDWriteFactory();
        ASSERT_TRUE(factory.has_value());
        m_factory = *factory;

        const HRESULT hr = m_factory->CreateTextFormat(
            L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.0F, L"en-us", m_format.GetAddressOf());
        ASSERT_TRUE(SUCCEEDED(hr));
    }

    // maxWidthDips deliberately narrow enough to force wrapping for the
    // longer test strings below - callers needing a non-wrapping layout
    // pass a wide value (kWideDips) instead.
    Microsoft::WRL::ComPtr<IDWriteTextLayout> makeLayout(std::u16string_view text, float maxWidthDips) {
        const std::wstring_view                   wText = toWstringView(text);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        const HRESULT hr = m_factory->CreateTextLayout(wText.data(), static_cast<UINT32>(wText.size()),
                                                        m_format.Get(), maxWidthDips, 65536.0F,
                                                        layout.GetAddressOf());
        EXPECT_TRUE(SUCCEEDED(hr));
        if (layout) {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        }
        return layout;
    }

    Microsoft::WRL::ComPtr<IDWriteFactory7>   m_factory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_format;
    static constexpr float                   kWideDips = 65536.0F;
};

// Verifies the row spans returned for `layout` are contiguous (each row
// starts exactly where the previous one ended) and collectively cover
// [0, expectedTotalLength) - the invariant every caller of
// computeVisualRows() relies on regardless of row count.
void expectContiguousRowsCoveringLength(const std::vector<VisualRowSpan>& rows,
                                        std::uint32_t                    expectedTotalLength) {
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(rows.front().startColumn, 0U);
    for (std::size_t i = 1; i < rows.size(); ++i) {
        EXPECT_EQ(rows[i].startColumn, rows[i - 1].endColumn) << "gap/overlap at row " << i;
    }
    EXPECT_EQ(rows.back().endColumn, expectedTotalLength);
}

TEST_F(VisualRowLayoutTest, ShortLineAtWideWidthIsOneRow) {
    constexpr std::u16string_view kText = u"hello world";
    const auto layout = makeLayout(kText, kWideDips);
    const auto rows   = computeVisualRows(*layout.Get());
    ASSERT_EQ(rows.size(), 1U);
    expectContiguousRowsCoveringLength(rows, static_cast<std::uint32_t>(kText.size()));
}

TEST_F(VisualRowLayoutTest, EmptyLineIsOneRowWithEmptyRange) {
    const auto layout = makeLayout(u"", kWideDips);
    const auto rows   = computeVisualRows(*layout.Get());
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows.front().startColumn, 0U);
    EXPECT_EQ(rows.front().endColumn, 0U);
}

TEST_F(VisualRowLayoutTest, LongLineAtNarrowWidthWrapsIntoContiguousRows) {
    // Many short, space-separated words - plenty of legal break points, so
    // a narrow width forces several rows without relying on mid-word
    // breaking (that's the separate test below).
    constexpr std::u16string_view kText =
        u"the quick brown fox jumps over the lazy dog again and again and again";
    // Roughly enough for a handful of words per row at 14pt Consolas -
    // exact row count isn't asserted (font-metrics-dependent, would make
    // this test brittle); only the structural invariant is.
    const auto layout = makeLayout(kText, 80.0F);
    const auto rows   = computeVisualRows(*layout.Get());
    EXPECT_GT(rows.size(), 1U) << "narrow width should force multiple rows for this text";
    expectContiguousRowsCoveringLength(rows, static_cast<std::uint32_t>(kText.size()));
}

TEST_F(VisualRowLayoutTest, SingleUnbreakableWordWiderThanLayoutStillWrapsMidWord) {
    // No spaces at all - DWRITE_WORD_WRAPPING_WRAP's documented default
    // behavior still breaks mid-word rather than overflowing the layout
    // box, once no legal word-boundary break point exists.
    constexpr std::u16string_view kText = u"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const auto layout = makeLayout(kText, 40.0F);
    const auto rows   = computeVisualRows(*layout.Get());
    EXPECT_GT(rows.size(), 1U) << "an unbreakable word wider than the layout should still wrap mid-word";
    expectContiguousRowsCoveringLength(rows, static_cast<std::uint32_t>(kText.size()));
}

TEST_F(VisualRowLayoutTest, NonWrappingLayoutIsAlwaysOneRowRegardlessOfLength) {
    // The wrap-OFF path RenderPipeline actually uses (kMaxLayoutWidthDips,
    // see render_pipeline.cpp) - confirms computeVisualRows() degrades to
    // the same single-span shape wrap-ON short lines already produce,
    // meaning RenderPipeline's row-counting logic needs no separate
    // wrap-off branch when calling into this module.
    constexpr std::u16string_view kText =
        u"this is a fairly long line of text that would wrap at any reasonably narrow width but must not here";
    const auto layout = makeLayout(kText, kWideDips);
    const auto rows   = computeVisualRows(*layout.Get());
    ASSERT_EQ(rows.size(), 1U);
    expectContiguousRowsCoveringLength(rows, static_cast<std::uint32_t>(kText.size()));
}

}  // namespace
