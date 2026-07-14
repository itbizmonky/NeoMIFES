#include <gtest/gtest.h>

#include <string>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/original_buffer.h"
#include "neomifes/document/piece_table.h"

using namespace std::literals::string_view_literals;

namespace {

using neomifes::document::OriginalBuffer;
using neomifes::document::PieceTable;
using neomifes::document::TextRange;

std::u16string extractAll(const PieceTable& pt) {
    auto snap = pt.snapshot();
    return snap->extract({0, snap->length()});
}

TEST(PieceTableTest, EmptyDocument) {
    PieceTable pt;
    EXPECT_EQ(pt.length(), 0u);
    EXPECT_EQ(pt.lineCount(), 1u);
    EXPECT_EQ(extractAll(pt), u"");
}

TEST(PieceTableTest, InsertIntoEmpty) {
    PieceTable pt;
    pt.insert(0, u"hello");
    EXPECT_EQ(pt.length(), 5u);
    EXPECT_EQ(extractAll(pt), u"hello");
}

TEST(PieceTableTest, InsertAtBeginning) {
    PieceTable pt;
    pt.insert(0, u"world");
    pt.insert(0, u"hello ");
    EXPECT_EQ(extractAll(pt), u"hello world");
}

TEST(PieceTableTest, InsertAtEndAppends) {
    PieceTable pt;
    pt.insert(0, u"foo");
    pt.insert(3, u"bar");
    EXPECT_EQ(extractAll(pt), u"foobar");
}

TEST(PieceTableTest, InsertInMiddle) {
    PieceTable pt;
    pt.insert(0, u"abcdef");
    pt.insert(3, u"XYZ");
    EXPECT_EQ(extractAll(pt), u"abcXYZdef");
}

TEST(PieceTableTest, InsertClampsBeyondEnd) {
    PieceTable pt;
    pt.insert(0, u"abc");
    pt.insert(9999, u"Z");
    EXPECT_EQ(extractAll(pt), u"abcZ");
}

TEST(PieceTableTest, EraseWholeDocument) {
    PieceTable pt;
    pt.insert(0, u"abcdef");
    pt.erase({0, 6});
    EXPECT_EQ(pt.length(), 0u);
    EXPECT_EQ(extractAll(pt), u"");
}

TEST(PieceTableTest, EraseFromMiddle) {
    PieceTable pt;
    pt.insert(0, u"abcdef");
    pt.erase({2, 4});   // remove "cd"
    EXPECT_EQ(extractAll(pt), u"abef");
}

TEST(PieceTableTest, EraseIsIdempotentOnEmptyRange) {
    PieceTable pt;
    pt.insert(0, u"abc");
    pt.erase({1, 1});
    EXPECT_EQ(extractAll(pt), u"abc");
}

TEST(PieceTableTest, ReplaceAcrossPieces) {
    PieceTable pt;
    pt.insert(0, u"abcdef");
    pt.insert(3, u"XYZ");                     // "abcXYZdef"
    pt.replace({2, 7}, u"1234");              // remove "cXYZd", insert "1234"
    EXPECT_EQ(extractAll(pt), u"ab1234ef");
}

TEST(PieceTableTest, OriginalBufferSeed) {
    auto orig = OriginalBuffer::fromU16String(u"hello");
    PieceTable pt(orig);
    EXPECT_EQ(extractAll(pt), u"hello");
    pt.insert(5, u", world");
    EXPECT_EQ(extractAll(pt), u"hello, world");
}

TEST(PieceTableTest, NewlineCountTracked) {
    PieceTable pt;
    pt.insert(0, u"line1\nline2\nline3");
    EXPECT_EQ(pt.newlineCount(), 2u);
    EXPECT_EQ(pt.lineCount(),    3u);
    pt.insert(pt.length(), u"\n");
    EXPECT_EQ(pt.newlineCount(), 3u);
}

TEST(PieceTableTest, SnapshotDecouplesFromMutation) {
    PieceTable pt;
    pt.insert(0, u"hello");
    auto snap = pt.snapshot();
    pt.insert(5, u", world");           // mutate after snapshotting
    EXPECT_EQ(snap->length(), 5u);
    EXPECT_EQ(snap->extract({0, 5}), u"hello");
    EXPECT_EQ(pt.length(), 12u);
}

TEST(PieceTableTest, SurrogatePairsRoundtrip) {
    PieceTable pt;
    // U+1F600 == D83D DE00, contributes 2 UTF-16 CUs.
    const std::u16string face = u"\xD83D\xDE00";
    pt.insert(0, face);
    EXPECT_EQ(pt.length(), 2u);
    EXPECT_EQ(extractAll(pt), face);
}

}  // namespace
