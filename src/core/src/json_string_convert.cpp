#include "json_string_convert.h"

#include <span>
#include <variant>
#include <vector>

#include "neomifes/encoding/encoding.h"

namespace neomifes::core::detail {

std::string toUtf8(std::u16string_view text) {
    const auto  result = encoding::encode(text, encoding::Encoding::Utf8);
    const auto& bytes   = std::get<std::vector<std::byte>>(result);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::optional<std::u16string> fromUtf8(std::string_view text) {
    const auto bytes  = std::as_bytes(std::span(text.data(), text.size()));
    auto       result = encoding::decode(bytes, encoding::Encoding::Utf8);
    if (std::holds_alternative<encoding::DecodeError>(result)) {
        return std::nullopt;
    }
    return std::move(std::get<std::u16string>(result));
}

}  // namespace neomifes::core::detail
