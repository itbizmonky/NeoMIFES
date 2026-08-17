#pragma once

// nextVisibleLogLine / previousVisibleLogLine - WI-14c navigation over a
// LogModel's line array. Finds the nearest matched, filter-passing line
// relative to `from`, wrapping around like
// core::BookmarkManager::next()/previous() (Phase 4b8c) does - this file is
// deliberately headless (no document::Document, no core:: dependency) so
// it stays unit-testable against a synthetic std::vector<LogLine>, the same
// "minimal-dependency pure function" shape format_detection.h's
// detectLogPatternRule() already established for this module.
//
// With no filter applied (levelFilterMask == kAllLogLevelsVisible), calling
// this repeatedly walks every matched line in document order - i.e. "jump
// through the time series" (要件定義書§8's 時系列ジャンプ). Filtered down to
// Error+Fatal only, the same function becomes "jump between error lines"
// (ERROR抽出); filtered to Warning only, "jump between warning lines"
// (WARNING抽出) - one mechanism intentionally satisfies all three
// requirements rather than three separate special-cased jump commands (see
// WI-14c's plan, 設計方針6).

#include <cstdint>
#include <optional>
#include <span>

#include "neomifes/document/text_pos.h"  // document::LineNumber
#include "neomifes/logmode/log_model.h"  // LogLine

namespace neomifes::logmode {

// The nearest line strictly after `from` where lines[line].matched is true
// and lines[line].level passes `levelFilterMask` (see logLevelFilterBit()),
// wrapping around to the first such line if none is found past `from`.
// nullopt if no line in `lines` satisfies both conditions at all, or if
// `lines` is empty. `lines` is assumed dense and index-aligned with
// document line numbers (LogModel::build()'s own documented invariant:
// "every document line produces exactly one LogLine").
[[nodiscard]] std::optional<document::LineNumber> nextVisibleLogLine(
    std::span<const LogLine> lines, document::LineNumber from, std::uint8_t levelFilterMask) noexcept;

// Mirror of nextVisibleLogLine() searching strictly before `from`, wrapping
// around to the last qualifying line if none is found before `from`.
[[nodiscard]] std::optional<document::LineNumber> previousVisibleLogLine(
    std::span<const LogLine> lines, document::LineNumber from, std::uint8_t levelFilterMask) noexcept;

}  // namespace neomifes::logmode
