#pragma once

// Internal to src/logmode/ only (no include/ path - never part of the
// public logmode:: API surface, same "private helper header shared across
// .cpp files in one module" pattern as src/syntax/src/syntax_internal.h).
// A deliberate duplicate of src/core/src/json_string_convert.h's
// core::detail::toUtf8/fromUtf8 pair, NOT a shared cross-module helper:
// neomifes::logmode depending on neomifes::core would be a layering
// violation (core:: sits above the engine modules, CLAUDE.md sec.3's
// layered architecture - lower layers don't know upper layers). core::
// itself reached this exact shape only after its 3rd independent
// occurrence (settings.cpp/search_history.cpp duplicated it twice before
// key_bindings.cpp's arrival triggered the extraction) - this is a first
// occurrence within neomifes::logmode, so a small local copy is the right
// size for now.

#include <optional>
#include <string>
#include <string_view>

namespace neomifes::logmode::detail {

// Encodes `text` as UTF-8. Total for any well-formed UTF-16 string (which
// every std::u16string this codebase constructs already is) - never fails.
[[nodiscard]] std::string toUtf8(std::u16string_view text);

// Decodes `text` from UTF-8. nullopt if `text` is not well-formed UTF-8.
[[nodiscard]] std::optional<std::u16string> fromUtf8(std::string_view text);

}  // namespace neomifes::logmode::detail
