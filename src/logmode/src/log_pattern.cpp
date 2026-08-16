#include "neomifes/logmode/log_pattern.h"

#include <algorithm>
#include <cctype>

namespace neomifes::logmode {

namespace {

// ASCII-only lowercase, matching app::detectLanguage()'s extension-casefold
// convention (no locale, no Unicode case folding needed - level tokens are
// always ASCII).
[[nodiscard]] std::u16string toLowerAscii(std::u16string_view text) {
    std::u16string out(text);
    std::ranges::transform(out, out.begin(), [](char16_t ch) {
        return (ch >= u'A' && ch <= u'Z') ? static_cast<char16_t>(ch - u'A' + u'a') : ch;
    });
    return out;
}

}  // namespace

LogLevel parseLevel(std::u16string_view text) noexcept {
    const std::u16string lower = toLowerAscii(text);
    if (lower == u"trace") {
        return LogLevel::Trace;
    }
    if (lower == u"debug" || lower == u"dbg") {
        return LogLevel::Debug;
    }
    if (lower == u"info") {
        return LogLevel::Info;
    }
    if (lower == u"warn" || lower == u"warning") {
        return LogLevel::Warning;
    }
    if (lower == u"error" || lower == u"err") {
        return LogLevel::Error;
    }
    if (lower == u"fatal" || lower == u"critical") {
        return LogLevel::Fatal;
    }
    return LogLevel::Unknown;
}

const std::vector<LogPatternRule>& builtInLogPatterns() {
    // Regex/format pairs verified against a standalone std::chrono::parse
    // probe (WI-14a) before being committed here - see this WI's
    // "実装後の確定事項" in build_plan.md for the concrete findings that
    // shaped these patterns (sys_time parse requires a resolvable year;
    // %Ez rejects a literal "Z" offset suffix; RFC 3164 has no year field
    // at all).
    static const std::vector<LogPatternRule> kPatterns = {
        // RFC 5424 syslog. Severity is encoded numerically inside <PRI>
        // (facility*8 + severity), not as a separate text field, so this
        // rule has no "level" named group - LogModel::build() leaves
        // matched lines at LogLevel::Unknown, exactly like the CLF rule
        // below. The "sd" (structured-data) field is matched non-greedily
        // to handle the common single-SD-block or NILVALUE ("-") case;
        // multiple concatenated SD blocks are a known, documented
        // simplification not handled here.
        LogPatternRule{
            .id          = u"rfc5424_syslog",
            .displayName = u"Syslog (RFC 5424)",
            .pattern     = uR"RX(^<(?P<pri>\d{1,3})>(?P<version>\d+) (?P<timestamp>\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,6})?(?:Z|[+-]\d{2}:\d{2})) (?P<hostname>\S+) (?P<appname>\S+) (?P<procid>\S+) (?P<msgid>\S+) (?P<sd>-|\[.*?\])(?: (?P<message>.*))?$)RX",
            .timestampFormat = u"%FT%T%Ez",
        },
        // RFC 3164 (BSD syslog). Has no year field at all (a property of
        // the RFC itself) - LogModel::build() must supply parseTimestamp()
        // an assumedYear for this rule's format to resolve to a sys_time.
        // Like RFC 5424, no textual "level" field exists.
        LogPatternRule{
            .id          = u"rfc3164_syslog",
            .displayName = u"Syslog (RFC 3164 / BSD)",
            .pattern     = uR"RX(^<(?P<pri>\d{1,3})>(?P<timestamp>[A-Z][a-z]{2}\s+\d{1,2}\s\d{2}:\d{2}:\d{2}) (?P<hostname>\S+) (?P<message>.*)$)RX",
            .timestampFormat = u"%b %d %H:%M:%S",
        },
        // Apache/Nginx Common + Combined Log Format. The
        // referer/user-agent tail is optional (Combined has it, Common
        // does not) - both are matched by this one rule. No "level" field:
        // an access log is a request record, not an event/severity log
        // (mapping HTTP status codes to LogLevel is a heuristic this
        // project does not implement, per this WI's 設計方針7).
        LogPatternRule{
            .id          = u"apache_nginx_clf",
            .displayName = u"Apache/Nginx Common・Combined Log Format",
            .pattern     = uR"RX(^(?P<host>\S+) (?P<ident>\S+) (?P<authuser>\S+) \[(?P<timestamp>\d{2}/[A-Z][a-z]{2}/\d{4}:\d{2}:\d{2}:\d{2} [+-]\d{4})\] "(?P<request>[^"]*)" (?P<status>\d{3}) (?P<size>\S+)(?: "(?P<referer>[^"]*)" "(?P<useragent>[^"]*)")?$)RX",
            .timestampFormat = u"%d/%b/%Y:%T %z",
        },
        // Generic ISO-8601-plus-level line, matching common log4j/logback
        // output conventions (NOT a claim of exact fidelity to any single
        // vendor's default pattern). Deliberately restricted to a single
        // separator style (space between date and time, '.' for the
        // fractional-second decimal point) rather than accepting the 'T'
        // ISO-8601 separator or ',' decimal comma too - std::chrono::parse
        // uses ONE fixed format string per rule, and the probe confirmed a
        // comma-separated fraction silently truncates to whole seconds
        // instead of failing (the trailing ",123" is simply left
        // unconsumed) rather than erroring, which parseTimestamp() now
        // detects and rejects via a full-stream-consumption check - but a
        // regex that accepted both separators would then non-deterministically
        // "match but fail to parse the timestamp" depending on which style
        // appeared, which is worse than not matching the line at all.
        LogPatternRule{
            .id          = u"generic_iso8601_level",
            .displayName = u"汎用 ISO-8601 + レベル行",
            .pattern     = uR"RX(^(?P<timestamp>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}(?:\.\d{1,6})?)\s+(?P<level>TRACE|DEBUG|INFO|WARN|WARNING|ERROR|FATAL)\b\s*(?P<message>.*)$)RX",
            .timestampFormat = u"%Y-%m-%d %H:%M:%S",
        },
    };
    return kPatterns;
}

}  // namespace neomifes::logmode
