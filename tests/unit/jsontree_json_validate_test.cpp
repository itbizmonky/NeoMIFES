#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "neomifes/document/document.h"
#include "neomifes/jsontree/json_tree.h"

namespace {

using neomifes::document::Document;
using neomifes::jsontree::validateJson;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

TEST(JsonValidateTest, ValidJsonReturnsNullopt) {
    const Document doc = makeDoc(u"{\"a\":1,\"b\":[true,null,\"x\"]}");
    EXPECT_FALSE(validateJson(doc).has_value());
}

TEST(JsonValidateTest, TrailingGarbageReturnsPositionAndMessage) {
    // "{\"a\":1}" (7 chars, indices 0-6) is itself valid; the 'x' at index 7
    // is unparsed trailing content nlohmann rejects once the top-level
    // value is already complete.
    const Document doc   = makeDoc(u"{\"a\":1}x");
    const auto     error = validateJson(doc);
    ASSERT_TRUE(error.has_value());
    EXPECT_GE(error->position, 7U);
    EXPECT_LE(error->position, doc.length());
    EXPECT_FALSE(error->message.empty());
}

TEST(JsonValidateTest, MissingClosingBraceReturnsPositionAndMessage) {
    const Document doc   = makeDoc(u"{\"a\":1");
    const auto     error = validateJson(doc);
    ASSERT_TRUE(error.has_value());
    EXPECT_GT(error->position, 0U);
    EXPECT_LE(error->position, doc.length());
    EXPECT_FALSE(error->message.empty());
}

TEST(JsonValidateTest, MissingColonReturnsAnError) {
    const Document doc   = makeDoc(u"{\"a\" 1}");
    const auto     error = validateJson(doc);
    ASSERT_TRUE(error.has_value());
    EXPECT_FALSE(error->message.empty());
}

TEST(JsonValidateTest, EmptyDocumentReturnsAnError) {
    const Document doc   = makeDoc(u"");
    const auto     error = validateJson(doc);
    ASSERT_TRUE(error.has_value());
    EXPECT_FALSE(error->message.empty());
}

TEST(JsonValidateTest, NestingPastGuardThresholdReturnsFixedMessageAtPositionZeroNotCrash) {
    // Same 201-deep construction jsontree_json_tree_test.cpp's own
    // NestingPastGuardThresholdReturnsNulloptNotCrash uses - json_tree.cpp's
    // kMaxJsonNestingDepth is 200, and start_object()/start_array() are
    // never handed a position by nlohmann (see JsonSyntaxError::position's
    // own comment), so this specific rejection reason always reports 0.
    constexpr int  kDepth = 201;
    std::u16string text(static_cast<std::size_t>(kDepth), u'[');
    text.append(static_cast<std::size_t>(kDepth), u']');
    const Document doc   = makeDoc(text);
    const auto     error = validateJson(doc);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->position, 0U);
    EXPECT_FALSE(error->message.empty());
}

TEST(JsonValidateTest, NestingAtGuardThresholdStillValidates) {
    constexpr int  kDepth = 200;
    std::u16string text(static_cast<std::size_t>(kDepth), u'[');
    text.append(static_cast<std::size_t>(kDepth), u']');
    const Document doc = makeDoc(text);
    EXPECT_FALSE(validateJson(doc).has_value());
}

TEST(JsonValidateTest, BufferSnapshotAndDocumentOverloadsAgree) {
    const Document doc = makeDoc(u"{\"a\":1}");
    EXPECT_FALSE(validateJson(*doc.snapshot()).has_value());
    EXPECT_FALSE(validateJson(doc).has_value());

    const Document invalidDoc = makeDoc(u"{\"a\":1");
    EXPECT_TRUE(validateJson(*invalidDoc.snapshot()).has_value());
    EXPECT_TRUE(validateJson(invalidDoc).has_value());
}

}  // namespace
