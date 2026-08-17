#pragma once

// computeGroupedLogLevels - WI-14d multi-line entry grouping. LogModel::
// build()'s own documented invariant (log_model.h) is that EVERY document
// line gets a LogLine, matched or not - a Java stack trace's continuation
// lines (the "at com.example.Foo.bar(...)" frames, "Caused by:" chains)
// show up as consecutive matched=false entries rather than being grouped
// under the preceding matched=true line. LogLine deliberately carries no
// owner-back-pointer field to stay small for multi-million-line documents
// (log_model.h's own comment), so grouping is computed here as a derived
// array instead - the same "headless, std::span<const LogLine> in, testable
// against a synthetic vector" shape log_navigation.h already established
// for this module.
//
// Before this function existed, normal_mode_wiring.cpp's
// pushLogVisualsForSession() pushed each LogLine's OWN level straight into
// RenderPipeline::setLogLineLevels() - a continuation line (matched=false)
// defaults to LogLevel::Unknown regardless of which entry it belongs to, so
// filtering to "Errors only" left a stack trace's body on screen even
// though its own ERROR header was the only reason the group exists at all.
// Routing pushLogVisualsForSession() through computeGroupedLogLevels()
// instead fixes both isLineHidden()'s filter check and drawLogLevelOnLine()'s
// color lookup at once, since both already key off the same per-line level
// array - no changes needed to RenderPipeline, EditorSession, or LogLine
// itself.

#include <span>
#include <vector>

#include "neomifes/logmode/log_pattern.h"  // LogLevel
#include "neomifes/logmode/log_model.h"    // LogLine

namespace neomifes::logmode {

// Returns one LogLevel per entry in `lines`, index-aligned with it
// (LogModel::lines()'s own dense-array invariant). A matched line keeps its
// own level. A continuation line (matched=false) inherits the level of the
// nearest PRECEDING matched line ("group leader"). Lines before the first
// matched line in the document (e.g. a log file's banner/preamble) have no
// group leader yet and stay LogLevel::Unknown - the same fail-open default
// isLineHidden() already applies when m_logLineLevels doesn't cover a line
// at all.
[[nodiscard]] std::vector<LogLevel> computeGroupedLogLevels(std::span<const LogLine> lines);

}  // namespace neomifes::logmode
