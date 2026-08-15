#pragma once

// Internal to src/core/ only (no include/ path - never part of the public
// core:: API surface, same "private helper header shared across .cpp files
// in one module" pattern as src/syntax/src/syntax_internal.h). Shared by
// settings.cpp/search_history.cpp/key_bindings.cpp (WI-10) to avoid a 3rd
// independent duplicate of these two functions - each file's prior
// anonymous-namespace copy noted "if a third JSON-backed persisted type
// needs the same helpers, that's the signal to extract them" (WI-10's
// core::KeyBindings is that third occurrence).
//
// Deliberately NOT placed in neomifes::util: neomifes::util cannot depend on
// neomifes::encoding without creating a cycle (neomifes::encoding depends on
// neomifes::platform, which itself PRIVATE-links neomifes::util). core::
// already PRIVATE-links neomifes::encoding directly, so keeping this
// internal to core:: avoids the cycle entirely without inventing a new
// cross-module home.

#include <optional>
#include <string>
#include <string_view>

namespace neomifes::core::detail {

// Encodes `text` as UTF-8. Total for any well-formed UTF-16 string (which
// every std::u16string this codebase constructs already is) - never fails.
[[nodiscard]] std::string toUtf8(std::u16string_view text);

// Decodes `text` from UTF-8. nullopt if `text` is not well-formed UTF-8.
[[nodiscard]] std::optional<std::u16string> fromUtf8(std::string_view text);

}  // namespace neomifes::core::detail
