#include "neomifes/core/recent_files.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "json_string_convert.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::core {

namespace {

using detail::fromUtf8;
using detail::toUtf8;

constexpr int kFormatVersion = 1;

// Same normalize-for-identity-comparison helper as Workspace::openFile()'s
// own canonicalOrSelf() (workspace.cpp) - kept as an independent local copy
// rather than shared, matching this codebase's established "small,
// single-purpose helper, not worth a cross-module extraction" precedent.
std::filesystem::path canonicalOrSelf(const std::filesystem::path& p) {
    std::error_code             ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(p, ec);
    return ec ? p : canonical;
}

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

RecentFiles RecentFiles::loadFrom(const std::filesystem::path& path) {
    RecentFiles recentFiles;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return recentFiles;
    }
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const auto parsed = nlohmann::json::parse(content, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return recentFiles;
    }
    const auto versionIt = parsed.find("version");
    if (versionIt == parsed.end() || !versionIt->is_number_integer() ||
        versionIt->get<int>() != kFormatVersion) {
        return recentFiles;
    }
    const auto entriesIt = parsed.find("entries");
    if (entriesIt == parsed.end() || !entriesIt->is_array()) {
        return recentFiles;
    }

    for (const auto& item : *entriesIt) {
        if (recentFiles.m_entries.size() >= kMaxEntries) {
            break;
        }
        if (!item.is_string()) {
            continue;  // tolerate a stray malformed entry rather than discarding the whole file
        }
        if (auto p = pathFromUtf8(item.get<std::string>())) {
            recentFiles.m_entries.push_back(std::move(*p));
        }
    }
    return recentFiles;
}

void RecentFiles::record(const std::filesystem::path& filePath) {
    const std::filesystem::path target = canonicalOrSelf(filePath);
    const auto existing = std::ranges::find_if(m_entries, [&target](const std::filesystem::path& entry) {
        return canonicalOrSelf(entry) == target;
    });
    if (existing != m_entries.end()) {
        m_entries.erase(existing);
    }
    m_entries.insert(m_entries.begin(), filePath);
    if (m_entries.size() > kMaxEntries) {
        m_entries.resize(kMaxEntries);
    }
}

void RecentFiles::saveTo(const std::filesystem::path& path) const {
    std::vector<std::string> utf8Entries;
    utf8Entries.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        utf8Entries.push_back(pathToUtf8(entry));
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
