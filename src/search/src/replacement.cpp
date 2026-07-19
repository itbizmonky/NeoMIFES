#include "neomifes/search/replacement.h"

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"

namespace neomifes::search {

std::u16string expandReplacementTemplate(std::u16string_view replacementTemplate,
                                          const document::Document& doc, const Match& match) {
    std::u16string result;
    result.reserve(replacementTemplate.size());

    for (std::size_t i = 0; i < replacementTemplate.size(); ++i) {
        const char16_t ch = replacementTemplate[i];
        if (ch != u'$' || i + 1 >= replacementTemplate.size()) {
            result.push_back(ch);
            continue;
        }

        const char16_t next = replacementTemplate[i + 1];
        if (next == u'$') {
            result.push_back(u'$');
            ++i;
        } else if (next == u'&' || next == u'0') {
            result.append(doc.snapshot()->extract(match.range));
            ++i;
        } else if (next >= u'1' && next <= u'9') {
            const auto groupIndex = static_cast<std::size_t>(next - u'1');
            if (groupIndex < match.groups.size()) {
                const document::TextRange& group = match.groups[groupIndex];
                if (!group.empty()) {
                    result.append(doc.snapshot()->extract(group));
                }
                ++i;
            } else {
                // Out-of-range group reference: leave "$N" as literal text.
                // Push only '$' here; the next loop iteration processes
                // `next` (the digit) as an ordinary literal character.
                result.push_back(ch);
            }
        } else {
            // Unrecognized escape (e.g. "$x"): leave '$' literal, let
            // `next` be processed normally on the next iteration.
            result.push_back(ch);
        }
    }

    return result;
}

}  // namespace neomifes::search
