#include <gtest/gtest.h>

#include <vector>

#include "neomifes/app/reindent.h"
#include "neomifes/core/cursor.h"
#include "neomifes/document/document.h"
#include "neomifes/syntax/syntax.h"

namespace {

using neomifes::app::computeReindentEdits;
using neomifes::app::reindentSelectedLineRanges;
using neomifes::app::supportsReindent;
using neomifes::core::Cursor;
using neomifes::document::Document;
using neomifes::document::TextRange;
using neomifes::syntax::Language;

std::vector<Cursor> noSelection() {
    return {Cursor{.position = 0, .anchor = 0, .isPrimary = true}};
}

TEST(AppReindentTest, SimpleNestedBracesIndentEachLevel) {
    Document doc;
    doc.insertText(0, u"void foo() {\nif (x) {\nbar();\n}\n}");

    const auto edits = computeReindentEdits(doc, Language::Cpp, 4, /*insertSpacesForTab=*/true, noSelection());

    ASSERT_EQ(edits.size(), 3U);
    EXPECT_EQ(edits[0].range, (TextRange{.start = 13, .end = 13}));  // start of "if (x) {"
    EXPECT_EQ(edits[0].insertedText, u"    ");
    EXPECT_EQ(edits[1].insertedText, u"        ");  // "bar();" at depth 2
    EXPECT_EQ(edits[2].insertedText, u"    ");       // first "}" at depth 1
}

TEST(AppReindentTest, LineStartingWithCloseBraceDedentsBeforeCounting) {
    Document doc;
    doc.insertText(0, u"if (x) {\nfoo();\n} else {\nbar();\n}");

    const auto edits = computeReindentEdits(doc, Language::Cpp, 4, /*insertSpacesForTab=*/true, noSelection());

    // line0 "if (x) {", line2 "} else {" (dedents to depth 0 for its own
    // display), and line4 "}" are all already correctly at depth 0 in this
    // flush-left fixture, so only the two depth-1 lines actually need an
    // edit. The key property under test: line3 "bar();" must resume at
    // depth 1 (not depth 0), proving "} else {" applied BOTH its brace
    // events (the close AND the following open) to the running depth even
    // though its own display only dedented by one level.
    ASSERT_EQ(edits.size(), 2U);
    EXPECT_EQ(edits[0].insertedText, u"    ");  // foo(); -> depth 1
    EXPECT_EQ(edits[1].insertedText, u"    ");  // bar(); -> depth 1 again
}

TEST(AppReindentTest, BracesInsideStringLiteralDoNotAffectDepth) {
    Document doc;
    // The string's own unmatched '{' must not push the running depth to 2 -
    // if it did, "bar();" below would be wrongly indented 8 spaces instead
    // of 4.
    doc.insertText(0, u"void foo() {\nconst char* s = \"unbalanced { brace\";\nbar();\n}");

    const auto edits = computeReindentEdits(doc, Language::Cpp, 4, /*insertSpacesForTab=*/true, noSelection());

    // The closing "}" is already correctly at depth 0 in this flush-left
    // fixture, so it produces no edit; the two depth-1 lines do. The key
    // property under test: bar(); must be at depth 1, not depth 2 - if the
    // string's own unmatched '{' had wrongly counted, it would be 2.
    ASSERT_EQ(edits.size(), 2U);
    EXPECT_EQ(edits[0].insertedText, u"    ");  // the string-literal line itself, depth 1
    EXPECT_EQ(edits[1].insertedText, u"    ");  // bar(); still depth 1, not 2
}

TEST(AppReindentTest, BracesInsideCommentDoNotAffectDepth) {
    Document doc;
    doc.insertText(0, u"void foo() {\n// unbalanced { comment\nbar();\n}");

    const auto edits = computeReindentEdits(doc, Language::Cpp, 4, /*insertSpacesForTab=*/true, noSelection());

    // Same "closing brace already at depth 0 needs no edit" shape as
    // BracesInsideStringLiteralDoNotAffectDepth above.
    ASSERT_EQ(edits.size(), 2U);
    EXPECT_EQ(edits[0].insertedText, u"    ");  // the comment line itself, depth 1
    EXPECT_EQ(edits[1].insertedText, u"    ");  // bar(); still depth 1
}

TEST(AppReindentTest, MultiLineCommentLinesAreLeftUntouched) {
    Document doc;
    // Lines 2-3 (the comment's interior/closing lines) must receive NO edit
    // at all, regardless of their current (deliberately mis-indented)
    // content - only the comment's opening line (1) and the surrounding
    // code (0, 4, 5) are candidates.
    doc.insertText(0, u"void foo() {\n/*\n   wrongly indented comment content\n */\nbar();\n}");

    const auto edits = computeReindentEdits(doc, Language::Cpp, 4, /*insertSpacesForTab=*/true, noSelection());

    // Expect edits only for line1 ("/*"), line4 ("bar();"); line0 and line5
    // are already correctly at depth 0.
    ASSERT_EQ(edits.size(), 2U);
    EXPECT_EQ(edits[0].range, (TextRange{.start = 13, .end = 13}));  // start of "/*"
    EXPECT_EQ(edits[0].insertedText, u"    ");
    EXPECT_EQ(edits[1].insertedText, u"    ");  // bar();
}

TEST(AppReindentTest, TabsVsSpacesSettingRespected) {
    Document doc;
    doc.insertText(0, u"void foo() {\nbar();\n}");

    const auto spacesEdits =
        computeReindentEdits(doc, Language::Cpp, 4, /*insertSpacesForTab=*/true, noSelection());
    ASSERT_EQ(spacesEdits.size(), 1U);
    EXPECT_EQ(spacesEdits[0].insertedText, u"    ");

    const auto tabEdits =
        computeReindentEdits(doc, Language::Cpp, 4, /*insertSpacesForTab=*/false, noSelection());
    ASSERT_EQ(tabEdits.size(), 1U);
    EXPECT_EQ(tabEdits[0].insertedText, u"\t");
}

TEST(AppReindentTest, EmptySelectionMeansWholeDocument) {
    Document doc;
    doc.insertText(0, u"void foo() {\nbar();\nbaz();\n}");

    const auto edits = computeReindentEdits(doc, Language::Cpp, 4, /*insertSpacesForTab=*/true, noSelection());

    ASSERT_EQ(edits.size(), 2U);  // bar(); and baz();, both need indenting
}

TEST(AppReindentTest, NonEmptySelectionOnlyReindentsSpannedLines) {
    Document doc;
    doc.insertText(0, u"void foo() {\nbar();\nbaz();\nqux();\n}");
    // Lines: 0 "void foo() {" 1 "bar();" 2 "baz();" 3 "qux();" 4 "}" - all
    // of 1/2/3 are mis-indented in this fixture, but the selection below
    // spans only lines 2-3 (from the start of line 2 to the start of line
    // 4, i.e. exactly the "Shift+Down to column 0 of the next line"
    // pattern) - line 1 must NOT receive an edit despite also needing one.
    const auto line2Start = doc.lineToOffset(2);
    const auto line4Start = doc.lineToOffset(4);
    const std::vector<Cursor> cursors{
        Cursor{.position = line4Start, .anchor = line2Start, .isPrimary = true}};

    const auto edits = computeReindentEdits(doc, Language::Cpp, 4, /*insertSpacesForTab=*/true, cursors);

    ASSERT_EQ(edits.size(), 2U);
    EXPECT_EQ(edits[0].range.start, line2Start);
    EXPECT_EQ(edits[1].range.start, doc.lineToOffset(3));
}

TEST(AppReindentTest, AlreadyCorrectlyIndentedInputProducesNoEdits) {
    Document doc;
    doc.insertText(0, u"void foo() {\n    if (x) {\n        bar();\n    }\n}");

    const auto edits = computeReindentEdits(doc, Language::Cpp, 4, /*insertSpacesForTab=*/true, noSelection());

    EXPECT_TRUE(edits.empty());
}

TEST(AppReindentTest, BlankLinesAreNeverTouched) {
    Document doc;
    // Line 1 is "blank" but dirty (2 stray spaces, no other content) -
    // must be left exactly as-is, not cleared and not indented.
    doc.insertText(0, u"void foo() {\n  \nbar();\n}");

    const auto edits = computeReindentEdits(doc, Language::Cpp, 4, /*insertSpacesForTab=*/true, noSelection());

    for (const auto& edit : edits) {
        EXPECT_NE(edit.range.start, doc.lineToOffset(1)) << "blank line 1 must not receive an edit";
    }
}

TEST(AppReindentTest, SupportsReindentAllowlistTable) {
    EXPECT_TRUE(supportsReindent(Language::Cpp));
    EXPECT_TRUE(supportsReindent(Language::Json));
    EXPECT_TRUE(supportsReindent(Language::Css));
    EXPECT_TRUE(supportsReindent(Language::PowerShell));
    EXPECT_FALSE(supportsReindent(Language::Python));
    EXPECT_FALSE(supportsReindent(Language::Html));
    EXPECT_FALSE(supportsReindent(Language::Shell));
    EXPECT_FALSE(supportsReindent(Language::Yaml));
    EXPECT_FALSE(supportsReindent(Language::Sql));
}

TEST(AppReindentTest, SelectedLineRangesMergesOverlappingCursors) {
    Document doc;
    doc.insertText(0, u"a\nb\nc\nd\ne\nf\ng");  // 7 one-char lines, offsets 0,2,4,6,8,10,12

    const std::vector<Cursor> cursors{
        Cursor{.position = doc.lineToOffset(3), .anchor = doc.lineToOffset(1), .isPrimary = false},  // lines 1-2
        Cursor{.position = doc.lineToOffset(4), .anchor = doc.lineToOffset(2), .isPrimary = true},   // lines 2-3
    };

    const auto ranges = reindentSelectedLineRanges(doc, cursors);

    ASSERT_EQ(ranges.size(), 1U);  // the two overlapping selections merge into one
    EXPECT_EQ(ranges[0].start, 1U);
    EXPECT_EQ(ranges[0].endInclusive, 3U);
}

TEST(AppReindentTest, SelectedLineRangesExcludesTrailingLineAtColumnZero) {
    Document doc;
    doc.insertText(0, u"a\nb\nc\nd");

    const std::vector<Cursor> cursors{
        Cursor{.position = doc.lineToOffset(2), .anchor = doc.lineToOffset(0), .isPrimary = true}};

    const auto ranges = reindentSelectedLineRanges(doc, cursors);

    ASSERT_EQ(ranges.size(), 1U);
    EXPECT_EQ(ranges[0].start, 0U);
    EXPECT_EQ(ranges[0].endInclusive, 1U);  // line 2 excluded - selection ends at its column 0
}

}  // namespace
