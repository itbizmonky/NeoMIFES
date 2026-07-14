#include "neomifes/document/original_buffer.h"

namespace neomifes::document {

std::u16string_view OriginalBuffer::view(std::uint64_t offset,
                                         std::uint64_t length) const noexcept {
    if (offset > m_decoded.size() || length > m_decoded.size() - offset) {
        return {};
    }
    return {m_decoded.data() + offset, static_cast<std::size_t>(length)};
}

std::shared_ptr<const OriginalBuffer>
OriginalBuffer::fromU16String(std::u16string s) {
    return std::make_shared<const OriginalBuffer>(std::move(s));
}

}  // namespace neomifes::document
