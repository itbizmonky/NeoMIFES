#include "neomifes/core/settings.h"

#include <fstream>
#include <iterator>
#include <span>

#include <nlohmann/json.hpp>

#include "neomifes/encoding/encoding.h"

namespace neomifes::core {

namespace {

// Same UTF-8<->UTF-16 boundary-crossing helpers as search_history.cpp -
// nlohmann::json works in UTF-8 (std::string), this codebase's internal
// string type is UTF-16 (std::u16string, CLAUDE.md §4). Duplicated rather
// than shared: SearchHistory already accepted this duplication once: if a
// third JSON-backed persisted type needs the same helpers, that's the
// signal to extract them into neomifes::util instead of guessing at a
// shared home now.

[[nodiscard]] std::string toUtf8(std::u16string_view text) {
    const auto  result = encoding::encode(text, encoding::Encoding::Utf8);
    const auto& bytes   = std::get<std::vector<std::byte>>(result);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::optional<std::u16string> fromUtf8(std::string_view text) {
    const auto bytes  = std::as_bytes(std::span(text.data(), text.size()));
    auto       result = encoding::decode(bytes, encoding::Encoding::Utf8);
    if (std::holds_alternative<encoding::DecodeError>(result)) {
        return std::nullopt;
    }
    return std::move(std::get<std::u16string>(result));
}

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

    if (const auto it = parsed.find("fontFamily"); it != parsed.end() && it->is_string()) {
        if (auto text = fromUtf8(it->get<std::string>())) {
            settings.fontFamily = std::move(*text);
        }
    }
    if (const auto it = parsed.find("fontSizeDips"); it != parsed.end() && it->is_number()) {
        settings.fontSizeDips = clampFontSizeDips(it->get<float>());
    }
    if (const auto it = parsed.find("tabWidth"); it != parsed.end() && it->is_number_unsigned()) {
        settings.tabWidth = clampTabWidth(it->get<std::uint32_t>());
    }
    if (const auto it = parsed.find("insertSpacesForTab"); it != parsed.end() && it->is_boolean()) {
        settings.insertSpacesForTab = it->get<bool>();
    }
    if (const auto it = parsed.find("showLineNumbers"); it != parsed.end() && it->is_boolean()) {
        settings.showLineNumbers = it->get<bool>();
    }
    if (const auto it = parsed.find("showMinimap"); it != parsed.end() && it->is_boolean()) {
        settings.showMinimap = it->get<bool>();
    }
    if (const auto it = parsed.find("autoSaveIntervalSeconds"); it != parsed.end() && it->is_number_unsigned()) {
        settings.autoSaveIntervalSeconds = it->get<std::uint32_t>();
    }
    if (const auto it = parsed.find("themeName"); it != parsed.end() && it->is_string()) {
        if (auto text = fromUtf8(it->get<std::string>())) {
            settings.themeName = std::move(*text);
        }
    }

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
    j["autoSaveIntervalSeconds"]  = autoSaveIntervalSeconds;
    j["themeName"]                = toUtf8(themeName);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out << j.dump();
}

}  // namespace neomifes::core
