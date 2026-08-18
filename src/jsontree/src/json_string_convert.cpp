#include "json_string_convert.h"

#include <span>
#include <variant>

#include "neomifes/encoding/encoding.h"

namespace neomifes::jsontree::detail {

std::optional<std::u16string> fromUtf8(std::string_view text) {
    const auto bytes  = std::as_bytes(std::span(text.data(), text.size()));
    auto       result = encoding::decode(bytes, encoding::Encoding::Utf8);
    if (std::holds_alternative<encoding::DecodeError>(result)) {
        return std::nullopt;
    }
    return std::move(std::get<std::u16string>(result));
}

}  // namespace neomifes::jsontree::detail
