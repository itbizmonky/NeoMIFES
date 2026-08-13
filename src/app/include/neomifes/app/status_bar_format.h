#pragma once

// status_bar_format (WI-07 step4) - pure functions turning already-extracted
// EditorSession/Workspace state into the exact wstring each
// ui::StatusBarParts field should hold. Deliberately separate from
// status_bar.h (ui:: layer, Win32-mechanics-only: STATUSCLASSNAME
// create/SB_SETTEXTW) and from any click-handling (WI-07 step6 - changing
// the encoding/line-ending via this bar's own UI - see that step's own
// design notes) - this file only formats, never reads EditorSession/
// Document/RenderPipeline directly, which is what keeps it linkable into
// neomifes_app_input and unit-testable without a live HWND (same layering
// grep_result_formatting.h already follows for GrepBar's result list).

#include <cstdint>
#include <optional>
#include <string>

#include "neomifes/document/text_pos.h"
#include "neomifes/encoding/encoding.h"
#include "neomifes/syntax/syntax.h"

namespace neomifes::app {

// "行:桁" - both 1-based (editors conventionally display 1-based line/
// column even though this codebase's internal document::LineNumber and the
// column callers pass in are 0-based - the +1 conversion happens here, not
// at any call site, so callers never have to remember to do it themselves).
[[nodiscard]] std::wstring formatStatusBarPosition(document::LineNumber line, std::uint32_t column);

// Empty string if selectedLength == 0 (no selection) - StatusBar::setParts()
// then shows a blank part rather than "0 selected", matching how most
// editors omit this field entirely when nothing is selected.
[[nodiscard]] std::wstring formatStatusBarSelectionCount(std::uint64_t selectedLength);

[[nodiscard]] std::wstring formatStatusBarEncoding(encoding::Encoding encoding);

// encoding::LineEnding::Mixed is not a meaningful SAVE target (see
// encoding.h's convertLineEndings() comment, which treats it as Lf
// internally) but IS meaningful to DISPLAY if a loaded file actually had
// mixed terminators - shown here as "Mixed" rather than silently rounded to
// one convention, so the user isn't misled about what Ctrl+S will do until
// they explicitly pick one (WI-07 step6).
[[nodiscard]] std::wstring formatStatusBarLineEnding(encoding::LineEnding lineEnding);

[[nodiscard]] std::wstring formatStatusBarOverwriteMode(bool overwriteMode);

// Empty string if `language` is nullopt (undetected/unrecognized extension) -
// same "blank rather than a placeholder word" convention as
// formatStatusBarSelectionCount() above.
[[nodiscard]] std::wstring formatStatusBarLanguage(std::optional<syntax::Language> language);

}  // namespace neomifes::app
