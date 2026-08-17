#include "neomifes/logmode/log_grouping.h"

namespace neomifes::logmode {

std::vector<LogLevel> computeGroupedLogLevels(std::span<const LogLine> lines) {
    std::vector<LogLevel> levels;
    levels.reserve(lines.size());
    // No group leader yet for any line before the document's first matched
    // line - LogLevel::Unknown is the correct fail-open default here (same
    // as isLineHidden() treating an out-of-range line as not filtered).
    LogLevel currentGroupLevel = LogLevel::Unknown;
    for (const LogLine& line : lines) {
        if (line.matched) {
            currentGroupLevel = line.level;
        }
        levels.push_back(currentGroupLevel);
    }
    return levels;
}

}  // namespace neomifes::logmode
