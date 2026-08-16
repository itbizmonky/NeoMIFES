#pragma once

// LogPatternRule / LogLevel - line-oriented log format description (WI-14a,
// Phase 10.1 headless core). Each rule is an RE2 pattern using named
// capture groups (e.g. "(?P<timestamp>...)", "(?P<level>...)") rather than
// positional indices, so LogModel can resolve field offsets once per rule
// via RE2::NamedCapturingGroups() instead of hardcoding submatch numbers.
//
// builtInLogPatterns() ships exactly 4 rules, all for publicly documented,
// verifiable standards (IETF RFC 5424/3164 syslog, the Apache/Nginx
// Common+Combined Log Format, and a generic ISO-8601-plus-level line
// matching common log4j/logback output conventions). Vendor-specific
// formats (SAP/AWS CloudTrail/Azure Monitor/Kubernetes/Docker/...) are
// deliberately NOT included - their exact on-disk layout is not public,
// stable, testable knowledge, and guessing at one from memory would be
// exactly the speculative implementation CLAUDE.md rule 3 forbids. See
// docs/issues/ "Phase 10.1 v2.0拡張候補" for the deferred list.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neomifes::logmode {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warning, Error, Fatal, Unknown };

// ASCII-only casefold, synonym-aware (WARN/WARNING, ERR/ERROR, DBG/DEBUG,
// FATAL/CRITICAL). Not per-rule configurable yet - LogPatternRule has no
// levelMap field (deferred until a second consumer needs custom
// vocabulary, e.g. once user-editable pattern files land, WI-14d).
[[nodiscard]] LogLevel parseLevel(std::u16string_view text) noexcept;

struct LogPatternRule {
    std::u16string id;               // stable id, e.g. u"rfc5424_syslog"
    std::u16string displayName;      // e.g. u"Syslog (RFC 5424)"
    std::u16string pattern;          // RE2 syntax, named groups. UTF-16 to
                                      // match this project's internal string
                                      // standard (CLAUDE.md sec.4) and
                                      // search::Query::pattern's existing
                                      // convention; converted to UTF-8 once
                                      // at compile time (see log_model.cpp).
    std::u16string timestampFormat;  // std::chrono::parse-compatible format
                                      // string; empty if this rule has no
                                      // "timestamp" named group (e.g. the
                                      // Apache/Nginx Combined Log Format
                                      // rule below does have one - only a
                                      // rule with truly no time concept
                                      // would leave this empty, and none of
                                      // the 4 built-ins currently do).
};

// The 4 built-in rules (WI-14a scope), in a stable order. See this header's
// comment above for why vendor-specific formats are excluded.
[[nodiscard]] const std::vector<LogPatternRule>& builtInLogPatterns();

}  // namespace neomifes::logmode
