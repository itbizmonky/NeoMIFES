#include <gtest/gtest.h>

#include "neomifes/document/document.h"
#include "neomifes/render/syntax_worker.h"
#include "neomifes/syntax/incremental_parser.h"

namespace {

using neomifes::document::EditDelta;
using neomifes::render::toReparseEdit;
using neomifes::syntax::ReparseEdit;

TEST(ToReparseEditTest, SingleLineEditDoublesBytePositionsAndColumnsUnscaledRows) {
    const EditDelta delta{
        .startPos     = 10,
        .startLine    = 0,
        .startColumn  = 10,
        .oldEndPos    = 12,
        .oldEndLine   = 0,
        .oldEndColumn = 12,
        .newEndPos    = 13,
        .newEndLine   = 0,
        .newEndColumn = 13,
    };
    const ReparseEdit edit = toReparseEdit(delta);
    EXPECT_EQ(edit.startByte, 20U);
    EXPECT_EQ(edit.oldEndByte, 24U);
    EXPECT_EQ(edit.newEndByte, 26U);
    EXPECT_EQ(edit.startRow, 0U);
    EXPECT_EQ(edit.startColumn, 20U);
    EXPECT_EQ(edit.oldEndRow, 0U);
    EXPECT_EQ(edit.oldEndColumn, 24U);
    EXPECT_EQ(edit.newEndRow, 0U);
    EXPECT_EQ(edit.newEndColumn, 26U);
}

// Splitting a line in two (e.g. a newline inserted mid-line) - oldEndLine
// stays on the start line but newEndLine advances, exercising that each
// field is converted independently rather than assuming old/new share a
// row.
TEST(ToReparseEditTest, MultiLineEditConvertsEachEndpointIndependently) {
    const EditDelta delta{
        .startPos     = 100,
        .startLine    = 3,
        .startColumn  = 5,
        .oldEndPos    = 101,
        .oldEndLine   = 3,
        .oldEndColumn = 6,
        .newEndPos    = 101,
        .newEndLine   = 4,
        .newEndColumn = 0,
    };
    const ReparseEdit edit = toReparseEdit(delta);
    EXPECT_EQ(edit.startByte, 200U);
    EXPECT_EQ(edit.oldEndByte, 202U);
    EXPECT_EQ(edit.newEndByte, 202U);
    EXPECT_EQ(edit.startRow, 3U);
    EXPECT_EQ(edit.startColumn, 10U);
    EXPECT_EQ(edit.oldEndRow, 3U);
    EXPECT_EQ(edit.oldEndColumn, 12U);
    EXPECT_EQ(edit.newEndRow, 4U);
    EXPECT_EQ(edit.newEndColumn, 0U);
}

}  // namespace
