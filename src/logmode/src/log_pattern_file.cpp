#include "neomifes/logmode/log_pattern_file.h"

#include <re2/re2.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <system_error>

#include <nlohmann/json.hpp>

#include "json_string_convert.h"

namespace neomifes::logmode {

namespace {

using detail::fromUtf8;

constexpr int kFormatVersion = 1;

// Yes/no compile check only - NOT log_model.cpp's compileRule() (that
// function is anonymous-namespace private to log_model.cpp and also
// resolves timestamp/level field indices this validation step doesn't
// need). `patternUtf8` is the pattern's raw JSON string value, already
// UTF-8 (JSON strings always are) - no UTF-16 round-trip needed just to
// validate compilability.
[[nodiscard]] bool patternCompiles(const std::string& patternUtf8) {
    re2::RE2::Options options;
    options.set_log_errors(false);  // an invalid user-supplied pattern is a data problem, not worth logging
    const re2::RE2 re(patternUtf8, options);
    return re.ok();
}

// ASCII-only casefold extension match - same "towlower each wchar_t"
// convention app::detectLanguage() (syntax_language.h) already established
// for this project's file-extension checks.
[[nodiscard]] bool hasJsonExtension(const std::filesystem::path& path) {
    std::wstring ext = path.extension().wstring();
    for (wchar_t& ch : ext) {
        ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
    }
    return ext == L".json";
}

}  // namespace

std::optional<LogPatternRule> loadLogPatternRuleFromFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const auto parsed = nlohmann::json::parse(content, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return std::nullopt;
    }
    const auto versionIt = parsed.find("version");
    if (versionIt == parsed.end() || !versionIt->is_number_integer() || versionIt->get<int>() != kFormatVersion) {
        return std::nullopt;
    }

    const auto idIt          = parsed.find("id");
    const auto displayNameIt = parsed.find("displayName");
    const auto patternIt     = parsed.find("pattern");
    if (idIt == parsed.end() || !idIt->is_string() || displayNameIt == parsed.end() ||
        !displayNameIt->is_string() || patternIt == parsed.end() || !patternIt->is_string()) {
        return std::nullopt;
    }
    if (!patternCompiles(patternIt->get<std::string>())) {
        return std::nullopt;
    }

    auto id          = fromUtf8(idIt->get<std::string>());
    auto displayName = fromUtf8(displayNameIt->get<std::string>());
    auto pattern      = fromUtf8(patternIt->get<std::string>());
    if (!id || !displayName || !pattern) {
        return std::nullopt;
    }

    LogPatternRule rule;
    rule.id          = std::move(*id);
    rule.displayName = std::move(*displayName);
    rule.pattern     = std::move(*pattern);

    // Optional field - absent, non-string, or non-UTF-8 all leave
    // timestampFormat at its default-constructed "" (the same "no
    // timestamp concept for this rule" meaning the built-in rules use).
    if (const auto tsIt = parsed.find("timestampFormat"); tsIt != parsed.end() && tsIt->is_string()) {
        if (auto timestampFormat = fromUtf8(tsIt->get<std::string>())) {
            rule.timestampFormat = std::move(*timestampFormat);
        }
    }

    return rule;
}

std::vector<LogPatternRule> loadUserLogPatternsFromDirectory(const std::filesystem::path& dir) {
    std::vector<LogPatternRule> rules;

    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return rules;
    }

    // Manual increment(ec) loop, NOT a range-based for - range-based for's
    // implicit ++it calls directory_iterator's throwing operator++()
    // overload, defeating the whole point of the error_code-taking
    // constructor above. Same non-throwing discipline
    // grep_service.cpp's grepOneRoot() already established for this exact
    // situation.
    std::vector<std::filesystem::path> jsonFiles;
    auto it = std::filesystem::directory_iterator(
        dir, std::filesystem::directory_options::skip_permission_denied, ec);
    for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        std::error_code entryEc;
        if (it->is_regular_file(entryEc) && !entryEc && hasJsonExtension(it->path())) {
            jsonFiles.push_back(it->path());
        }
    }
    std::ranges::sort(jsonFiles);  // deterministic id-collision resolution (see header comment)

    for (const auto& file : jsonFiles) {
        auto rule = loadLogPatternRuleFromFile(file);
        if (!rule) {
            continue;
        }
        const bool idAlreadyTaken = std::ranges::any_of(
            rules, [&rule](const LogPatternRule& existing) { return existing.id == rule->id; });
        if (idAlreadyTaken) {
            continue;
        }
        rules.push_back(std::move(*rule));
    }
    return rules;
}

}  // namespace neomifes::logmode
