#include <gtest/gtest.h>

#include "neomifes/render/d2d_factories.h"
#include "neomifes/render/text_layout_cache.h"

namespace {

using neomifes::render::sharedDWriteFactory;
using neomifes::render::TextLayoutCache;

// IDWriteTextLayout/IDWriteTextFormat creation needs only the process-wide
// DirectWrite factory - no HWND/D3D/D2D device required, so these tests
// build real DirectWrite objects rather than mocking them.
class TextLayoutCacheTest : public ::testing::Test {
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

    Microsoft::WRL::ComPtr<IDWriteFactory7>   m_factory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_format;
    static constexpr float                   kMaxWidth  = 65536.0F;
    static constexpr float                   kMaxHeight = 65536.0F;
};

TEST_F(TextLayoutCacheTest, FirstAccessIsMiss) {
    TextLayoutCache cache;
    const auto result =
        cache.getOrCreate(0, u"hello", *m_factory.Get(), *m_format.Get(), kMaxWidth, kMaxHeight);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(*result, nullptr);
    EXPECT_EQ(cache.size(), 1U);
    EXPECT_EQ(cache.stats().hits, 0U);
    EXPECT_EQ(cache.stats().misses, 1U);
}

TEST_F(TextLayoutCacheTest, SecondAccessSameLineIsHitAndReturnsSamePointer) {
    TextLayoutCache cache;
    const auto first = cache.getOrCreate(0, u"hello", *m_factory.Get(), *m_format.Get(), kMaxWidth, kMaxHeight);
    ASSERT_TRUE(first.has_value());

    const auto second =
        cache.getOrCreate(0, u"hello", *m_factory.Get(), *m_format.Get(), kMaxWidth, kMaxHeight);
    ASSERT_TRUE(second.has_value());

    EXPECT_EQ(*first, *second);
    EXPECT_EQ(cache.size(), 1U);
    EXPECT_EQ(cache.stats().hits, 1U);
    EXPECT_EQ(cache.stats().misses, 1U);
}

TEST_F(TextLayoutCacheTest, DifferentLineIsSeparateMiss) {
    TextLayoutCache cache;
    const auto first  = cache.getOrCreate(0, u"line zero", *m_factory.Get(), *m_format.Get(), kMaxWidth, kMaxHeight);
    const auto second = cache.getOrCreate(1, u"line one", *m_factory.Get(), *m_format.Get(), kMaxWidth, kMaxHeight);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    EXPECT_NE(*first, *second);
    EXPECT_EQ(cache.size(), 2U);
    EXPECT_EQ(cache.stats().misses, 2U);
}

TEST_F(TextLayoutCacheTest, ClearDropsAllEntries) {
    TextLayoutCache cache;
    (void)cache.getOrCreate(0, u"a", *m_factory.Get(), *m_format.Get(), kMaxWidth, kMaxHeight);
    (void)cache.getOrCreate(1, u"b", *m_factory.Get(), *m_format.Get(), kMaxWidth, kMaxHeight);
    ASSERT_EQ(cache.size(), 2U);

    cache.clear();
    EXPECT_EQ(cache.size(), 0U);

    const auto afterClear =
        cache.getOrCreate(0, u"a", *m_factory.Get(), *m_format.Get(), kMaxWidth, kMaxHeight);
    ASSERT_TRUE(afterClear.has_value());
    EXPECT_EQ(cache.stats().misses, 3U);  // 2 before clear + 1 re-miss after
}

TEST_F(TextLayoutCacheTest, ResetStatsIsIndependentOfClear) {
    TextLayoutCache cache;
    (void)cache.getOrCreate(0, u"a", *m_factory.Get(), *m_format.Get(), kMaxWidth, kMaxHeight);
    (void)cache.getOrCreate(0, u"a", *m_factory.Get(), *m_format.Get(), kMaxWidth, kMaxHeight);
    ASSERT_EQ(cache.stats().hits, 1U);
    ASSERT_EQ(cache.stats().misses, 1U);

    cache.resetStats();
    EXPECT_EQ(cache.stats().hits, 0U);
    EXPECT_EQ(cache.stats().misses, 0U);
    EXPECT_EQ(cache.size(), 1U);  // resetStats() must not evict entries
}

}  // namespace
