#include <gtest/gtest.h>

#include "neomifes/core/viewport.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::core::Viewport;
using neomifes::document::Document;

TEST(ViewportTest, StartsAtLineZeroWithNoVisibleLines) {
    const Viewport viewport;
    EXPECT_EQ(viewport.topLine(), 0U);
    EXPECT_EQ(viewport.visibleLines().start, 0U);
    EXPECT_EQ(viewport.visibleLines().end, 0U);
}

TEST(ViewportTest, ScrollToSetsTopLine) {
    Viewport viewport;
    viewport.scrollTo(42);
    EXPECT_EQ(viewport.topLine(), 42U);
}

TEST(ViewportTest, VisibleLinesReflectsTopLineAndCount) {
    Viewport viewport;
    viewport.scrollTo(10);
    viewport.setVisibleLineCount(20);
    EXPECT_EQ(viewport.visibleLines().start, 10U);
    EXPECT_EQ(viewport.visibleLines().end, 30U);
}

TEST(ViewportTest, EnsureVisibleDoesNothingWhenAlreadyInWindow) {
    Document doc;
    doc.insertText(0, u"a\nb\nc\nd\ne");  // 5 lines
    Viewport viewport;
    viewport.scrollTo(1);
    viewport.setVisibleLineCount(3);  // window = [1, 4)

    viewport.ensureVisible(doc.lineToOffset(2), doc);  // line 2, inside window
    EXPECT_EQ(viewport.topLine(), 1U);
}

TEST(ViewportTest, EnsureVisibleScrollsUpWhenPositionAboveWindow) {
    Document doc;
    doc.insertText(0, u"a\nb\nc\nd\ne");
    Viewport viewport;
    viewport.scrollTo(3);
    viewport.setVisibleLineCount(2);  // window = [3, 5)

    viewport.ensureVisible(doc.lineToOffset(0), doc);  // line 0, above window
    EXPECT_EQ(viewport.topLine(), 0U);
}

TEST(ViewportTest, EnsureVisibleScrollsDownWhenPositionBelowWindow) {
    Document doc;
    doc.insertText(0, u"a\nb\nc\nd\ne");
    Viewport viewport;
    viewport.scrollTo(0);
    viewport.setVisibleLineCount(2);  // window = [0, 2)

    viewport.ensureVisible(doc.lineToOffset(4), doc);  // line 4, below window
    EXPECT_EQ(viewport.topLine(), 3U);                 // window becomes [3, 5)
}

// WI-03: horizontal counterparts to the four tests above - ensureVisible()'s
// column half mirrors the line half exactly (see viewport.cpp).

TEST(ViewportTest, ScrollToColumnSetsLeftColumn) {
    Viewport viewport;
    viewport.scrollToColumn(15);
    EXPECT_EQ(viewport.leftColumn(), 15U);
}

TEST(ViewportTest, EnsureVisibleDoesNothingHorizontallyWhenColumnAlreadyInWindow) {
    Document doc;
    doc.insertText(0, u"0123456789ABCDEFGHIJ");  // single line, 20 columns
    Viewport viewport;
    viewport.scrollToColumn(5);
    viewport.setVisibleColumnCount(10);  // window = [5, 15)

    viewport.ensureVisible(8, doc);  // column 8, inside window
    EXPECT_EQ(viewport.leftColumn(), 5U);
}

TEST(ViewportTest, EnsureVisibleScrollsLeftWhenColumnBeforeWindow) {
    Document doc;
    doc.insertText(0, u"0123456789ABCDEFGHIJ");
    Viewport viewport;
    viewport.scrollToColumn(10);
    viewport.setVisibleColumnCount(5);  // window = [10, 15)

    viewport.ensureVisible(2, doc);  // column 2, left of window
    EXPECT_EQ(viewport.leftColumn(), 2U);
}

TEST(ViewportTest, EnsureVisibleScrollsRightWhenColumnPastWindow) {
    Document doc;
    doc.insertText(0, u"0123456789ABCDEFGHIJ");
    Viewport viewport;
    viewport.scrollToColumn(0);
    viewport.setVisibleColumnCount(5);  // window = [0, 5)

    viewport.ensureVisible(18, doc);  // column 18, past window
    EXPECT_EQ(viewport.leftColumn(), 14U);  // window becomes [14, 19)
}

}  // namespace
