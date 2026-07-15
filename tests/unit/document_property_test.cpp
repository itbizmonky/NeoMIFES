// Property test: exercise PieceTable with pseudo-random insert/erase/replace
// operations and cross-check the result against a plain std::u16string that
// serves as the ground-truth reference. This catches boundary bugs the
// hand-written cases miss (piece splitting on boundary, empty pieces, etc.).

#include <gtest/gtest.h>

#include <random>
#include <string>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/piece_table.h"

namespace {

using neomifes::document::PieceTable;
using neomifes::document::TextRange;

std::u16string extractAll(const PieceTable& pt) {
    auto snap = pt.snapshot();
    return snap->extract({0, snap->length()});
}

std::u16string randomAsciiSlice(std::mt19937& rng, std::size_t maxLen) {
    std::uniform_int_distribution<std::size_t> lenDist(0, maxLen);
    std::uniform_int_distribution<int>         charDist('a', 'z');
    const auto n = lenDist(rng);
    std::u16string s;
    s.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        s.push_back(static_cast<char16_t>(charDist(rng)));
    }
    // Occasionally sprinkle a newline so LineIndex counting is exercised.
    if (!s.empty() && (rng() % 4 == 0)) {
        s[rng() % s.size()] = u'\n';
    }
    return s;
}

TEST(PieceTablePropertyTest, MatchesReferenceStringUnderRandomOps) {
    // 20,000 per ADR-007's Phase 2b2 completion criteria - this is the primary
    // stress net for the tree-backed PieceTable (insert/erase/replace all
    // route through PieceTree since Phase 2b2 Step 2).
    constexpr int kIterations = 20000;
    std::mt19937 rng{0xC0FFEEu};

    PieceTable      pt;
    std::u16string  ref;

    for (int i = 0; i < kIterations; ++i) {
        const int op = static_cast<int>(rng() % 3);
        const std::size_t curLen = ref.size();
        std::uniform_int_distribution<std::size_t> posDist(0, curLen);

        switch (op) {
            case 0: {   // insert
                const auto pos = posDist(rng);
                const auto s   = randomAsciiSlice(rng, 8);
                pt.insert(pos, s);
                ref.insert(pos, s);
                break;
            }
            case 1: {   // erase (may be empty)
                if (curLen == 0) break;
                const auto a = posDist(rng);
                const auto b = posDist(rng);
                const auto lo = std::min(a, b);
                const auto hi = std::max(a, b);
                pt.erase({lo, hi});
                ref.erase(lo, hi - lo);
                break;
            }
            case 2: {   // replace
                if (curLen == 0) break;
                const auto a = posDist(rng);
                const auto b = posDist(rng);
                const auto lo = std::min(a, b);
                const auto hi = std::max(a, b);
                const auto s  = randomAsciiSlice(rng, 6);
                pt.replace({lo, hi}, s);
                ref.replace(lo, hi - lo, s);
                break;
            }
            default: break;
        }

        // Cheap invariants first (they short-circuit before the O(N) compare).
        ASSERT_EQ(pt.length(), ref.size()) << "step " << i;

        // Full content comparison every so often to keep the test bounded.
        if ((i % 100) == 99) {
            ASSERT_EQ(extractAll(pt), ref) << "step " << i;
        }
    }

    // Final full compare.
    EXPECT_EQ(extractAll(pt), ref);
}

}  // namespace
