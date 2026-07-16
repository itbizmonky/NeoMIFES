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

}  // namespace
