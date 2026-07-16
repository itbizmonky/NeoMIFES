#include "neomifes/render/text_layout_cache.h"

#include "neomifes/util/wchar_cast.h"

namespace neomifes::render {

RenderExpected<IDWriteTextLayout*> TextLayoutCache::getOrCreate(
    document::LineNumber line, std::u16string_view lineText, IDWriteFactory7& dwriteFactory,
    IDWriteTextFormat& textFormat, float maxWidthDips, float maxHeightDips) noexcept {
    const auto it = m_entries.find(line);
    if (it != m_entries.end()) {
        ++m_stats.hits;
        return it->second.Get();
    }

    const std::wstring_view wText = util::toWstringView(lineText);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    const HRESULT hr = dwriteFactory.CreateTextLayout(
        wText.data(), static_cast<UINT32>(wText.size()), &textFormat, maxWidthDips, maxHeightDips,
        layout.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DWriteFactory, .hr = hr});
    }

    ++m_stats.misses;
    IDWriteTextLayout* const result = layout.Get();
    m_entries.emplace(line, std::move(layout));
    return result;
}

void TextLayoutCache::clear() noexcept { m_entries.clear(); }

}  // namespace neomifes::render
