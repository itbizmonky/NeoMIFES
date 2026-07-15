// Unit tests for PieceTree - insert / splitPieceAt / eraseRange /
// collectPieces / validate / pieceContainingStrictly. The RB invariants and
// aggregate consistency are exercised via validate(), which is called after
// every mutation in the stress tests.
//
// Step 1 (insert/split) tests are above the "Erase" section marker; Step 2
// (erase, CLRS 13.4) tests follow it.

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "neomifes/document/piece.h"
#include "neomifes/document/piece_tree.h"

namespace {

using neomifes::document::Piece;
using neomifes::document::PieceSource;
using neomifes::document::PieceTree;
using neomifes::document::TextRange;

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

// =============================================================================
// pieceContainingStrictly
// =============================================================================

TEST(PieceTreeTest, PieceContainingStrictlyReturnsNulloptOnBoundaries) {
    PieceTree t;
    t.insertAt(0, addPiece(0, 5));
    t.insertAt(5, addPiece(100, 5));   // pieces: [0,5) [5,10)

    EXPECT_FALSE(t.pieceContainingStrictly(0).has_value());   // start of doc
    EXPECT_FALSE(t.pieceContainingStrictly(5).has_value());   // exact piece boundary
    EXPECT_FALSE(t.pieceContainingStrictly(10).has_value());  // end of doc
}

TEST(PieceTreeTest, PieceContainingStrictlyFindsInteriorPosition) {
    PieceTree t;
    t.insertAt(0, addPiece(50, 8));   // single piece [0,8)

    auto lookup = t.pieceContainingStrictly(3);
    ASSERT_TRUE(lookup.has_value());
    EXPECT_EQ(lookup->pieceStart, 0u);
    EXPECT_EQ(lookup->piece.offset, 50u);
    EXPECT_EQ(lookup->piece.length, 8u);
}

TEST(PieceTreeTest, PieceContainingStrictlyOnEmptyTree) {
    PieceTree t;
    EXPECT_FALSE(t.pieceContainingStrictly(0).has_value());
    EXPECT_FALSE(t.pieceContainingStrictly(5).has_value());
}

// =============================================================================
// eraseRange (Phase 2b2 Step 2 - CLRS 13.4 RB delete)
// =============================================================================

TEST(PieceTreeTest, EraseSingleRootLeavesEmptyTree) {
    PieceTree t;
    t.insertAt(0, addPiece(0, 5));
    t.eraseRange({0, 5});

    EXPECT_TRUE(t.empty());
    EXPECT_EQ(t.pieceCount(),  0u);
    EXPECT_EQ(t.totalLength(), 0u);
    EXPECT_TRUE(t.validate());
}

TEST(PieceTreeTest, EraseOneOfManyAppendedPieces) {
    PieceTree t;
    for (int i = 0; i < 10; ++i) {
        t.insertAt(t.totalLength(), addPiece(static_cast<std::uint64_t>(i) * 5, 5));
    }
    ASSERT_TRUE(t.validate());

    // Remove the 3rd piece (logical range [15,20)).
    t.eraseRange({15, 20});
    ASSERT_TRUE(t.validate());

    auto pieces = t.collectPieces();
    ASSERT_EQ(pieces.size(), 9u);
    EXPECT_EQ(t.totalLength(), 45u);
    // The piece that was at index 3 (offset 15) should be gone; index 3 in
    // the result is now what used to be index 4 (offset 20).
    EXPECT_EQ(pieces[3].offset, 20u);
}

TEST(PieceTreeTest, EraseRangeSpanningMultiplePieces) {
    PieceTree t;
    for (int i = 0; i < 10; ++i) {
        t.insertAt(t.totalLength(), addPiece(static_cast<std::uint64_t>(i) * 5, 5));
    }
    ASSERT_TRUE(t.validate());

    // Remove pieces covering [10, 30) - i.e. pieces at original offsets 10,15,20,25.
    t.eraseRange({10, 30});
    ASSERT_TRUE(t.validate());

    auto pieces = t.collectPieces();
    ASSERT_EQ(pieces.size(), 6u);
    EXPECT_EQ(t.totalLength(), 30u);
    EXPECT_EQ(pieces[0].offset, 0u);
    EXPECT_EQ(pieces[1].offset, 5u);
    EXPECT_EQ(pieces[2].offset, 30u);  // first surviving piece after the gap
}

TEST(PieceTreeTest, EraseEntireDocumentAcrossManyPieces) {
    PieceTree t;
    for (int i = 0; i < 50; ++i) {
        t.insertAt(t.totalLength(), addPiece(static_cast<std::uint64_t>(i), 1));
    }
    ASSERT_TRUE(t.validate());

    t.eraseRange({0, t.totalLength()});

    EXPECT_TRUE(t.empty());
    EXPECT_EQ(t.pieceCount(), 0u);
    EXPECT_TRUE(t.validate());
}

TEST(PieceTreeTest, EraseNewlineAggregateUpdatesCorrectly) {
    PieceTree t;
    t.insertAt(0, addPiece(0, 5, 2));    // 2 newlines
    t.insertAt(5, addPiece(5, 5, 3));    // 3 newlines
    t.insertAt(10, addPiece(10, 5, 1));  // 1 newline
    ASSERT_EQ(t.totalNewlines(), 6u);

    t.eraseRange({5, 10});  // remove the middle (3-newline) piece
    ASSERT_TRUE(t.validate());
    EXPECT_EQ(t.totalNewlines(), 3u);
    EXPECT_EQ(t.totalLength(),   10u);
}

TEST(PieceTreeTest, EraseEmptyRangeIsNoOp) {
    PieceTree t;
    t.insertAt(0, addPiece(0, 5));
    t.eraseRange({2, 2});
    EXPECT_EQ(t.pieceCount(),  1u);
    EXPECT_EQ(t.totalLength(), 5u);
    EXPECT_TRUE(t.validate());
}

TEST(PieceTreeTest, EraseRangeClampsToTotalLength) {
    PieceTree t;
    t.insertAt(0, addPiece(0, 5));
    t.eraseRange({2, 9999});
    EXPECT_EQ(t.totalLength(), 2u);
    EXPECT_TRUE(t.validate());
}

// Repeatedly delete the root/whatever node happens to be there until the
// tree drains completely - exercises both the one-child and two-child
// deletion branches of eraseNode across a shrinking, rebalancing tree.
TEST(PieceTreeTest, RepeatedRootDeletionDrainsTreeCleanly) {
    PieceTree t;
    constexpr int kN = 300;
    for (int i = 0; i < kN; ++i) {
        t.insertAt(t.totalLength(), addPiece(static_cast<std::uint64_t>(i), 1));
    }
    ASSERT_TRUE(t.validate());

    for (int i = 0; i < kN; ++i) {
        ASSERT_GT(t.totalLength(), 0u) << "drained early at i=" << i;
        t.eraseRange({0, 1});
        ASSERT_TRUE(t.validate()) << "invariant broke after deletion #" << i;
    }
    EXPECT_TRUE(t.empty());
}

// Delete every other piece (exercises deletions that leave gaps, forcing
// many different node shapes to be removed: leaves, one-child, two-child).
// Pieces are tagged via `offset` so survivors can be identified after the
// pass (odd-indexed original pieces should remain).
TEST(PieceTreeTest, AlternatingDeletionPattern) {
    PieceTree t;
    constexpr int kN = 200;
    for (int i = 0; i < kN; ++i) {
        // offset doubles as an identity tag: original piece #i has offset i*10.
        t.insertAt(t.totalLength(), addPiece(static_cast<std::uint64_t>(i) * 10, 1));
    }
    ASSERT_TRUE(t.validate());

    // Walk once, deleting every even-indexed piece (0, 2, 4, ...). Since
    // deleting shifts later pieces down by one index, re-fetch the piece
    // list after each deletion rather than trying to track positions.
    int deletionsDone = 0;
    while (deletionsDone < kN / 2) {
        auto pieces = t.collectPieces();
        const std::size_t targetIndex = static_cast<std::size_t>(deletionsDone);
        ASSERT_LT(targetIndex, pieces.size());

        std::uint64_t startOffset = 0;
        for (std::size_t i = 0; i < targetIndex; ++i) {
            startOffset += pieces[i].length;
        }
        t.eraseRange({startOffset, startOffset + pieces[targetIndex].length});
        ASSERT_TRUE(t.validate()) << "invariant broke at deletion " << deletionsDone;
        ++deletionsDone;
    }

    // Survivors should be exactly the odd-indexed original pieces, in order:
    // offsets 10, 30, 50, ... (original index 1, 3, 5, ...).
    auto finalPieces = t.collectPieces();
    ASSERT_EQ(finalPieces.size(), static_cast<std::size_t>(kN / 2));
    for (std::size_t i = 0; i < finalPieces.size(); ++i) {
        const std::uint64_t expectedOriginalIndex = (2 * i) + 1;
        EXPECT_EQ(finalPieces[i].offset, expectedOriginalIndex * 10) << "index " << i;
    }
}

// -----------------------------------------------------------------------------
// Randomized stress: insert / split / erase mixed, validate() after every
// step. This is the primary correctness net for the RB-delete implementation
// given the environment cannot run a local compiler build.
// -----------------------------------------------------------------------------

TEST(PieceTreeTest, StressRandomInsertSplitErase) {
    PieceTree t;
    std::mt19937 rng{0x5EED5EEDu};

    // Seed with a moderate amount of content.
    t.insertAt(0, addPiece(0, 500, 5));
    ASSERT_TRUE(t.validate());

    for (int step = 0; step < 3000; ++step) {
        const auto total = t.totalLength();
        if (total == 0) {
            // Tree drained - reseed so subsequent ops have something to work with.
            t.insertAt(0, addPiece(0, 1 + (rng() % 50)));
            ASSERT_TRUE(t.validate()) << "reseed at step " << step;
            continue;
        }

        std::uniform_int_distribution<int> opDist(0, 3);
        const int op = opDist(rng);

        if (op == 0) {
            // Insert at a random boundary (0 or total - always valid boundaries).
            const bool atStart = (rng() % 2) == 0;
            t.insertAt(atStart ? 0 : total, addPiece(0, 1 + (rng() % 8)));
        } else if (op == 1) {
            // Split at a random interior position of a piece longer than 1.
            auto pieces = t.collectPieces();
            std::vector<std::size_t> candidates;
            for (std::size_t i = 0; i < pieces.size(); ++i) {
                if (pieces[i].length > 1) candidates.push_back(i);
            }
            if (candidates.empty()) continue;
            const auto pi = candidates[rng() % candidates.size()];
            std::uint64_t startOffset = 0;
            for (std::size_t i = 0; i < pi; ++i) startOffset += pieces[i].length;
            const std::uint64_t within = 1u + (rng() % (pieces[pi].length - 1));
            t.splitPieceAt(startOffset + within, 0);
        } else {
            // Erase a small boundary-aligned range: pick a random piece,
            // erase exactly that piece (guaranteed boundary-aligned).
            auto pieces = t.collectPieces();
            if (pieces.empty()) continue;
            const auto pi = rng() % pieces.size();
            std::uint64_t startOffset = 0;
            for (std::size_t i = 0; i < pi; ++i) startOffset += pieces[i].length;
            t.eraseRange({startOffset, startOffset + pieces[pi].length});
        }

        ASSERT_TRUE(t.validate()) << "invariant broke at step " << step;
    }
}

// Cross-check against a reference std::vector<Piece> model under the same
// mixed operations, verifying not just RB invariants but that eraseRange
// actually removes the CORRECT content (not just "some" valid content).
TEST(PieceTreeTest, StressMatchesReferenceModel) {
    PieceTree t;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ref;  // (offset, length) pairs, in order

    std::mt19937 rng{0xABCDEFu};

    auto totalRefLength = [&ref]() {
        std::uint64_t sum = 0;
        for (auto& [o, l] : ref) sum += l;
        return sum;
    };

    for (int step = 0; step < 1500; ++step) {
        const auto total = totalRefLength();
        std::uniform_int_distribution<int> opDist(0, 1);
        const int op = (total == 0) ? 0 : opDist(rng);

        if (op == 0) {
            // Append a new piece.
            const std::uint64_t len = 1 + (rng() % 6);
            const std::uint64_t fakeOffset = static_cast<std::uint64_t>(step) * 100;
            t.insertAt(t.totalLength(), addPiece(fakeOffset, len));
            ref.emplace_back(fakeOffset, len);
        } else {
            // Erase a random whole piece from the reference (boundary-aligned
            // by construction, since ref entries map 1:1 to tree pieces as
            // long as we never split in this particular stress variant).
            const auto idx = rng() % ref.size();
            std::uint64_t startOffset = 0;
            for (std::size_t i = 0; i < idx; ++i) startOffset += ref[i].second;
            t.eraseRange({startOffset, startOffset + ref[idx].second});
            ref.erase(ref.begin() + static_cast<std::ptrdiff_t>(idx));
        }

        ASSERT_TRUE(t.validate()) << "invariant broke at step " << step;

        auto pieces = t.collectPieces();
        ASSERT_EQ(pieces.size(), ref.size()) << "step " << step;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            EXPECT_EQ(pieces[i].offset, ref[i].first)  << "step " << step << " idx " << i;
            EXPECT_EQ(pieces[i].length, ref[i].second) << "step " << step << " idx " << i;
        }
    }
}

}  // namespace
