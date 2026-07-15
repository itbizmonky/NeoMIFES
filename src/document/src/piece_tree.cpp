#include "neomifes/document/piece_tree.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace neomifes::document {

// ---------------------------------------------------------------------------
// PieceTreeNode
// ---------------------------------------------------------------------------

PieceTreeNode::PieceTreeNode(const Piece& p) noexcept
    : piece(p),
      subtreeLength(p.length),
      subtreeNewlines(p.newlineCount),
      subtreeCount(1) {}

// ---------------------------------------------------------------------------
// PieceTree - internal helpers
// ---------------------------------------------------------------------------

std::unique_ptr<PieceTreeNode>& PieceTree::slotOf(PieceTreeNode* n) noexcept {
    // Precondition: n belongs to this tree.
    if (n->parent == nullptr) {
        return m_root;
    }
    return (n == n->parent->left.get()) ? n->parent->left : n->parent->right;
}

void PieceTree::updateAggregate(PieceTreeNode* n) noexcept {
    if (n == nullptr) {
        return;
    }
    const std::uint64_t leftLen   = n->left  ? n->left->subtreeLength    : 0ULL;
    const std::uint64_t rightLen  = n->right ? n->right->subtreeLength   : 0ULL;
    const std::uint64_t leftNL    = n->left  ? n->left->subtreeNewlines  : 0ULL;
    const std::uint64_t rightNL   = n->right ? n->right->subtreeNewlines : 0ULL;
    const std::size_t   leftCnt   = n->left  ? n->left->subtreeCount     : std::size_t{0};
    const std::size_t   rightCnt  = n->right ? n->right->subtreeCount    : std::size_t{0};

    n->subtreeLength   = n->piece.length       + leftLen  + rightLen;
    n->subtreeNewlines = n->piece.newlineCount + leftNL   + rightNL;
    n->subtreeCount    = 1                     + leftCnt  + rightCnt;
}

void PieceTree::updateAggregatesUpward(PieceTreeNode* n) noexcept {
    for (PieceTreeNode* cur = n; cur != nullptr; cur = cur->parent) {
        updateAggregate(cur);
    }
}

// ---------------------------------------------------------------------------
// Rotations
// ---------------------------------------------------------------------------
//
// Ownership handling: children are held via std::unique_ptr, so each rotation
// has to physically transfer ownership between slots without letting any of
// the transient unique_ptrs destroy live nodes.
//
//   Before rotateLeft(x):        After:
//        parent                       parent
//          |                            |
//          x                            y
//         / \                          / \
//        a   y                        x   c
//           / \                      / \
//          b   c                    a   b
//
// The steps below move: x out of its slot, y out of x->right, y->left into
// x->right, x under y->left, then y back into the original slot. Aggregates
// for x (now under y) and then y are refreshed at the end.
// ---------------------------------------------------------------------------

void PieceTree::rotateLeft(PieceTreeNode* x) {
    assert(x != nullptr);
    assert(x->right != nullptr);

    PieceTreeNode* const xParent = x->parent;
    std::unique_ptr<PieceTreeNode>& originalSlot = slotOf(x);

    std::unique_ptr<PieceTreeNode> xOwned = std::move(originalSlot);
    std::unique_ptr<PieceTreeNode> yOwned = std::move(xOwned->right);

    // b := y->left; goes to x->right.
    xOwned->right = std::move(yOwned->left);
    if (xOwned->right) {
        xOwned->right->parent = xOwned.get();
    }

    PieceTreeNode* const yRaw = yOwned.get();
    PieceTreeNode* const xRaw = xOwned.get();

    // x becomes y->left.
    xRaw->parent = yRaw;
    yRaw->left   = std::move(xOwned);

    // y takes x's original parent link.
    yRaw->parent = xParent;
    originalSlot = std::move(yOwned);

    // Aggregate order: x first (its children changed), then y (whose left
    // subtree just changed and whose piece did not).
    updateAggregate(xRaw);
    updateAggregate(yRaw);
}

void PieceTree::rotateRight(PieceTreeNode* x) {
    // Symmetric of rotateLeft.
    assert(x != nullptr);
    assert(x->left != nullptr);

    PieceTreeNode* const xParent = x->parent;
    std::unique_ptr<PieceTreeNode>& originalSlot = slotOf(x);

    std::unique_ptr<PieceTreeNode> xOwned = std::move(originalSlot);
    std::unique_ptr<PieceTreeNode> yOwned = std::move(xOwned->left);

    xOwned->left = std::move(yOwned->right);
    if (xOwned->left) {
        xOwned->left->parent = xOwned.get();
    }

    PieceTreeNode* const yRaw = yOwned.get();
    PieceTreeNode* const xRaw = xOwned.get();

    xRaw->parent  = yRaw;
    yRaw->right   = std::move(xOwned);

    yRaw->parent = xParent;
    originalSlot = std::move(yOwned);

    updateAggregate(xRaw);
    updateAggregate(yRaw);
}

// ---------------------------------------------------------------------------
// RB Insert-Fixup (CLRS 3rd ed. 13.3, adapted for nullptr sentinels)
// ---------------------------------------------------------------------------

namespace {

constexpr bool isRed(const PieceTreeNode* n) noexcept {
    return n != nullptr && n->color == RbColor::Red;
}

}  // namespace

void PieceTree::insertFixup(PieceTreeNode* z) {
    while (z->parent != nullptr && z->parent->color == RbColor::Red) {
        PieceTreeNode* const grandparent = z->parent->parent;
        // The RB invariant guarantees a grandparent exists when parent is
        // red (the root is always black, so a red parent cannot be root).
        assert(grandparent != nullptr);

        if (z->parent == grandparent->left.get()) {
            PieceTreeNode* const uncle = grandparent->right.get();
            if (isRed(uncle)) {
                // Case 1: uncle is red - recolor and move z up.
                z->parent->color = RbColor::Black;
                uncle->color     = RbColor::Black;
                grandparent->color = RbColor::Red;
                z = grandparent;
            } else {
                if (z == z->parent->right.get()) {
                    // Case 2: zigzag - rotate left at parent to fall through
                    // into case 3.
                    z = z->parent;
                    rotateLeft(z);
                }
                // Case 3: recolor + rotate right at grandparent.
                z->parent->color = RbColor::Black;
                grandparent->color = RbColor::Red;
                rotateRight(grandparent);
            }
        } else {
            // Mirror: parent is grandparent's right child.
            PieceTreeNode* const uncle = grandparent->left.get();
            if (isRed(uncle)) {
                z->parent->color = RbColor::Black;
                uncle->color     = RbColor::Black;
                grandparent->color = RbColor::Red;
                z = grandparent;
            } else {
                if (z == z->parent->left.get()) {
                    z = z->parent;
                    rotateRight(z);
                }
                z->parent->color = RbColor::Black;
                grandparent->color = RbColor::Red;
                rotateLeft(grandparent);
            }
        }
    }
    m_root->color = RbColor::Black;
}

// ---------------------------------------------------------------------------
// insertAt
// ---------------------------------------------------------------------------
//
// Walks the tree using subtreeLength to find the boundary at `pos`, then
// links a fresh RED node into an empty child slot, then runs insertFixup.
// Aggregates on the ancestor chain are refreshed before fixup (fixup then
// re-refreshes them where rotations happen).
// ---------------------------------------------------------------------------

void PieceTree::insertAt(TextPos pos, const Piece& piece) {
    // Empty tree: fresh root, colored black.
    if (m_root == nullptr) {
        auto n = std::make_unique<PieceTreeNode>(piece);
        n->color = RbColor::Black;
        m_root = std::move(n);
        m_pieceCount = 1;
        return;
    }

    // Clamp pos to totalLength() to give "append" semantics for out-of-range.
    const auto total = m_root->subtreeLength;
    pos = std::min(pos, total);

    // Walk to find the parent slot for the new node.
    PieceTreeNode* parent = nullptr;
    bool           insertAsLeft = false;
    PieceTreeNode* cur    = m_root.get();
    TextPos        rel    = pos;

    while (cur != nullptr) {
        const auto leftLen = cur->left ? cur->left->subtreeLength : 0ULL;

        if (rel <= leftLen) {
            // Target boundary is inside (or at start of) the left subtree.
            if (cur->left) {
                cur = cur->left.get();
            } else {
                parent = cur;
                insertAsLeft = true;
                break;
            }
        } else {
            const auto beforeRight = leftLen + cur->piece.length;
            if (rel < beforeRight) {
                // Position falls strictly inside `cur`'s piece - caller error.
                // In debug: assert. In release: coerce to end of piece.
                assert(!"insertAt: pos falls inside a piece (caller must splitPieceAt first)");
                rel = beforeRight;  // fall-through into "insert as right subtree leftmost"
            }
            // rel is either == beforeRight (right at end of cur's piece) or > beforeRight.
            const TextPos rightRel = rel - beforeRight;
            if (cur->right) {
                cur = cur->right.get();
                rel = rightRel;
            } else {
                parent = cur;
                insertAsLeft = false;
                break;
            }
        }
    }

    // Link fresh node into parent's empty child slot.
    auto fresh = std::make_unique<PieceTreeNode>(piece);
    fresh->color = RbColor::Red;
    fresh->parent = parent;
    PieceTreeNode* const raw = fresh.get();

    if (insertAsLeft) {
        parent->left = std::move(fresh);
    } else {
        parent->right = std::move(fresh);
    }

    // Refresh aggregates on the ancestor chain before rebalancing.
    updateAggregatesUpward(raw);

    // RB rebalance.
    insertFixup(raw);

    ++m_pieceCount;
}

// ---------------------------------------------------------------------------
// RB Delete (CLRS 3rd ed. 13.4, adapted for nullptr sentinels + unique_ptr
// ownership)
// ---------------------------------------------------------------------------
//
// Two structural cases, mirroring CLRS exactly but expressed as explicit
// unique_ptr surgery (the same style as rotateLeft/rotateRight):
//
//   z has <= 1 child:  transplant z with its (possibly null) child.
//   z has 2 children:  y = minimum(z->right) takes z's place; y's own right
//                       child (x, its only possible child, since y is a
//                       minimum) takes y's old place.
//
// x may end up null in either case. Because a null x has no `.parent` field
// of its own, callers of eraseFixup must track "x's parent" externally - we
// call this xParent throughout. This is the standard technique for adapting
// CLRS's sentinel-based RB-DELETE-FIXUP to a nullptr-based tree.
//
// Aggregate correctness: after ALL structural surgery (both the initial
// splice AND any fixup rotations) completes, a single updateAggregatesUpward
// call starting from xParent's CURRENT (post-fixup) position and following
// its CURRENT parent chain to the root is sufficient to re-correct every
// aggregate that could have changed. This holds because:
//   (a) xParent is always at or below every node that fixup's rotations can
//       touch (fixup only rotates at xParent or its ancestors, exactly like
//       insert-fixup only rotates at z or its ancestors), and
//   (b) rotations preserve reachability: any node that WAS an ancestor of
//       xParent before a rotation remains reachable via xParent's currentt
//       parent-pointer chain afterward (rotations only reorder a node and
//       its immediate parent/child, never detach an outer ancestor).
// So we deliberately do NOT try to track "every touched node" - we just
// walk xParent's actual final parent chain once, after everything else is
// done.
// ---------------------------------------------------------------------------

namespace {

PieceTreeNode* treeMinimum(PieceTreeNode* n) noexcept {
    while (n->left) {
        n = n->left.get();
    }
    return n;
}

}  // namespace

// CLRS 13.4 RB-DELETE-FIXUP verbatim (4 structural cases x 2 mirrored sides);
// splitting it would break the textbook correspondence this implementation
// deliberately preserves for auditability, and it is covered by 20,000-
// iteration property tests plus dedicated RB-invariant tests (see
// document_piece_tree_test.cpp).
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void PieceTree::eraseFixup(PieceTreeNode* x, PieceTreeNode* xParent) {
    while (x != m_root.get() && (x == nullptr || x->color == RbColor::Black)) {
        assert(xParent != nullptr);  // unreachable otherwise: see loop guard above
        const bool xIsLeft = (xParent->left.get() == x);

        if (xIsLeft) {
            PieceTreeNode* w = xParent->right.get();
            assert(w != nullptr);  // RB properties guarantee a sibling exists here
            if (isRed(w)) {
                w->color = RbColor::Black;
                xParent->color = RbColor::Red;
                rotateLeft(xParent);
                w = xParent->right.get();
            }
            if (!isRed(w->left.get()) && !isRed(w->right.get())) {
                w->color = RbColor::Red;
                x = xParent;
                xParent = x->parent;
            } else {
                if (!isRed(w->right.get())) {
                    if (w->left) {
                        w->left->color = RbColor::Black;
                    }
                    w->color = RbColor::Red;
                    rotateRight(w);
                    w = xParent->right.get();
                }
                w->color = xParent->color;
                xParent->color = RbColor::Black;
                if (w->right) {
                    w->right->color = RbColor::Black;
                }
                rotateLeft(xParent);
                x = m_root.get();
                xParent = nullptr;
            }
        } else {
            // Mirror image of the above, exchanging left/right.
            PieceTreeNode* w = xParent->left.get();
            assert(w != nullptr);
            if (isRed(w)) {
                w->color = RbColor::Black;
                xParent->color = RbColor::Red;
                rotateRight(xParent);
                w = xParent->left.get();
            }
            if (!isRed(w->right.get()) && !isRed(w->left.get())) {
                w->color = RbColor::Red;
                x = xParent;
                xParent = x->parent;
            } else {
                if (!isRed(w->left.get())) {
                    if (w->right) {
                        w->right->color = RbColor::Black;
                    }
                    w->color = RbColor::Red;
                    rotateLeft(w);
                    w = xParent->left.get();
                }
                w->color = xParent->color;
                xParent->color = RbColor::Black;
                if (w->left) {
                    w->left->color = RbColor::Black;
                }
                rotateRight(xParent);
                x = m_root.get();
                xParent = nullptr;
            }
        }
    }
    if (x != nullptr) {
        x->color = RbColor::Black;
    }
}

void PieceTree::eraseNode(PieceTreeNode* z) {
    assert(z != nullptr);

    PieceTreeNode* const zParent = z->parent;
    RbColor zRemovedColor = z->color;  // the color that actually leaves the tree

    PieceTreeNode* xRaw       = nullptr;
    PieceTreeNode* xParentRaw = nullptr;

    if (z->left == nullptr || z->right == nullptr) {
        // At most one child: transplant z with that child (possibly null).
        std::unique_ptr<PieceTreeNode>& zSlot = slotOf(z);
        const std::unique_ptr<PieceTreeNode> zOwned = std::move(zSlot);
        std::unique_ptr<PieceTreeNode>  childOwned =
            zOwned->left ? std::move(zOwned->left) : std::move(zOwned->right);

        xRaw = childOwned.get();
        if (xRaw != nullptr) {
            xRaw->parent = zParent;
        }
        xParentRaw = zParent;

        zSlot = std::move(childOwned);
        // zOwned destructs here: its remaining child (the one NOT chosen
        // above) is already null by the `z->left == nullptr || z->right ==
        // nullptr` precondition, so no cascade.
    } else {
        // Two children: y = leftmost node of z's right subtree.
        PieceTreeNode* const y = treeMinimum(z->right.get());
        zRemovedColor = y->color;  // y's color is what "moves" in this case
        xRaw = y->right.get();     // y is a minimum, so y->left is always null

        if (y->parent == z) {
            // y is directly z's right child - no separate detach step needed.
            xParentRaw = y;

            std::unique_ptr<PieceTreeNode>& zSlot  = slotOf(z);
            const std::unique_ptr<PieceTreeNode> zOwned = std::move(zSlot);
            std::unique_ptr<PieceTreeNode>  yOwned = std::move(zOwned->right);  // == y

            // y keeps its existing right child (x) untouched; it inherits
            // z's left child and z's color/parent.
            yOwned->left = std::move(zOwned->left);
            if (yOwned->left) {
                yOwned->left->parent = yOwned.get();
            }
            yOwned->color  = zOwned->color;
            yOwned->parent = zParent;

            zSlot = std::move(yOwned);
            // zOwned destructs here (left/right already moved out).
        } else {
            // y is deeper inside z->right's subtree. First detach y from its
            // own slot, transplanting y's right child (x) into y's old spot.
            PieceTreeNode* const yParent = y->parent;
            std::unique_ptr<PieceTreeNode>& ySlot  = slotOf(y);
            std::unique_ptr<PieceTreeNode>  yOwned = std::move(ySlot);
            std::unique_ptr<PieceTreeNode>  xOwned = std::move(yOwned->right);

            if (xOwned) {
                xOwned->parent = yParent;
            }
            ySlot = std::move(xOwned);  // y's old slot now holds x (or null)

            xParentRaw = yParent;

            // Now detach z and give y z's left/right children + color.
            std::unique_ptr<PieceTreeNode>& zSlot  = slotOf(z);
            const std::unique_ptr<PieceTreeNode> zOwned = std::move(zSlot);

            yOwned->right = std::move(zOwned->right);
            if (yOwned->right) {
                yOwned->right->parent = yOwned.get();
            }
            yOwned->left = std::move(zOwned->left);
            if (yOwned->left) {
                yOwned->left->parent = yOwned.get();
            }
            yOwned->color  = zOwned->color;
            yOwned->parent = zParent;

            zSlot = std::move(yOwned);
            // zOwned destructs here (left/right already moved out). Note
            // xParentRaw (== yParent) is a descendant of y in the NEW
            // topology, so updateAggregatesUpward(xParentRaw) below will
            // walk through y on its way to the root.
        }
    }

    if (zRemovedColor == RbColor::Black) {
        eraseFixup(xRaw, xParentRaw);
    }

    // Unconditional: the structural change happened regardless of color, so
    // ancestor aggregates always need refreshing. Safe to call with a null
    // xParentRaw (only happens when the tree became empty or a lone child
    // became the new root - both cases need no further updates).
    if (xParentRaw != nullptr) {
        updateAggregatesUpward(xParentRaw);
    }

    --m_pieceCount;
}

void PieceTree::eraseRange(TextRange range) {
    if (m_root == nullptr) {
        return;
    }
    const auto total = m_root->subtreeLength;
    if (range.start >= total) {
        return;
    }
    const TextPos end = (range.end > total) ? total : range.end;
    if (range.start >= end) {
        return;
    }

    TextPos remaining = end - range.start;
    while (remaining > 0) {
        PieceTreeNode* node = findNodeStartingAt(range.start);
        assert(node != nullptr &&
               "eraseRange: range.start is not a valid piece boundary");
        if (node == nullptr) {
            return;  // defensive: precondition violated, stop rather than corrupt state
        }
        const std::uint64_t len = node->piece.length;
        assert(len <= remaining &&
               "eraseRange: range.end is not aligned to a piece boundary");
        eraseNode(node);
        remaining = (len >= remaining) ? 0 : (remaining - len);
    }
}

// ---------------------------------------------------------------------------
// splitPieceAt
// ---------------------------------------------------------------------------
//
// Finds the piece containing `pos`; shrinks it to end at `pos`; inserts the
// right half as a new node with the correct offset/length/newlineCount.
// ---------------------------------------------------------------------------

void PieceTree::splitPieceAt(TextPos pos, std::uint32_t leftNewlines) {
    if (m_root == nullptr) {
        assert(!"splitPieceAt on empty tree");
        return;
    }
    const auto total = m_root->subtreeLength;
    if (pos == 0 || pos >= total) {
        assert(!"splitPieceAt at boundary is a no-op / out-of-range");
        return;
    }

    // Walk to the node whose piece contains `pos` strictly inside.
    PieceTreeNode* cur    = m_root.get();
    TextPos        rel    = pos;
    TextPos        pieceStart = 0;  // logical start of `cur`'s piece

    while (cur != nullptr) {
        const auto leftLen = cur->left ? cur->left->subtreeLength : 0ULL;

        if (rel < leftLen) {
            cur = cur->left.get();
        } else if (rel < leftLen + cur->piece.length) {
            // Split point is inside cur's piece.
            pieceStart = pos - (rel - leftLen);
            break;
        } else {
            rel -= leftLen + cur->piece.length;
            pieceStart += leftLen + cur->piece.length;
            cur = cur->right.get();
        }
    }

    if (cur == nullptr) {
        assert(!"splitPieceAt: pos not found (should be unreachable)");
        return;
    }

    const std::uint64_t offsetWithin = pos - pieceStart;
    assert(offsetWithin > 0);
    assert(offsetWithin < cur->piece.length);
    assert(leftNewlines <= cur->piece.newlineCount);

    // Right half becomes a fresh piece inserted at logical position `pos`.
    Piece rightHalf{};
    rightHalf.source       = cur->piece.source;
    rightHalf.offset       = cur->piece.offset + offsetWithin;
    rightHalf.length       = cur->piece.length - offsetWithin;
    rightHalf.newlineCount = cur->piece.newlineCount - leftNewlines;

    // Shrink cur's piece and refresh aggregates on the ancestor chain.
    cur->piece.length       = offsetWithin;
    cur->piece.newlineCount = leftNewlines;
    updateAggregatesUpward(cur);

    // Now insertAt(pos, rightHalf); pos is a valid boundary (end of the
    // just-shrunk piece).
    insertAt(pos, rightHalf);
}

// ---------------------------------------------------------------------------
// pieceContainingStrictly / findNodeStartingAt
// ---------------------------------------------------------------------------
//
// Both walk the tree using the same subtreeLength-based descent as
// splitPieceAt's internal walk (kept as an independent implementation here
// rather than factored out, since splitPieceAt's walk is already
// CI-validated from Step 1 and duplicating ~10 lines is cheaper than risking
// a shared-helper regression under an environment with no local build).
// ---------------------------------------------------------------------------

std::optional<PieceTree::PieceLookup> PieceTree::pieceContainingStrictly(TextPos pos) const noexcept {
    if (m_root == nullptr) {
        return std::nullopt;
    }
    const auto total = m_root->subtreeLength;
    if (pos == 0 || pos >= total) {
        return std::nullopt;
    }

    PieceTreeNode* cur = m_root.get();
    TextPos        rel = pos;
    TextPos        subtreeStart = 0;  // absolute logical start of `cur`'s subtree

    while (cur != nullptr) {
        const auto leftLen = cur->left ? cur->left->subtreeLength : 0ULL;

        if (rel < leftLen) {
            cur = cur->left.get();
        } else if (rel < leftLen + cur->piece.length) {
            const TextPos foundPieceStart = subtreeStart + leftLen;
            if (rel == leftLen) {
                return std::nullopt;  // pos lands exactly on this piece's start
            }
            return PieceLookup{.piece = cur->piece, .pieceStart = foundPieceStart};
        } else {
            subtreeStart += leftLen + cur->piece.length;
            rel          -= leftLen + cur->piece.length;
            cur = cur->right.get();
        }
    }
    return std::nullopt;
}

PieceTreeNode* PieceTree::findNodeStartingAt(TextPos pos) const noexcept {
    if (m_root == nullptr) {
        return nullptr;
    }
    PieceTreeNode* cur = m_root.get();
    TextPos        rel = pos;

    while (cur != nullptr) {
        const auto leftLen = cur->left ? cur->left->subtreeLength : 0ULL;

        if (rel < leftLen) {
            cur = cur->left.get();
        } else if (rel == leftLen) {
            return cur;  // cur's own piece begins exactly at `pos`
        } else if (rel < leftLen + cur->piece.length) {
            // pos falls strictly inside cur's piece - caller violated the
            // "pos is a boundary" precondition.
            assert(!"findNodeStartingAt: pos is not a piece boundary");
            return nullptr;
        } else {
            rel -= leftLen + cur->piece.length;
            cur = cur->right.get();
        }
    }
    return nullptr;  // pos == totalLength(): no node starts at end-of-document
}

// ---------------------------------------------------------------------------
// Read queries
// ---------------------------------------------------------------------------

std::uint64_t PieceTree::totalLength() const noexcept {
    return m_root ? m_root->subtreeLength : 0ULL;
}

std::uint64_t PieceTree::totalNewlines() const noexcept {
    return m_root ? m_root->subtreeNewlines : 0ULL;
}

namespace {

// NOLINTBEGIN(misc-no-recursion) - recursion depth is bounded by the tree's
// black-height, O(log n) for a balanced RB-tree, so stack usage is not a
// practical concern at any document size this project targets (10GB file /
// 100K+ pieces is still only ~34 levels deep).
void collectInOrder(const PieceTreeNode* n, std::vector<Piece>& out) {
    if (n == nullptr) {
        return;
    }
    collectInOrder(n->left.get(), out);
    out.push_back(n->piece);
    collectInOrder(n->right.get(), out);
}
// NOLINTEND(misc-no-recursion)

}  // namespace

std::vector<Piece> PieceTree::collectPieces() const {
    std::vector<Piece> out;
    out.reserve(m_pieceCount);
    collectInOrder(m_root.get(), out);
    return out;
}

// ---------------------------------------------------------------------------
// validate() - RB and aggregate invariants
// ---------------------------------------------------------------------------

namespace {

struct ValidateResult {
    bool          ok;
    int           blackHeight;                  // black-nodes on any root-to-leaf path
    std::uint64_t subtreeLength;
    std::uint64_t subtreeNewlines;
    std::size_t   subtreeCount;
};

constexpr ValidateResult kInvalidResult{
    .ok = false, .blackHeight = 0, .subtreeLength = 0, .subtreeNewlines = 0, .subtreeCount = 0};

// See collectInOrder's recursion-depth justification above; the same O(log n)
// bound applies here.
// NOLINTNEXTLINE(misc-no-recursion)
ValidateResult validateNode(const PieceTreeNode* n, const PieceTreeNode* expectedParent) {
    if (n == nullptr) {
        // nil considered black -> contributes 1 to black-height.
        return ValidateResult{
            .ok = true, .blackHeight = 1, .subtreeLength = 0, .subtreeNewlines = 0, .subtreeCount = 0};
    }

    // Parent pointer consistency.
    if (n->parent != expectedParent) {
        return kInvalidResult;
    }

    // No red-red on the parent-child edge.
    if (n->color == RbColor::Red && expectedParent != nullptr
        && expectedParent->color == RbColor::Red) {
        return kInvalidResult;
    }

    const auto lv = validateNode(n->left.get(),  n);
    if (!lv.ok) {
        return kInvalidResult;
    }
    const auto rv = validateNode(n->right.get(), n);
    if (!rv.ok) {
        return kInvalidResult;
    }

    // Uniform black-height.
    if (lv.blackHeight != rv.blackHeight) {
        return kInvalidResult;
    }

    // Aggregate consistency.
    const auto expectedLen   = n->piece.length       + lv.subtreeLength   + rv.subtreeLength;
    const auto expectedNL    = n->piece.newlineCount + lv.subtreeNewlines + rv.subtreeNewlines;
    const auto expectedCount = std::size_t{1}       + lv.subtreeCount    + rv.subtreeCount;
    if (n->subtreeLength   != expectedLen)   { return kInvalidResult; }
    if (n->subtreeNewlines != expectedNL)    { return kInvalidResult; }
    if (n->subtreeCount    != expectedCount) { return kInvalidResult; }

    const int selfBlack = (n->color == RbColor::Black) ? 1 : 0;
    return ValidateResult{
        .ok = true,
        .blackHeight = lv.blackHeight + selfBlack,
        .subtreeLength = expectedLen,
        .subtreeNewlines = expectedNL,
        .subtreeCount = expectedCount};
}

}  // namespace

bool PieceTree::validate() const noexcept {
    if (m_root == nullptr) {
        return m_pieceCount == 0;
    }
    if (m_root->color != RbColor::Black) {
        return false;
    }
    const auto v = validateNode(m_root.get(), nullptr);
    if (!v.ok) {
        return false;
    }
    return v.subtreeCount == m_pieceCount;
}

}  // namespace neomifes::document
