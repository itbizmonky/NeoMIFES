// Focused coverage for BufferSnapshot::pieceView - the Phase 2b primitive
// that lets O(N) traversal (LineIndex, future SearchEngine) avoid the O(N)
// re-walk that extract() performs.

#include <gtest/gtest.h>

#include <string>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/original_buffer.h"
#include "neomifes/document/piece_table.h"

namespace {

using neomifes::document::BufferSnapshot;
using neomifes::document::OriginalBuffer;
using neomifes::document::PieceSource;
using neomifes::document::PieceTable;

TEST(BufferSnapshotPieceViewTest, EmptyDocumentHasNoPieces) {
    PieceTable pt;
    auto snap = pt.snapshot();
    EXPECT_TRUE(snap->pieces().empty());
}

TEST(BufferSnapshotPieceViewTest, ReturnsCorrectViewForAddPiece) {
    PieceTable pt;
    pt.insert(0, u"hello world");
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);
    EXPECT_EQ(snap->pieces()[0].source, PieceSource::Add);
    EXPECT_EQ(snap->pieceView(snap->pieces()[0]), u"hello world");
}

TEST(BufferSnapshotPieceViewTest, ReturnsCorrectViewForOriginalPiece) {
    auto orig = OriginalBuffer::fromU16String(u"NeoMIFES");
    PieceTable pt(orig);
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);
    EXPECT_EQ(snap->pieces()[0].source, PieceSource::Original);
    EXPECT_EQ(snap->pieceView(snap->pieces()[0]), u"NeoMIFES");
}

TEST(BufferSnapshotPieceViewTest, SplitPiecesEachReturnCorrectSubview) {
    auto orig = OriginalBuffer::fromU16String(u"abcdef");
    PieceTable pt(orig);
    // Insert into the middle so the Original piece is split around the insert.
    pt.insert(3, u"XYZ");   // "abcXYZdef"
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 3u);

    // Concatenating each pieceView() must reconstruct the document.
    std::u16string reconstructed;
    for (const auto& p : snap->pieces()) {
        reconstructed.append(snap->pieceView(p));
    }
    EXPECT_EQ(reconstructed, u"abcXYZdef");

    // Individual pieces have the expected sub-slices.
    EXPECT_EQ(snap->pieceView(snap->pieces()[0]), u"abc");
    EXPECT_EQ(snap->pieceView(snap->pieces()[1]), u"XYZ");
    EXPECT_EQ(snap->pieceView(snap->pieces()[2]), u"def");
}

TEST(BufferSnapshotPieceViewTest, LargeAddPieceStaysContiguous) {
    // Even when AddBuffer opens fresh chunks, each piece is a single append
    // so pieceView() must always return a contiguous view.
    PieceTable pt;
    const std::u16string big(200'000, u'q');   // > kDefaultChunkCUs
    pt.insert(0, big);
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);
    const auto view = snap->pieceView(snap->pieces()[0]);
    EXPECT_EQ(view.size(), big.size());
    EXPECT_EQ(view.front(), u'q');
    EXPECT_EQ(view.back(),  u'q');
}

}  // namespace
