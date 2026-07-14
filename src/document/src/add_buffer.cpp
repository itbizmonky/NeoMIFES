#include "neomifes/document/add_buffer.h"

namespace neomifes::document {

std::uint64_t AddBuffer::append(std::u16string_view text) {
    const std::uint64_t offset = m_data.size();
    m_data.insert(m_data.end(), text.begin(), text.end());
    return offset;
}

std::u16string_view AddBuffer::view(std::uint64_t offset,
                                    std::uint64_t length) const noexcept {
    // Guarded against caller mistakes: return empty view on out-of-range.
    if (offset > m_data.size() || length > m_data.size() - offset) {
        return {};
    }
    return {m_data.data() + offset, static_cast<std::size_t>(length)};
}

}  // namespace neomifes::document
