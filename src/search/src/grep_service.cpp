#include "neomifes/search/grep_service.h"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>
#include <variant>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/util/glob_match.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::search {

namespace {

namespace fs = std::filesystem;

using document::Document;
using document::LineNumber;
using document::LoadResult;
using document::TextPos;
using document::TextRange;

bool matchesAnyGlob(const std::vector<std::u16string>& globs, std::u16string_view filename) {
    return std::ranges::any_of(
        globs, [&](const std::u16string& pattern) { return util::globMatch(pattern, filename); });
}

// Exclude wins on conflict (a filename matching both an include and an
// exclude glob is excluded) - the more conservative "safer to under-search
// than over-search" default, matching how --include/--exclude combos are
// conventionally resolved in real grep tools.
bool shouldProcessFile(const GrepQuery& query, const fs::path& filePath) {
    const fs::path             filenamePath = filePath.filename();
    const std::u16string_view filename     = util::fromWstringView(filenamePath.native());
    if (!query.excludeGlobs.empty() && matchesAnyGlob(query.excludeGlobs, filename)) {
        return false;
    }
    if (query.includeGlobs.empty()) {
        return true;
    }
    return matchesAnyGlob(query.includeGlobs, filename);
}

// Loads one file and appends every SearchService match as a GrepMatch. A
// LoadError (NotFound/PermissionDenied/IoFailure/InvalidUtf8/TooLarge/
// Unknown) just means "skip this file" - matches how grep/ripgrep skip
// unreadable or binary files rather than aborting the whole query. This is
// deliberately not distinguished by error kind: 5c1 has no UI to surface a
// per-file diagnostic to, and inventing one now would be speculative.
void grepOneFile(const GrepQuery& query, const fs::path& path, std::vector<GrepMatch>& out) {
    auto loaded = document::loadUtf8File(path);
    if (!std::holds_alternative<LoadResult>(loaded)) {
        return;
    }
    const Document& doc = *std::get<LoadResult>(loaded).document;

    const std::vector<Match> matches = SearchService::findAll(doc, query.query);
    if (matches.empty()) {
        return;  // skip snapshot()/line lookup cost entirely
    }

    const auto snapshot = doc.snapshot();
    for (const Match& match : matches) {
        const LineNumber line      = doc.offsetToLine(match.range.start);
        const TextPos     lineStart = doc.lineToOffset(line);
        const TextPos     lineEnd =
            (line + 1 >= doc.lineCount()) ? doc.length() : doc.lineToOffset(line + 1);
        std::u16string lineText = snapshot->extract(TextRange{.start = lineStart, .end = lineEnd});
        while (!lineText.empty() && (lineText.back() == u'\n' || lineText.back() == u'\r')) {
            lineText.pop_back();
        }
        const auto lineTextSize = static_cast<TextPos>(lineText.size());
        out.push_back(GrepMatch{
            .path        = path,
            .line        = line,
            .columnRange = TextRange{.start = match.range.start - lineStart,
                                     .end   = std::min(match.range.end - lineStart, lineTextSize)},
            .lineText    = std::move(lineText),
        });
    }
}

// A root that doesn't exist, isn't readable, or is actually a file (not a
// directory) is handled here rather than thrown - the query's roots are
// effectively user input (e.g. a typed-in Grep dialog path), a system
// boundary CLAUDE.md says to validate, but GrepService::findAll()'s flat
// vector return has no error channel for "one bad root among several", and
// a typo in one root should not blank out results from the others (same
// reasoning ripgrep/grep -r use). A root that IS a regular file is treated
// as "grep this one file", which real grep tools accept too.
void grepOneRoot(const GrepQuery& query, const fs::path& root, std::vector<GrepMatch>& out) {
    std::error_code ec;
    if (fs::is_regular_file(root, ec)) {
        if (shouldProcessFile(query, root)) {
            grepOneFile(query, root, out);
        }
        return;
    }
    if (ec || !fs::is_directory(root, ec) || ec) {
        return;
    }

    auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        return;
    }
    for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            break;  // stop *this root's* traversal only; other roots still run
        }
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc) || fileEc) {
            continue;
        }
        if (shouldProcessFile(query, it->path())) {
            grepOneFile(query, it->path(), out);
        }
    }
}

}  // namespace

std::vector<GrepMatch> GrepService::findAll(const GrepQuery& query) {
    std::vector<GrepMatch> results;
    if (query.query.pattern.empty()) {
        return results;  // mirrors SearchService::findAll()'s own early-out
    }
    for (const auto& root : query.roots) {
        grepOneRoot(query, root, results);
    }
    return results;
}

}  // namespace neomifes::search
