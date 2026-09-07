#include "neomifes/core/settings.h"

#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

#include "json_string_convert.h"

namespace neomifes::core {

namespace {

using detail::fromUtf8;
using detail::toUtf8;

constexpr int kFormatVersion = 1;

// Boundary clamps (settings.json is an external, user-editable file - a
// system boundary, CLAUDE.md's "validate at boundaries" rule applies here
// even though internal callers never need these checks). 0 is rejected
// because DirectWrite's SetIncrementalTabStop() requires a positive value
// and indent_guide_math.h already treats tabWidth==0 as a degenerate input;
// 32 is a generous upper bound against a corrupted/adversarial value, not a
// real product constraint.
[[nodiscard]] std::uint32_t clampTabWidth(std::uint32_t tabWidth) noexcept {
    return (tabWidth == 0 || tabWidth > 32) ? 4U : tabWidth;
}

[[nodiscard]] float clampFontSizeDips(float fontSizeDips) noexcept {
    return fontSizeDips <= 0.0F ? 14.0F : fontSizeDips;
}

// Shared by every plain boolean field below - factored out so applyFields()
// itself stays a flat sequence of one-liners for these fields instead of
// repeating the same find/type-check/assign branch per field, which is what
// pushed applyFields() over clang-tidy's cognitive-complexity threshold once
// WI-34 added the 2 scrollbar-visibility fields (readability-function-
// cognitive-complexity, src/ threshold 25).
void applyBoolField(const nlohmann::json& parsed, const char* key, bool& out) {
    if (const auto it = parsed.find(key); it != parsed.end() && it->is_boolean()) {
        out = it->get<bool>();
    }
}

// Split out of loadFrom() purely to keep clang-tidy's cognitive-complexity
// check happy (the file-open/parse/version checks plus all nine field reads
// inline pushed loadFrom past the src/ threshold of 25) - loadFrom() keeps
// the "is this file usable at all" boundary checks, this keeps the "which
// fields does it override" per-field logic.
void applyFields(const nlohmann::json& parsed, Settings& out) {
    if (const auto it = parsed.find("fontFamily"); it != parsed.end() && it->is_string()) {
        if (auto text = fromUtf8(it->get<std::string>())) {
            out.fontFamily = std::move(*text);
        }
    }
    if (const auto it = parsed.find("fontSizeDips"); it != parsed.end() && it->is_number()) {
        out.fontSizeDips = clampFontSizeDips(it->get<float>());
    }
    if (const auto it = parsed.find("tabWidth"); it != parsed.end() && it->is_number_unsigned()) {
        out.tabWidth = clampTabWidth(it->get<std::uint32_t>());
    }
    applyBoolField(parsed, "insertSpacesForTab", out.insertSpacesForTab);
    applyBoolField(parsed, "showLineNumbers", out.showLineNumbers);
    applyBoolField(parsed, "showMinimap", out.showMinimap);
    applyBoolField(parsed, "wordWrap", out.wordWrap);
    if (const auto it = parsed.find("autoSaveIntervalSeconds"); it != parsed.end() && it->is_number_unsigned()) {
        out.autoSaveIntervalSeconds = it->get<std::uint32_t>();
    }
    applyBoolField(parsed, "createBackupOnSave", out.createBackupOnSave);
    if (const auto it = parsed.find("themeName"); it != parsed.end() && it->is_string()) {
        if (auto text = fromUtf8(it->get<std::string>())) {
            out.themeName = std::move(*text);
        }
    }
    applyBoolField(parsed, "showHorizontalScrollbar", out.showHorizontalScrollbar);
    applyBoolField(parsed, "showVerticalScrollbar", out.showVerticalScrollbar);
}

}  // namespace

Settings Settings::loadFrom(const std::filesystem::path& path) {
    Settings settings;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return settings;
    }
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const auto parsed = nlohmann::json::parse(content, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return settings;
    }
    const auto versionIt = parsed.find("version");
    if (versionIt == parsed.end() || !versionIt->is_number_integer() ||
        versionIt->get<int>() != kFormatVersion) {
        return settings;
    }

    applyFields(parsed, settings);
    return settings;
}

void Settings::saveTo(const std::filesystem::path& path) const {
    nlohmann::json j;
    j["version"]                 = kFormatVersion;
    j["fontFamily"]               = toUtf8(fontFamily);
    j["fontSizeDips"]             = fontSizeDips;
    j["tabWidth"]                 = tabWidth;
    j["insertSpacesForTab"]       = insertSpacesForTab;
    j["showLineNumbers"]          = showLineNumbers;
    j["showMinimap"]              = showMinimap;
    j["wordWrap"]                  = wordWrap;
    j["autoSaveIntervalSeconds"]  = autoSaveIntervalSeconds;
    j["createBackupOnSave"]       = createBackupOnSave;
    j["themeName"]                = toUtf8(themeName);
    j["showHorizontalScrollbar"]  = showHorizontalScrollbar;
    j["showVerticalScrollbar"]    = showVerticalScrollbar;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out << j.dump();
}

}  // namespace neomifes::core
