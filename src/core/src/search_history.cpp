#include "neomifes/core/search_history.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

#include <nlohmann/json.hpp>

#include "json_string_convert.h"

namespace neomifes::core {

namespace {

using detail::fromUtf8;
using detail::toUtf8;

constexpr int kFormatVersion = 1;

}  // namespace

SearchHistory SearchHistory::loadFrom(const std::filesystem::path& path) {
    SearchHistory history;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return history;
    }
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const auto parsed = nlohmann::json::parse(content, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return history;
    }
    const auto versionIt = parsed.find("version");
    if (versionIt == parsed.end() || !versionIt->is_number_integer() ||
        versionIt->get<int>() != kFormatVersion) {
        return history;
    }
    const auto entriesIt = parsed.find("entries");
    if (entriesIt == parsed.end() || !entriesIt->is_array()) {
        return history;
    }

    for (const auto& item : *entriesIt) {
        if (history.m_entries.size() >= kMaxEntries) {
            break;
        }
        if (!item.is_string()) {
            continue;  // tolerate a stray malformed entry rather than discarding the whole file
        }
        if (auto text = fromUtf8(item.get<std::string>())) {
            history.m_entries.push_back(std::move(*text));
        }
    }
    return history;
}

void SearchHistory::record(std::u16string_view query) {
    if (query.empty()) {
        return;
    }
    const auto existing = std::ranges::find(m_entries, query);
    if (existing != m_entries.end()) {
        m_entries.erase(existing);
    }
    m_entries.insert(m_entries.begin(), std::u16string(query));
    if (m_entries.size() > kMaxEntries) {
        m_entries.resize(kMaxEntries);
    }
}

std::optional<std::u16string_view> SearchHistory::older(std::u16string_view currentText) const noexcept {
    if (m_entries.empty()) {
        return std::nullopt;
    }
    const auto it = std::ranges::find(m_entries, currentText);
    if (it == m_entries.end()) {
        return m_entries.front();  // not currently browsing - start at the most recent
    }
    const auto index = static_cast<std::size_t>(std::distance(m_entries.begin(), it));
    if (index + 1 >= m_entries.size()) {
        return std::nullopt;  // already at the oldest - clamp, don't wrap
    }
    return m_entries[index + 1];
}

std::optional<std::u16string_view> SearchHistory::newer(std::u16string_view currentText) const noexcept {
    if (m_entries.empty()) {
        return std::nullopt;
    }
    const auto it = std::ranges::find(m_entries, currentText);
    if (it == m_entries.end() || it == m_entries.begin()) {
        return std::nullopt;  // not found, or already at the newest
    }
    const auto index = static_cast<std::size_t>(std::distance(m_entries.begin(), it));
    return m_entries[index - 1];
}

void SearchHistory::saveTo(const std::filesystem::path& path) const {
    std::vector<std::string> utf8Entries;
    utf8Entries.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        utf8Entries.push_back(toUtf8(entry));
    }

    nlohmann::json j;
    j["version"] = kFormatVersion;
    j["entries"] = utf8Entries;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out << j.dump();
}

}  // namespace neomifes::core
