#pragma once

// resolveTagJumpPath - resolves a util::TagJumpReference's raw path text
// against a base directory (Phase 5c4). Header-only, mirroring
// grep_query_builder.h's shape: a pure function the caller (main.cpp) feeds
// std::filesystem::current_path() into, rather than this function calling
// it internally, so it stays headlessly unit-testable.
//
// Why current_path() and not "the currently open file's directory": this
// codebase has no tracked "currently open file path" after startup (main.cpp
// consumes --open's path once at construction and discards it), but even if
// it did, that would be the wrong base directory for this feature's primary
// use case - MSVC/MSBuild always report paths relative to the build's
// invocation directory, not whatever file happens to be open in the editor
// when the user pastes build output and presses F12. Resolving against the
// process's own cwd is both simpler (no new state to track) and more
// correct (matches how the referenced paths were actually generated) -
// see the Phase 5c4 plan's Context section for the full reasoning.

#include <filesystem>
#include <string_view>

#include "neomifes/util/wchar_cast.h"

namespace neomifes::app {

// Absolute paths (drive-letter `C:\...`, UNC `\\server\share\...`) pass
// through unchanged; anything else is joined onto baseDir.
[[nodiscard]] inline std::filesystem::path resolveTagJumpPath(std::u16string_view rawPath,
                                                               const std::filesystem::path& baseDir) {
    const std::filesystem::path parsed(neomifes::util::toWstringView(rawPath));
    return parsed.is_absolute() ? parsed : baseDir / parsed;
}

}  // namespace neomifes::app
