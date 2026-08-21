#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "neomifes/document/document.h"
#include "neomifes/jsontree/json_format.h"
#include "neomifes/jsontree/json_tree.h"

namespace {

using neomifes::document::Document;
using neomifes::jsontree::formatJsonNode;
using neomifes::jsontree::JsonNode;
using neomifes::jsontree::parseJsonTree;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

[[nodiscard]] std::u16string formatSource(std::u16string_view source) {
    const Document doc  = makeDoc(source);
    const auto      tree = parseJsonTree(doc);
    return tree.has_value() ? formatJsonNode(*tree) : u"<parse failed>";
}

TEST(JsonFormatTest, NestedObjectArrayFormatsWithIndentAndPreservesKeyOrder) {
    const std::u16string result = formatSource(u"{\"b\":1,\"a\":[true,null]}");
    EXPECT_EQ(result, u"{\n  \"b\": 1,\n  \"a\": [\n    true,\n    null\n  ]\n}");
}

TEST(JsonFormatTest, NumberLeafPreservesOriginalSpelling) {
    const std::u16string result = formatSource(u"{\"x\":1.50}");
    EXPECT_EQ(result, u"{\n  \"x\": 1.50\n}");
}

TEST(JsonFormatTest, KeyRequiringEscapeIsReEncoded) {
    // Source key is (after JSON-decoding the \" escape) the 3-character
    // string a"b - re-formatting must re-escape the embedded quote, not
    // emit a syntactically broken "a"b".
    const std::u16string result = formatSource(u"{\"a\\\"b\":1}");
    EXPECT_EQ(result, u"{\n  \"a\\\"b\": 1\n}");
}

TEST(JsonFormatTest, ControlCharacterInKeyIsEscaped) {
    // Source key decodes (via \t) to a literal tab character.
    const std::u16string result = formatSource(u"{\"a\\tb\":1}");
    EXPECT_EQ(result, u"{\n  \"a\\tb\": 1\n}");
}

TEST(JsonFormatTest, EmptyObjectAndArrayFormatWithoutNewlines) {
    const std::u16string result = formatSource(u"{\"a\":{},\"b\":[]}");
    EXPECT_EQ(result, u"{\n  \"a\": {},\n  \"b\": []\n}");
}

TEST(JsonFormatTest, BareScalarRootFormatsAsItself) {
    EXPECT_EQ(formatSource(u"42"), u"42");
    EXPECT_EQ(formatSource(u"\"hello\""), u"\"hello\"");
}

TEST(JsonFormatTest, ReformattingAlreadyFormattedOutputIsIdempotent) {
    const std::u16string once  = formatSource(u"{\"b\":1,\"a\":[true,null]}");
    const Document        doc   = makeDoc(once);
    const auto             tree  = parseJsonTree(doc);
    ASSERT_TRUE(tree.has_value());
    EXPECT_EQ(formatJsonNode(*tree), once);
}

TEST(JsonFormatTest, CustomIndentWidthIsHonored) {
    const Document doc  = makeDoc(u"{\"a\":1}");
    const auto      tree = parseJsonTree(doc);
    ASSERT_TRUE(tree.has_value());
    EXPECT_EQ(formatJsonNode(*tree, /*indentWidth=*/4), u"{\n    \"a\": 1\n}");
}

}  // namespace
