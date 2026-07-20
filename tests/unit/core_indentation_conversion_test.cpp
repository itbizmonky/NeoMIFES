#include <gtest/gtest.h>

#include <vector>

#include "neomifes/core/cursor.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/indentation_conversion.h"
#include "neomifes/core/replace_all_command.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::core::Cursor;
using neomifes::core::computeIndentationConversionEdits;
using neomifes::core::ExecutionContext;
using neomifes::core::IndentationConversionTarget;
using neomifes::core::ReplaceAllCommand;
using neomifes::core::SelectionModel;
using neomifes::document::Document;
using neomifes::document::TextRange;

TEST(IndentationConversionTest, TabsToSpacesConvertsSingleLeadingTab) {
    Document doc;
    doc.insertText(0, u"\tfoo\nbar");

    const auto edits =
        computeIndentationConversionEdits(IndentationConversionTarget::TabsToSpaces, 4, doc);

    ASSERT_EQ(edits.size(), 1U);
    EXPECT_EQ(edits[0].range, (TextRange{.start = 0, .end = 1}));
    EXPECT_EQ(edits[0].insertedText, u"    ");
}

TEST(IndentationConversionTest, SpacesToTabsConvertsExactMultiple) {
    Document doc;
    doc.insertText(0, u"    foo\nbar");

    const auto edits =
        computeIndentationConversionEdits(IndentationConversionTarget::SpacesToTabs, 4, doc);

    ASSERT_EQ(edits.size(), 1U);
    EXPECT_EQ(edits[0].range, (TextRange{.start = 0, .end = 4}));
    EXPECT_EQ(edits[0].insertedText, u"\t");
}

TEST(IndentationConversionTest, SpacesToTabsLeavesRemainderAsSpaces) {
    Document doc;
    doc.insertText(0, u"      foo");  // 6 spaces, tabWidth 4 -> 1 tab + 2 spaces

    const auto edits =
        computeIndentationConversionEdits(IndentationConversionTarget::SpacesToTabs, 4, doc);

    ASSERT_EQ(edits.size(), 1U);
    EXPECT_EQ(edits[0].range, (TextRange{.start = 0, .end = 6}));
    EXPECT_EQ(edits[0].insertedText, u"\t  ");
}

TEST(IndentationConversionTest, LineAlreadyInTargetFormIsOmitted) {
    Document doc;
    doc.insertText(0, u"\tfoo");

    const auto edits =
        computeIndentationConversionEdits(IndentationConversionTarget::SpacesToTabs, 4, doc);

    EXPECT_TRUE(edits.empty());
}

TEST(IndentationConversionTest, LineWithNoLeadingWhitespaceIsOmitted) {
    Document doc;
    doc.insertText(0, u"foo\nbar");

    const auto edits =
        computeIndentationConversionEdits(IndentationConversionTarget::TabsToSpaces, 4, doc);

    EXPECT_TRUE(edits.empty());
}

TEST(IndentationConversionTest, OnlyLinesNeedingChangeAppearInResult) {
    Document doc;
    doc.insertText(0, u"\tone\n  two\nthree");  // line0: tab, line1: 2 spaces, line2: none

    const auto edits =
        computeIndentationConversionEdits(IndentationConversionTarget::TabsToSpaces, 4, doc);

    ASSERT_EQ(edits.size(), 1U);
    EXPECT_EQ(edits[0].range, (TextRange{.start = 0, .end = 1}));
    EXPECT_EQ(edits[0].insertedText, u"    ");
}

TEST(IndentationConversionTest, EmbeddedTabInLeadingSpaceRunIsCopiedThroughAndResetsCount) {
    Document doc;
    // 5 spaces, a tab, then 3 more spaces - tabWidth 4. The first 4 spaces
    // collapse to a tab; the 5th (run remainder before the embedded tab)
    // stays a space; the existing tab is copied through unchanged; and the
    // trailing 3 spaces stay spaces because the count resets at the embedded
    // tab rather than accumulating with the leading run to reach tabWidth.
    doc.insertText(0, u"     \t   foo");

    const auto edits =
        computeIndentationConversionEdits(IndentationConversionTarget::SpacesToTabs, 4, doc);

    ASSERT_EQ(edits.size(), 1U);
    EXPECT_EQ(edits[0].range, (TextRange{.start = 0, .end = 9}));
    EXPECT_EQ(edits[0].insertedText, u"\t \t   ");
}

TEST(IndentationConversionTest, ResultDispatchesThroughReplaceAllCommandEndToEnd) {
    Document doc;
    doc.insertText(0, u"\tone\n\ttwo");

    auto edits = computeIndentationConversionEdits(IndentationConversionTarget::TabsToSpaces, 4, doc);
    ASSERT_EQ(edits.size(), 2U);

    SelectionModel             selection;
    ExecutionContext           ctx(doc, selection);
    const std::vector<Cursor>  before{Cursor{.position = 0, .anchor = 0, .isPrimary = true}};
    ReplaceAllCommand          cmd(std::move(edits), before);

    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"    one\n    two");

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"\tone\n\ttwo");
}

}  // namespace
