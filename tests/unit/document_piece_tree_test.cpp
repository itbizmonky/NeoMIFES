// Unit tests for the Phase 2b2 Step 1 PieceTree - insert / splitPieceAt /
// collectPieces / validate. The RB invariants and aggregate consistency are
// exercised via validate(), which is called after every mutation in the
// stress tests.

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "neomifes/document/piece.h"
#include "neomifes/document/piece_tree.h"

namespace {

using neomifes::document::Piece;
using neomifes::document::PieceSource;
using neomifes::document::PieceTree;

Piece addPiece(std::uint64_t offset, std::uint64_t length,
               std::uint32_t newlines = 0) noexcept {
    Piece p{};
    p.source       = PieceSource::Add;
    p.offset       = offset;
    p.length       = length;
    p.newlineCount = newlines;
    return p;
}

// -----------------------------------------------------------------------------
// Empty tree
// -----------------------------------------------------------------------------

TEST(PieceTreeTest, EmptyOnConstruct) {
    PieceTree t;
    EXPECT_TRUE(t.empty());
    EXPECT_EQ(t.totalLength(),   0u);
    EXPECT_EQ(t.totalNewlines(), 0u);
    EXPECT_EQ(t.pieceCount(),    0u);
    EXPECT_TRUE(t.collectPieces().empty());
    EXPECT_TRUE(t.validate());
}

// -----------------------------------------------------------------------------
// Single insert
// -----------------------------------------------------------------------------

TEST(PieceTreeTest, SingleInsertBecomesBlackRoot) {
    PieceTree t;
    t.insertAt(0, addPiece(0, 5));

    EXPECT_FALSE(t.empty());
    EXPECT_EQ(t.pieceCount(),  1u);
    EXPECT_EQ(t.totalLength(), 5u);

    auto pieces = t.collectPieces();
    ASSERT_EQ(pieces.size(), 1u);
    EXPECT_EQ(pieces[0].offset, 0u);
    EXPECT_EQ(pieces[0].length, 5u);
    EXPECT_TRUE(t.validate());
}

// -----------------------------------------------------------------------------
// Append many (all inserts at totalLength()) - stress right-leaning insertion
// -----------------------------------------------------------------------------

TEST(PieceTreeTest, AppendManyKeepsBalance) {
    PieceTree t;
    constexpr int kN = 500;
    for (int i = 0; i < kN; ++i) {
        t.insertAt(t.totalLength(), addPiece(static_cast<std::uint64_t>(i) * 3, 3));
        ASSERT_TRUE(t.validate()) << "after insert #" << i;
    }
    EXPECT_EQ(t.pieceCount(),  static_cast<std::size_t>(kN));
    EXPECT_EQ(t.totalLength(), static_cast<std::uint64_t>(kN) * 3);

    // Order preserved.
    auto pieces = t.collectPieces();
    ASSERT_EQ(pieces.size(), static_cast<std::size_t>(kN));
    for (int i = 0; i < kN; ++i) {
        EXPECT_EQ(pieces[i].offset, static_cast<std::uint64_t>(i) * 3) << "index " << i;
    }
}

// -----------------------------------------------------------------------------
// Prepend many (all inserts at position 0) - stress left-leaning insertion
// -----------------------------------------------------------------------------

TEST(PieceTreeTest, PrependManyKeepsBalance) {
    PieceTree t;
    constexpr int kN = 500;
    for (int i = 0; i < kN; ++i) {
        t.insertAt(0, addPiece(static_cast<std::uint64_t>(i) * 3, 3));
        ASSERT_TRUE(t.validate()) << "after insert #" << i;
    }
    EXPECT_EQ(t.pieceCount(),  static_cast<std::size_t>(kN));
    EXPECT_EQ(t.totalLength(), static_cast<std::uint64_t>(kN) * 3);

    // In-order should list pieces in reverse insertion order.
    auto pieces = t.collectPieces();
    ASSERT_EQ(pieces.size(), static_cast<std::size_t>(kN));
    for (int i = 0; i < kN; ++i) {
        const std::uint64_t expectedOffset =
            static_cast<std::uint64_t>(kN - 1 - i) * 3;
        EXPECT_EQ(pieces[i].offset, expectedOffset) << "index " << i;
    }
}

// -----------------------------------------------------------------------------
// Alternating prepend / append - exercises both rotations
// -----------------------------------------------------------------------------

TEST(PieceTreeTest, AlternatingInsertions) {
    PieceTree t;
    for (int i = 0; i < 200; ++i) {
        if (i % 2 == 0) {
            t.insertAt(t.totalLength(), addPiece(static_cast<std::uint64_t>(i), 1));
        } else {
            t.insertAt(0, addPiece(static_cast<std::uint64_t>(i), 1));
        }
        ASSERT_TRUE(t.validate()) << "after insert #" << i;
    }
    EXPECT_EQ(t.pieceCount(),  200u);
    EXPECT_EQ(t.totalLength(), 200u);
}

// -----------------------------------------------------------------------------
// Newline aggregate
// -----------------------------------------------------------------------------

TEST(PieceTreeTest, NewlineAggregateAccumulates) {
    PieceTree t;
    t.insertAt(t.totalLength(), addPiece(0, 3, 1));   // "a\nb" say
    t.insertAt(t.totalLength(), addPiece(3, 5, 2));   // 2 newlines
    t.insertAt(t.totalLength(), addPiece(8, 4, 0));   // 0

    EXPECT_EQ(t.totalNewlines(), 3u);
    EXPECT_EQ(t.pieceCount(),    3u);
    EXPECT_EQ(t.totalLength(),   12u);
    EXPECT_TRUE(t.validate());
}

// -----------------------------------------------------------------------------
// splitPieceAt basics
// -----------------------------------------------------------------------------

TEST(PieceTreeTest, SplitPieceAtMiddle) {
    PieceTree t;
    // One piece of length 10 with 3 newlines.
    t.insertAt(0, addPiece(100, 10, 3));

    // Split at logical position 4 - leftNewlines = 1.
    t.splitPieceAt(4, 1);

    EXPECT_EQ(t.pieceCount(),  2u);
    EXPECT_EQ(t.totalLength(), 10u);
    EXPECT_EQ(t.totalNewlines(), 3u);
    EXPECT_TRUE(t.validate());

    auto pieces = t.collectPieces();
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_EQ(pieces[0].source,       PieceSource::Add);
    EXPECT_EQ(pieces[0].offset,       100u);
    EXPECT_EQ(pieces[0].length,       4u);
    EXPECT_EQ(pieces[0].newlineCount, 1u);
    EXPECT_EQ(pieces[1].source,       PieceSource::Add);
    EXPECT_EQ(pieces[1].offset,       104u);
    EXPECT_EQ(pieces[1].length,       6u);
    EXPECT_EQ(pieces[1].newlineCount, 2u);
}

TEST(PieceTreeTest, SplitAtDifferentPositions) {
    PieceTree t;
    t.insertAt(0, addPiece(0, 20, 4));

    t.splitPieceAt(5,  1);   // 5 + 15
    ASSERT_TRUE(t.validate());
    t.splitPieceAt(15, 2);   // splits second piece (was 15 long) at offset 10 → 10 + 5
    ASSERT_TRUE(t.validate());

    auto pieces = t.collectPieces();
    ASSERT_EQ(pieces.size(), 3u);
    EXPECT_EQ(pieces[0].length, 5u);
    EXPECT_EQ(pieces[1].length, 10u);
    EXPECT_EQ(pieces[2].length, 5u);
    EXPECT_EQ(pieces[0].newlineCount + pieces[1].newlineCount + pieces[2].newlineCount, 4u);
    EXPECT_EQ(t.totalLength(), 20u);
}

// -----------------------------------------------------------------------------
// Randomized stress: insert at random boundaries + occasional splits.
// Validates after every step so an invariant break stops on the first offender.
// -----------------------------------------------------------------------------

TEST(PieceTreeTest, StressRandomInsertsAndSplits) {
    PieceTree t;
    std::mt19937 rng{0xBEEF1234u};

    // Seed with a single big piece so splits have room to work.
    t.insertAt(0, addPiece(0, 1000, 10));
    ASSERT_TRUE(t.validate());

    for (int step = 0; step < 800; ++step) {
        const auto total = t.totalLength();
        std::uniform_int_distribution<int> opDist(0, 2);
        const int op = opDist(rng);

        if (op == 0) {
            // Insert at position 0 (always a boundary).
            t.insertAt(0, addPiece(0, 1 + (rng() % 8)));
        } else if (op == 1) {
            // Append.
            t.insertAt(total, addPiece(0, 1 + (rng() % 8)));
        } else {
            // Split at a random inside-a-piece position. To ensure it's
            // strictly inside, aim for a random offset within any piece
            // longer than 1 CU.
            auto pieces = t.collectPieces();
            std::vector<std::size_t> candidates;
            for (std::size_t i = 0; i < pieces.size(); ++i) {
                if (pieces[i].length > 1) {
                    candidates.push_back(i);
                }
            }
            if (candidates.empty()) continue;
            const auto pi = candidates[rng() % candidates.size()];
            std::uint64_t startOffset = 0;
            for (std::size_t i = 0; i < pi; ++i) startOffset += pieces[i].length;
            const std::uint64_t within = 1u + (rng() % (pieces[pi].length - 1));
            t.splitPieceAt(startOffset + within, 0);   // leftNewlines = 0 for simplicity
        }

        ASSERT_TRUE(t.validate()) << "invariant broke at step " << step;
    }
}

// -----------------------------------------------------------------------------
// Move semantics
// -----------------------------------------------------------------------------

TEST(PieceTreeTest, MoveConstructorTransfersOwnership) {
    PieceTree a;
    for (int i = 0; i < 50; ++i) {
        a.insertAt(a.totalLength(), addPiece(0, 3));
    }
    ASSERT_TRUE(a.validate());
    const auto originalCount  = a.pieceCount();
    const auto originalLength = a.totalLength();

    PieceTree b = std::move(a);
    EXPECT_TRUE(a.empty());
    EXPECT_EQ(b.pieceCount(),  originalCount);
    EXPECT_EQ(b.totalLength(), originalLength);
    EXPECT_TRUE(b.validate());
}

// -----------------------------------------------------------------------------
// Insertion order preserved (in-order iteration reflects logical sequence)
// -----------------------------------------------------------------------------

TEST(PieceTreeTest, InsertionOrderPreservedAcrossManyOps) {
    PieceTree t;
    // Reference list mirrors the operations.
    std::vector<std::uint64_t> refOffsets;

    // Append 100.
    for (int i = 0; i < 100; ++i) {
        t.insertAt(t.totalLength(), addPiece(static_cast<std::uint64_t>(i), 1));
        refOffsets.push_back(static_cast<std::uint64_t>(i));
    }
    // Prepend 50.
    for (int i = 0; i < 50; ++i) {
        const std::uint64_t off = 1000ULL + i;
        t.insertAt(0, addPiece(off, 1));
        refOffsets.insert(refOffsets.begin(), off);
    }
    // Insert at boundary in the middle - use position 60 (guaranteed boundary
    // because every piece has length 1).
    t.insertAt(60, addPiece(9999, 1));
    refOffsets.insert(refOffsets.begin() + 60, 9999);

    ASSERT_TRUE(t.validate());

    auto pieces = t.collectPieces();
    ASSERT_EQ(pieces.size(), refOffsets.size());
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        EXPECT_EQ(pieces[i].offset, refOffsets[i]) << "index " << i;
    }
}

// -----------------------------------------------------------------------------
// Boundary-check: pos > totalLength is clamped to append
// -----------------------------------------------------------------------------

TEST(PieceTreeTest, InsertPastEndIsClamped) {
    PieceTree t;
    t.insertAt(0, addPiece(0, 5));
    t.insertAt(9999, addPiece(100, 3));

    auto pieces = t.collectPieces();
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_EQ(pieces[0].offset, 0u);
    EXPECT_EQ(pieces[1].offset, 100u);
    EXPECT_TRUE(t.validate());
}

}  // namespace
