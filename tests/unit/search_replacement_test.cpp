#include <gtest/gtest.h>

#include <string>

#include "neomifes/document/document.h"
#include "neomifes/search/replacement.h"
#include "neomifes/search/search_service.h"

namespace {

using neomifes::document::Document;
using neomifes::document::TextRange;
using neomifes::search::expandReplacementTemplate;
using neomifes::search::Match;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

TEST(ExpandReplacementTemplateTest, DollarZeroAndDollarAmpBothExpandToWholeMatch) {
    const Document doc   = makeDoc(u"foobar");
    const Match    match{.range = TextRange{.start = 0, .end = 6}, .groups = {}};

    EXPECT_EQ(expandReplacementTemplate(u"[$0]", doc, match), u"[foobar]");
    EXPECT_EQ(expandReplacementTemplate(u"[$&]", doc, match), u"[foobar]");
}

TEST(ExpandReplacementTemplateTest, DollarOneThroughNineExpandToCaptureGroups) {
    const Document doc = makeDoc(u"foobar");
    const Match    match{.range  = TextRange{.start = 0, .end = 6},
                       .groups = {TextRange{.start = 0, .end = 3}, TextRange{.start = 3, .end = 6}}};

    EXPECT_EQ(expandReplacementTemplate(u"$2-$1", doc, match), u"bar-foo");
}

TEST(ExpandReplacementTemplateTest, DollarDollarExpandsToLiteralDollarSign) {
    const Document doc   = makeDoc(u"x");
    const Match    match{.range = TextRange{.start = 0, .end = 1}, .groups = {}};

    EXPECT_EQ(expandReplacementTemplate(u"$$100", doc, match), u"$100");
}

TEST(ExpandReplacementTemplateTest, OutOfRangeGroupReferenceIsLeftAsLiteralText) {
    const Document doc   = makeDoc(u"x");
    const Match    match{.range = TextRange{.start = 0, .end = 1}, .groups = {}};  // no groups at all

    EXPECT_EQ(expandReplacementTemplate(u"[$5]", doc, match), u"[$5]");
}

TEST(ExpandReplacementTemplateTest, NonParticipatingGroupExpandsToEmptyString) {
    const Document doc = makeDoc(u"b");
    // "(a)|(b)" matching "b": group 1 empty-at-match-start (non-participating),
    // group 2 is "b" itself.
    const Match match{.range  = TextRange{.start = 0, .end = 1},
                      .groups = {TextRange{.start = 0, .end = 0}, TextRange{.start = 0, .end = 1}}};

    EXPECT_EQ(expandReplacementTemplate(u"[$1][$2]", doc, match), u"[][b]");
}

TEST(ExpandReplacementTemplateTest, EmptyTemplateProducesEmptyString) {
    const Document doc   = makeDoc(u"x");
    const Match    match{.range = TextRange{.start = 0, .end = 1}, .groups = {}};

    EXPECT_EQ(expandReplacementTemplate(u"", doc, match), u"");
}

TEST(ExpandReplacementTemplateTest, TemplateWithNoDollarSignsIsUnchanged) {
    const Document doc   = makeDoc(u"x");
    const Match    match{.range = TextRange{.start = 0, .end = 1}, .groups = {}};

    EXPECT_EQ(expandReplacementTemplate(u"plain text", doc, match), u"plain text");
}

TEST(ExpandReplacementTemplateTest, TrailingDollarSignAtEndOfTemplateIsLeftLiteral) {
    const Document doc   = makeDoc(u"x");
    const Match    match{.range = TextRange{.start = 0, .end = 1}, .groups = {}};

    EXPECT_EQ(expandReplacementTemplate(u"abc$", doc, match), u"abc$");
}

TEST(ExpandReplacementTemplateTest, UnrecognizedEscapeLeavesDollarSignLiteral) {
    const Document doc   = makeDoc(u"x");
    const Match    match{.range = TextRange{.start = 0, .end = 1}, .groups = {}};

    EXPECT_EQ(expandReplacementTemplate(u"$x", doc, match), u"$x");
}

}  // namespace
