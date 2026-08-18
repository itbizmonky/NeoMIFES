#pragma once

// Internal to src/jsontree/ only (no include/ path - never part of the
// public jsontree:: API surface, same "private helper header shared across
// .cpp files in one module" pattern as src/syntax/src/syntax_internal.h).
// A deliberate partial duplicate of src/core/src/json_string_convert.h's
// core::detail::fromUtf8 (also duplicated in full, both directions, by
// src/logmode/src/json_string_convert.h) - NOT a shared cross-module
// helper: neomifes::jsontree depending on neomifes::core would be a
// layering violation (core:: sits above the engine modules, CLAUDE.md
// sec.3's layered architecture). Only fromUtf8() is duplicated here, not
// its toUtf8() counterpart: this module's UTF-16->UTF-8 direction is
// handled once, for the whole document, by neomifes::util::
// toUtf8WithOffsets() (needed for nlohmann::ordered_json::parse() plus the
// byte<->UTF-16-offset table json_tree.cpp's position scanner needs) -
// fromUtf8() is used only for the much smaller job of decoding individual
// object keys nlohmann::ordered_json already handed back as UTF-8.

#include <optional>
#include <string>
#include <string_view>

namespace neomifes::jsontree::detail {

// Decodes `text` from UTF-8. nullopt if `text` is not well-formed UTF-8 -
// in practice this should never happen for a key nlohmann::ordered_json
// itself decoded from an already-validated JSON document, but the caller
// (json_tree.cpp) still folds this into parseJsonTree()'s overall
// std::nullopt contract rather than assuming it can't fail.
[[nodiscard]] std::optional<std::u16string> fromUtf8(std::string_view text);

}  // namespace neomifes::jsontree::detail
