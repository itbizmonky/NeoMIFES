#include "neomifes/core/cumulative_shift_edit.h"

#include <cstdint>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"

namespace neomifes::core {

void applyEditsWithCumulativeShift(document::Document& doc, std::span<const PerCursorEdit> edits,
                                    std::vector<std::u16string>&    outDeletedTexts,
                                    std::vector<document::TextPos>& outStartsAtExecute) {
    const std::size_t n = edits.size();
    outDeletedTexts.assign(n, std::u16string{});
    outStartsAtExecute.assign(n, document::TextPos{0});

    std::int64_t cumulativeShift = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const PerCursorEdit& edit         = edits[i];
        const auto           currentStart = static_cast<document::TextPos>(
            static_cast<std::int64_t>(edit.range.start) + cumulativeShift);
        const document::TextRange currentRange{.start = currentStart,
                                               .end   = currentStart + edit.range.length()};

        outDeletedTexts[i]    = doc.snapshot()->extract(currentRange);
        outStartsAtExecute[i] = currentStart;
        doc.replaceRange(currentRange, edit.insertedText);

        cumulativeShift += static_cast<std::int64_t>(edit.insertedText.size()) -
                           static_cast<std::int64_t>(edit.range.length());
    }
}

void undoEditsDescending(document::Document& doc, std::span<const PerCursorEdit> edits,
                          std::span<const std::u16string>    deletedTexts,
                          std::span<const document::TextPos> startsAtExecute) {
    for (std::size_t i = edits.size(); i-- > 0;) {
        const document::TextPos   start = startsAtExecute[i];
        const document::TextRange insertedRange{.start = start,
                                                .end    = start + edits[i].insertedText.size()};
        doc.replaceRange(insertedRange, deletedTexts[i]);
    }
}

}  // namespace neomifes::core
