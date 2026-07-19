#include "neomifes/core/bookmark_manager.h"

#include <algorithm>

namespace neomifes::core {

void BookmarkManager::toggle(document::LineNumber line) {
    const auto it = std::ranges::lower_bound(m_lines, line);
    if (it != m_lines.end() && *it == line) {
        m_lines.erase(it);
    } else {
        m_lines.insert(it, line);
    }
}

bool BookmarkManager::isBookmarked(document::LineNumber line) const noexcept {
    return std::ranges::binary_search(m_lines, line);
}

std::optional<document::LineNumber> BookmarkManager::next(document::LineNumber from) const noexcept {
    if (m_lines.empty()) {
        return std::nullopt;
    }
    const auto it = std::ranges::upper_bound(m_lines, from);
    return (it != m_lines.end()) ? *it : m_lines.front();
}

std::optional<document::LineNumber> BookmarkManager::previous(document::LineNumber from) const noexcept {
    if (m_lines.empty()) {
        return std::nullopt;
    }
    const auto it = std::ranges::lower_bound(m_lines, from);
    return (it != m_lines.begin()) ? *std::prev(it) : m_lines.back();
}

}  // namespace neomifes::core
