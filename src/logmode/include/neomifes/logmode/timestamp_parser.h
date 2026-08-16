#pragma once

// parseTimestamp - std::chrono::parse-based log-line timestamp parsing
// (WI-14a). No new external dependency: std::chrono::parse/from_stream is
// present in this project's pinned MSVC toolchain (verified directly
// against the installed STL headers before writing this file, CLAUDE.md
// rule 3) - no ADR needed.
//
// Three behaviors below were NOT obvious from the chrono spec alone and
// were confirmed with a standalone probe program before being encoded
// here (build_plan.md's WI-14a "実装後の確定事項" records the probe
// output verbatim):
//   1. std::chrono::sys_time<Duration> as the parse target REQUIRES the
//      format to resolve a full calendar date (year, month, AND day) -
//      "%b %d" (month+day, no year) fails to parse even though every
//      individual field it names is present in the input. This is why
//      `assumedYear` exists: a format with no "%Y" component (RFC 3164
//      syslog has none - a property of that RFC, not a bug here) cannot
//      produce a sys_time at all without one being supplied from outside.
//   2. "%Ez" (RFC 3339/ISO 8601 extended UTC offset) does NOT accept a
//      literal "Z" (Zulu/UTC) suffix, even though RFC 5424 timestamps
//      commonly use "Z" as shorthand for "+00:00" - parseTimestamp()
//      normalizes a trailing 'Z' to "+00:00" before parsing whenever
//      `format` contains "%Ez".
//   3. A format like "%Y-%m-%d %H:%M:%S" against "...32,123" (comma
//      decimal separator instead of '.') does NOT fail - it silently
//      parses only "...32" and leaves ",123" unconsumed in the stream,
//      producing a technically-successful parse with the wrong (truncated
//      to whole seconds) result. parseTimestamp() rejects this by
//      requiring the entire input to be consumed (after skipping
//      trailing whitespace) - a partial match is treated as failure.

#include <chrono>
#include <optional>
#include <string_view>

namespace neomifes::logmode {

using Timestamp = std::chrono::sys_time<std::chrono::milliseconds>;

// Parses `text` using `format` (a std::chrono::parse-compatible format
// string, e.g. "%FT%T%Ez"). Returns nullopt on any parse failure
// (truncated/malformed input, or an unconsumed remainder per finding 3
// above) rather than throwing - matches this project's recoverable-error
// convention (CLAUDE.md sec.4: std::optional for recoverable errors).
//
// `assumedYear` is consulted ONLY when `format` contains no "%Y" (see
// finding 1 above) - in that case, `text`/`format` are NOT parsed
// directly; instead an assumedYear-prefixed copy of both is built and
// parsed instead (e.g. text "Oct 11 22:14:15" + format "%b %d %H:%M:%S"
// becomes "2026 Oct 11 22:14:15" + "%Y %b %d %H:%M:%S" when
// assumedYear=2026). If `format` already contains "%Y", `assumedYear` is
// ignored even if supplied.
[[nodiscard]] std::optional<Timestamp> parseTimestamp(
    std::u16string_view text, std::u16string_view format,
    std::optional<int> assumedYear = std::nullopt);

}  // namespace neomifes::logmode
