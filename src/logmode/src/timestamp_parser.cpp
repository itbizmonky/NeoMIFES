#include "neomifes/logmode/timestamp_parser.h"

#include <sstream>
#include <string>

namespace neomifes::logmode {

namespace {

// Timestamps and format strings are ASCII-only by construction (digits,
// letters, '-', ':', '.', ',', '+', spaces, '%' specifiers) - every
// LogPatternRule::pattern's "timestamp" capture group and every
// timestampFormat in builtInLogPatterns() only ever produces/consumes
// ASCII text. A truncating per-code-unit cast is therefore sufficient
// here; this deliberately does NOT reuse util::toUtf8WithOffsets()
// (Phase 5a), which exists to handle arbitrary Unicode text and builds an
// offset table this call site has no use for.
[[nodiscard]] std::string toAscii(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char16_t ch : text) {
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

}  // namespace

std::optional<Timestamp> parseTimestamp(std::u16string_view text, std::u16string_view format,
                                         std::optional<int> assumedYear) {
    std::string textAscii   = toAscii(text);
    std::string formatAscii = toAscii(format);

    // Finding 1 (see header comment): sys_time can't resolve without a
    // year. Inject one when the format itself has none.
    if (assumedYear.has_value() && formatAscii.find("%Y") == std::string::npos) {
        textAscii   = std::to_string(*assumedYear) + " " + textAscii;
        formatAscii = "%Y " + formatAscii;
    }

    // Finding 2: %Ez rejects a literal "Z" (Zulu) suffix RFC 5424 uses as
    // shorthand for "+00:00".
    if (formatAscii.find("%Ez") != std::string::npos && !textAscii.empty() &&
        textAscii.back() == 'Z') {
        textAscii.pop_back();
        textAscii += "+00:00";
    }

    std::istringstream iss(textAscii);
    Timestamp           tp{};
    iss >> std::chrono::parse(formatAscii, tp);
    if (iss.fail()) {
        return std::nullopt;
    }

    // Finding 3: a mismatched fractional-second separator (or any other
    // trailing garbage) can leave characters unconsumed without setting
    // failbit - require the whole input (after trailing whitespace) to
    // have been read.
    if (!(iss >> std::ws).eof()) {
        return std::nullopt;
    }

    return tp;
}

}  // namespace neomifes::logmode
