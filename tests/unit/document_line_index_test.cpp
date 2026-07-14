#include <gtest/gtest.h>

#include "neomifes/document/document.h"

namespace {

using neomifes::document::Document;

TEST(LineIndexTest, EmptyDocumentHasOneLine) {
    Document doc;
    EXPECT_EQ(doc.lineCount(),         1u);
    EXPECT_EQ(doc.offsetToLine(0),     0u);
    EXPECT_EQ(doc.lineToOffset(0),     0u);
}

TEST(LineIndexTest, SingleLine) {
    Document doc;
    doc.insertText(0, u"hello world");
    EXPECT_EQ(doc.lineCount(),          1u);
    EXPECT_EQ(doc.offsetToLine(5),      0u);
    EXPECT_EQ(doc.offsetToLine(11),     0u);
    EXPECT_EQ(doc.lineToOffset(0),      0u);
    // Out-of-range line clamps to last line start.
    EXPECT_EQ(doc.lineToOffset(999),    0u);
}

TEST(LineIndexTest, MultipleLines) {
    Document doc;
    doc.insertText(0, u"aa\nbbb\ncccc");
    // Offsets: 0 'a' 1 'a' 2 '\n' 3 'b' 4 'b' 5 'b' 6 '\n' 7 'c' 8 'c' 9 'c' 10 'c'
    EXPECT_EQ(doc.lineCount(),      3u);
    EXPECT_EQ(doc.lineToOffset(0),  0u);
    EXPECT_EQ(doc.lineToOffset(1),  3u);
    EXPECT_EQ(doc.lineToOffset(2),  7u);
    EXPECT_EQ(doc.offsetToLine(0),  0u);
    EXPECT_EQ(doc.offsetToLine(2),  0u);
    EXPECT_EQ(doc.offsetToLine(3),  1u);
    EXPECT_EQ(doc.offsetToLine(6),  1u);
    EXPECT_EQ(doc.offsetToLine(7),  2u);
    EXPECT_EQ(doc.offsetToLine(10), 2u);
}

TEST(LineIndexTest, TrailingNewlineAddsEmptyLine) {
    Document doc;
    doc.insertText(0, u"abc\n");
    EXPECT_EQ(doc.lineCount(),     2u);
    EXPECT_EQ(doc.lineToOffset(1), 4u);
}

TEST(LineIndexTest, RebuildAfterMutation) {
    Document doc;
    doc.insertText(0, u"one\ntwo");
    EXPECT_EQ(doc.lineCount(), 2u);
    doc.insertText(doc.length(), u"\nthree");
    EXPECT_EQ(doc.lineCount(), 3u);
    EXPECT_EQ(doc.lineToOffset(2), 8u);  // "one\ntwo\n" == 8 chars
}

}  // namespace
