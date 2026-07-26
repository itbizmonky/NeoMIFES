#include "neomifes/core/folding_model.h"

#include <algorithm>

namespace neomifes::core {

namespace {

[[nodiscard]] bool hides(const FoldRegion& region, document::LineNumber line) noexcept {
    return region.folded && line > region.headerLine && line <= region.endLineInclusive;
}

}  // namespace

void FoldingModel::setFoldableRegions(std::vector<FoldRegion> regions) {
    for (auto& region : regions) {
        const auto previous =
            std::ranges::find(m_regions, region.headerLine, &FoldRegion::headerLine);
        if (previous != m_regions.end()) {
            region.folded = previous->folded;
        }
    }
    m_regions = std::move(regions);
}

void FoldingModel::toggleFold(document::LineNumber headerLine) noexcept {
    const auto it = std::ranges::find(m_regions, headerLine, &FoldRegion::headerLine);
    if (it != m_regions.end()) {
        it->folded = !it->folded;
    }
}

bool FoldingModel::isLineHidden(document::LineNumber line) const noexcept {
    return std::ranges::any_of(m_regions, [line](const FoldRegion& region) { return hides(region, line); });
}

bool FoldingModel::isFoldHeader(document::LineNumber line) const noexcept {
    return std::ranges::find(m_regions, line, &FoldRegion::headerLine) != m_regions.end();
}

std::optional<FoldRegion> FoldingModel::regionAt(document::LineNumber headerLine) const noexcept {
    const auto it = std::ranges::find(m_regions, headerLine, &FoldRegion::headerLine);
    return it != m_regions.end() ? std::optional<FoldRegion>(*it) : std::nullopt;
}

std::optional<FoldRegion> FoldingModel::foldedRegionContaining(document::LineNumber line) const noexcept {
    // Outermost match (smallest headerLine) - a parent region's span always
    // encloses every nested child's span, so this yields the largest hidden
    // block covering `line`, which is what movement-key correction wants to
    // snap past in one step (see editor_input.cpp's Phase 7i correction).
    std::optional<FoldRegion> outermost;
    for (const FoldRegion& region : m_regions) {
        if (!hides(region, line)) {
            continue;
        }
        if (!outermost || region.headerLine < outermost->headerLine) {
            outermost = region;
        }
    }
    return outermost;
}

bool FoldingModel::revealLine(document::LineNumber line) noexcept {
    bool changed = false;
    for (FoldRegion& region : m_regions) {
        if (hides(region, line)) {
            region.folded = false;
            changed        = true;
        }
    }
    return changed;
}

}  // namespace neomifes::core
