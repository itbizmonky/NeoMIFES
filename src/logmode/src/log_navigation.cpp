#include "neomifes/logmode/log_navigation.h"

#include <cstddef>

namespace neomifes::logmode {

namespace {

[[nodiscard]] bool qualifies(const LogLine& line, std::uint8_t levelFilterMask) noexcept {
    return line.matched && (logLevelFilterBit(line.level) & levelFilterMask) != 0;
}

}  // namespace

std::optional<document::LineNumber> nextVisibleLogLine(std::span<const LogLine> lines,
                                                        document::LineNumber from,
                                                        std::uint8_t levelFilterMask) noexcept {
    if (lines.empty()) {
        return std::nullopt;
    }
    const std::size_t count = lines.size();
    // offset runs 1..count so every index is visited exactly once,
    // starting just after `from` and wrapping around - same "search past,
    // then wrap to the start" shape core::BookmarkManager::next() uses.
    // Modulo arithmetic handles `from` being out of range defensively
    // (should not happen given LogModel::build()'s dense-array invariant,
    // but this never indexes out of bounds regardless).
    for (std::size_t offset = 1; offset <= count; ++offset) {
        const std::size_t index = (static_cast<std::size_t>(from) + offset) % count;
        if (qualifies(lines[index], levelFilterMask)) {
            return static_cast<document::LineNumber>(index);
        }
    }
    return std::nullopt;
}

std::optional<document::LineNumber> previousVisibleLogLine(std::span<const LogLine> lines,
                                                            document::LineNumber from,
                                                            std::uint8_t levelFilterMask) noexcept {
    if (lines.empty()) {
        return std::nullopt;
    }
    const std::size_t count = lines.size();
    // `count` added before subtracting `offset` to avoid unsigned
    // underflow when from < offset - same wraparound coverage as
    // nextVisibleLogLine() above, walked in the opposite direction.
    for (std::size_t offset = 1; offset <= count; ++offset) {
        const std::size_t index = (static_cast<std::size_t>(from) + count - offset) % count;
        if (qualifies(lines[index], levelFilterMask)) {
            return static_cast<document::LineNumber>(index);
        }
    }
    return std::nullopt;
}

}  // namespace neomifes::logmode
