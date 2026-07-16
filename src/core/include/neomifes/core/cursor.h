#pragma once

// Cursor - a single caret + its selection anchor.
// Position representation follows document::TextPos (flat UTF-16 code-unit
// offset) rather than a line/column pair - the Document Engine has no
// line/column type (see text_pos.h), and detailed_design.md sec.5.1 sketches
// Cursor this way already.

#include "neomifes/document/text_pos.h"

namespace neomifes::core {

struct Cursor {
    document::TextPos position  = 0;
    document::TextPos anchor    = 0;  // == position means no selection
    bool               isPrimary = false;

    [[nodiscard]] constexpr bool hasSelection() const noexcept { return position != anchor; }

    friend constexpr bool operator==(const Cursor&, const Cursor&) = default;
};

}  // namespace neomifes::core
