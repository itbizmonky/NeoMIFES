#include "neomifes/document/piece_tree.h"

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
    if (pos > total) {
        pos = total;
    }

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
// Read queries
// ---------------------------------------------------------------------------

std::uint64_t PieceTree::totalLength() const noexcept {
    return m_root ? m_root->subtreeLength : 0ULL;
}

std::uint64_t PieceTree::totalNewlines() const noexcept {
    return m_root ? m_root->subtreeNewlines : 0ULL;
}

namespace {

void collectInOrder(const PieceTreeNode* n, std::vector<Piece>& out) {
    if (n == nullptr) {
        return;
    }
    collectInOrder(n->left.get(), out);
    out.push_back(n->piece);
    collectInOrder(n->right.get(), out);
}

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

ValidateResult validateNode(const PieceTreeNode* n, const PieceTreeNode* expectedParent) {
    if (n == nullptr) {
        return {true, 1, 0, 0, 0};  // nil considered black -> contributes 1 to black-height
    }

    // Parent pointer consistency.
    if (n->parent != expectedParent) {
        return {false, 0, 0, 0, 0};
    }

    // No red-red on the parent-child edge.
    if (n->color == RbColor::Red && expectedParent != nullptr
        && expectedParent->color == RbColor::Red) {
        return {false, 0, 0, 0, 0};
    }

    const auto lv = validateNode(n->left.get(),  n);
    if (!lv.ok) return {false, 0, 0, 0, 0};
    const auto rv = validateNode(n->right.get(), n);
    if (!rv.ok) return {false, 0, 0, 0, 0};

    // Uniform black-height.
    if (lv.blackHeight != rv.blackHeight) {
        return {false, 0, 0, 0, 0};
    }

    // Aggregate consistency.
    const auto expectedLen   = n->piece.length       + lv.subtreeLength   + rv.subtreeLength;
    const auto expectedNL    = n->piece.newlineCount + lv.subtreeNewlines + rv.subtreeNewlines;
    const auto expectedCount = std::size_t{1}       + lv.subtreeCount    + rv.subtreeCount;
    if (n->subtreeLength   != expectedLen)   return {false, 0, 0, 0, 0};
    if (n->subtreeNewlines != expectedNL)    return {false, 0, 0, 0, 0};
    if (n->subtreeCount    != expectedCount) return {false, 0, 0, 0, 0};

    const int selfBlack = (n->color == RbColor::Black) ? 1 : 0;
    return {true,
            lv.blackHeight + selfBlack,
            expectedLen,
            expectedNL,
            expectedCount};
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
