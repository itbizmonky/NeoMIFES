#include <gtest/gtest.h>

#include "neomifes/document/document.h"
#include "neomifes/document/text_pos.h"

namespace {

using neomifes::document::Document;
using neomifes::document::TextRange;

TEST(DocumentVersionTest, StartsAtZero) {
    const Document doc;
    EXPECT_EQ(doc.version(), 0U);
}

TEST(DocumentVersionTest, InsertTextIncrementsByOne) {
    Document doc;
    doc.insertText(0, u"hello");
    EXPECT_EQ(doc.version(), 1U);
    doc.insertText(0, u"x");
    EXPECT_EQ(doc.version(), 2U);
}

TEST(DocumentVersionTest, EraseRangeIncrementsByOne) {
    Document doc;
    doc.insertText(0, u"hello");
    const auto afterInsert = doc.version();
    doc.eraseRange(TextRange{.start = 0, .end = 1});
    EXPECT_EQ(doc.version(), afterInsert + 1);
}

TEST(DocumentVersionTest, ReplaceRangeIncrementsByOne) {
    Document doc;
    doc.insertText(0, u"hello");
    const auto afterInsert = doc.version();
    doc.replaceRange(TextRange{.start = 0, .end = 1}, u"H");
    EXPECT_EQ(doc.version(), afterInsert + 1);
}

TEST(DocumentVersionTest, ReadOnlyCallsDoNotChangeVersion) {
    Document doc;
    doc.insertText(0, u"hello\nworld");
    const auto version = doc.version();

    (void)doc.snapshot();
    (void)doc.length();
    (void)doc.lineCount();
    (void)doc.pieceCount();
    (void)doc.toU16String();
    (void)doc.offsetToLine(2);
    (void)doc.lineToOffset(1);

    EXPECT_EQ(doc.version(), version);
}

}  // namespace
