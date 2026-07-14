#include <gtest/gtest.h>

#include "neomifes/document/add_buffer.h"

namespace {

using neomifes::document::AddBuffer;

TEST(AddBufferTest, EmptyOnCreate) {
    AddBuffer b;
    EXPECT_EQ(b.size(),          0u);
    EXPECT_EQ(b.view(0, 0),      u"");
}

TEST(AddBufferTest, AppendReturnsOffset) {
    AddBuffer b;
    EXPECT_EQ(b.append(u"hello"), 0u);
    EXPECT_EQ(b.append(u" world"), 5u);
    EXPECT_EQ(b.size(), 11u);
    EXPECT_EQ(b.view(0, 5),  u"hello");
    EXPECT_EQ(b.view(5, 6),  u" world");
    EXPECT_EQ(b.view(0, 11), u"hello world");
}

TEST(AddBufferTest, OutOfRangeViewReturnsEmpty) {
    AddBuffer b;
    b.append(u"abc");
    EXPECT_EQ(b.view(0, 100), u"");
    EXPECT_EQ(b.view(100, 1), u"");
}

TEST(AddBufferTest, EmptyAppend) {
    AddBuffer b;
    EXPECT_EQ(b.append(u""), 0u);
    EXPECT_EQ(b.size(),      0u);
}

}  // namespace
