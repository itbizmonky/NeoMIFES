#include "neomifes/core/autosave_index.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "json_string_convert.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::core {

namespace {

using detail::fromUtf8;
using detail::toUtf8;

constexpr int kFormatVersion = 1;

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& p) {
    return toUtf8(std::u16string(util::fromWstringView(p.wstring())));
}

[[nodiscard]] std::optional<std::filesystem::path> pathFromUtf8(const std::string& text) {
    const auto decoded = fromUtf8(text);
    if (!decoded) {
        return std::nullopt;
    }
    return std::filesystem::path(std::wstring(util::toWstringView(*decoded)));
}

}  // namespace

AutosaveIndex AutosaveIndex::loadFrom(const std::filesystem::path& path) {
    AutosaveIndex index;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return index;
    }
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const auto parsed = nlohmann::json::parse(content, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return index;
    }
    const auto versionIt = parsed.find("version");
    if (versionIt == parsed.end() || !versionIt->is_number_integer() ||
        versionIt->get<int>() != kFormatVersion) {
        return index;
    }
    const auto entriesIt = parsed.find("entries");
    if (entriesIt == parsed.end() || !entriesIt->is_array()) {
        return index;
    }

    for (const auto& item : *entriesIt) {
        if (!item.is_object()) {
            continue;  // tolerate a stray malformed entry rather than discarding the whole file
        }
        const auto hashIt = item.find("hash");
        const auto pathIt = item.find("path");
        if (hashIt == item.end() || !hashIt->is_string() || pathIt == item.end() || !pathIt->is_string()) {
            continue;
        }
        auto hash = fromUtf8(hashIt->get<std::string>());
        auto originalPath = pathFromUtf8(pathIt->get<std::string>());
        if (!hash || !originalPath) {
            continue;
        }
        index.m_entries.push_back(AutosaveEntry{.hash = std::move(*hash), .originalPath = std::move(*originalPath)});
    }
    return index;
}

void AutosaveIndex::record(std::u16string_view hash, const std::filesystem::path& originalPath) {
    const auto existing =
        std::ranges::find_if(m_entries, [hash](const AutosaveEntry& entry) { return entry.hash == hash; });
    if (existing != m_entries.end()) {
        existing->originalPath = originalPath;
        return;
    }
    m_entries.push_back(AutosaveEntry{.hash = std::u16string(hash), .originalPath = originalPath});
}

void AutosaveIndex::remove(std::u16string_view hash) {
    std::erase_if(m_entries, [hash](const AutosaveEntry& entry) { return entry.hash == hash; });
}

void AutosaveIndex::saveTo(const std::filesystem::path& path) const {
    nlohmann::json entriesJson = nlohmann::json::array();
    for (const auto& entry : m_entries) {
        nlohmann::json item;
        item["hash"] = toUtf8(entry.hash);
        item["path"] = pathToUtf8(entry.originalPath);
        entriesJson.push_back(std::move(item));
    }

    nlohmann::json j;
    j["version"] = kFormatVersion;
    j["entries"] = std::move(entriesJson);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out << j.dump();
}

}  // namespace neomifes::core
