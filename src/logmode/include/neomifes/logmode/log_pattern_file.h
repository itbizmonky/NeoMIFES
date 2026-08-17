#pragma once

// loadLogPatternRuleFromFile / loadUserLogPatternsFromDirectory - WI-14d
// user-editable pattern files. Lets a user supply their OWN verified
// LogPatternRule for a log format this project doesn't ship a built-in
// pattern for (SAP/AWS/Azure/... - see docs/issues/
// phase_10_1_v2_extended_patterns.md for why this project cannot guess at
// those formats itself, CLAUDE.md rule 3) without waiting on a future
// release. One JSON file = one LogPatternRule, scanned from a directory
// (main.cpp resolves this to `%APPDATA%\NeoMIFES\log_patterns\`) rather
// than a single all-patterns file (Settings/KeyBindings' own shape) - a
// user adding one new format only ever touches one new file.
//
// JSON schema (per file):
//   {"version": 1, "id": "...", "displayName": "...", "pattern": "...",
//    "timestampFormat": "..."}
// `id`/`displayName`/`pattern` are required strings; `timestampFormat` is
// optional (defaults to "", the same "no timestamp concept for this rule"
// meaning log_pattern.h's own LogPatternRule::timestampFormat documents).
// `pattern` is RE2 syntax with named capture groups, identical contract to
// builtInLogPatterns()'s built-in rules (log_pattern.h).
//
// Both functions are free functions, not a loadFrom()-style static method:
// LogPatternRule is a plain aggregate with no existing methods, and every
// other function in this module (builtInLogPatterns()/detectLogPatternRule()/
// nextVisibleLogLine()) is a free function too.

#include <filesystem>
#include <optional>
#include <vector>

#include "neomifes/logmode/log_pattern.h"

namespace neomifes::logmode {

// Parses ONE LogPatternRule from the JSON file at `path`. nullopt on ANY
// failure - missing file, malformed JSON, a "version" other than 1, a
// missing/non-string required field, non-well-formed UTF-8 in any string
// field, or `pattern` failing to compile as RE2 (mirrors log_model.cpp's
// compileRule() - a hand-edited regex with a typo is an expected condition,
// not an exception). Callers don't need to distinguish WHY a file was
// rejected, only whether to skip it - same "collapse every failure mode to
// std::nullopt" shape detectLogPatternRule() already uses for this module.
[[nodiscard]] std::optional<LogPatternRule> loadLogPatternRuleFromFile(const std::filesystem::path& path);

// Scans `dir` (NOT recursive - a flat directory of *.json files, matching
// extension case-insensitively like app::detectLanguage() does) for
// pattern files, loading each via loadLogPatternRuleFromFile() in sorted
// filename order for determinism. A file that fails to load is skipped,
// never aborts the scan - same per-entry tolerance contract
// core::KeyBindings::loadFrom() established for its own malformed-entry
// handling. `dir` not existing (including platform::resolveAppDataDir()
// having failed at startup) returns an empty vector, not an error. A
// file's rule `id` colliding with one already accepted earlier in this
// same scan is skipped too, so the result never contains two rules sharing
// one id (the earlier, alphabetically-first file wins).
[[nodiscard]] std::vector<LogPatternRule> loadUserLogPatternsFromDirectory(const std::filesystem::path& dir);

}  // namespace neomifes::logmode
