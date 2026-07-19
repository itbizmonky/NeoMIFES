#include <gtest/gtest.h>

#include <vector>

#include "neomifes/core/cursor.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/replace_all_command.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/document/document.h"
#include "neomifes/search/replacement.h"
#include "neomifes/search/search_service.h"

namespace {

using neomifes::core::Cursor;
using neomifes::core::ExecutionContext;
using neomifes::core::PerCursorEdit;
using neomifes::core::ReplaceAllCommand;
using neomifes::core::SelectionModel;
using neomifes::document::Document;
using neomifes::document::TextRange;
using neomifes::search::expandReplacementTemplate;
using neomifes::search::Match;
using neomifes::search::Query;
using neomifes::search::SearchService;

TEST(ReplaceAllCommandTest, ExecuteReplacesAllMatchesThenUndoRestoresOriginal) {
    Document doc;
    doc.insertText(0, u"foo bar foo");  // replace both "foo" -> "quux" (longer) and "bar" -> "X" (shorter)
    SelectionModel   selection;
    ExecutionContext ctx(doc, selection);

    std::vector<PerCursorEdit> edits{
        PerCursorEdit{.range = TextRange{.start = 0, .end = 3}, .insertedText = u"quux"},
        PerCursorEdit{.range = TextRange{.start = 4, .end = 7}, .insertedText = u"X"},
        PerCursorEdit{.range = TextRange{.start = 8, .end = 11}, .insertedText = u"quux"},
    };
    std::vector<Cursor> before{Cursor{.position = 5, .anchor = 5, .isPrimary = true}};
    ReplaceAllCommand    cmd(edits, before);

    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"quux X quux");

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"foo bar foo");
}

TEST(ReplaceAllCommandTest, CursorsAfterExecuteAndUndoBothReturnCursorsBeforeVerbatim) {
    Document doc;
    doc.insertText(0, u"foo foo");
    SelectionModel   selection;
    ExecutionContext ctx(doc, selection);

    std::vector<PerCursorEdit> edits{
        PerCursorEdit{.range = TextRange{.start = 0, .end = 3}, .insertedText = u"X"},
        PerCursorEdit{.range = TextRange{.start = 4, .end = 7}, .insertedText = u"X"},
    };
    // Cursor sits in the middle of the document, unrelated to either match -
    // replace-all must never move it, unlike MultiCursorEditCommand.
    std::vector<Cursor> before{Cursor{.position = 3, .anchor = 5, .isPrimary = true}};
    ReplaceAllCommand    cmd(edits, before);

    cmd.execute(ctx);
    const auto afterExecute = cmd.cursorsAfterExecute();
    ASSERT_EQ(afterExecute.size(), 1U);
    EXPECT_EQ(afterExecute[0], before[0]);

    cmd.undo(ctx);
    const auto afterUndo = cmd.cursorsAfterUndo();
    ASSERT_EQ(afterUndo.size(), 1U);
    EXPECT_EQ(afterUndo[0], before[0]);
}

TEST(ReplaceAllCommandTest, HandlesMismatchedEditsAndCursorsBeforeSizes) {
    // The entire point of ReplaceAllCommand over MultiCursorEditCommand:
    // match count (edits) is unrelated to cursor count. 1 cursor, 3 edits.
    Document doc;
    doc.insertText(0, u"a a a");
    SelectionModel   selection;
    ExecutionContext ctx(doc, selection);

    std::vector<PerCursorEdit> edits{
        PerCursorEdit{.range = TextRange{.start = 0, .end = 1}, .insertedText = u"X"},
        PerCursorEdit{.range = TextRange{.start = 2, .end = 3}, .insertedText = u"X"},
        PerCursorEdit{.range = TextRange{.start = 4, .end = 5}, .insertedText = u"X"},
    };
    std::vector<Cursor> before{Cursor{.position = 0, .anchor = 0, .isPrimary = true}};
    ReplaceAllCommand    cmd(edits, before);

    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"X X X");
    ASSERT_EQ(cmd.cursorsAfterExecute().size(), 1U);

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"a a a");
}

TEST(ReplaceAllCommandTest, EmptyEditsListIsANoOp) {
    Document doc;
    doc.insertText(0, u"unchanged");
    SelectionModel   selection;
    ExecutionContext ctx(doc, selection);

    ReplaceAllCommand cmd({}, {});

    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"unchanged");

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"unchanged");
}

TEST(ReplaceAllCommandTest, WeightSumsAllEdits) {
    std::vector<PerCursorEdit> edits{
        PerCursorEdit{.range = TextRange{.start = 0, .end = 0}, .insertedText = u"ab"},
        PerCursorEdit{.range = TextRange{.start = 5, .end = 5}, .insertedText = u"c"},
    };
    const ReplaceAllCommand cmd(edits, {});
    EXPECT_EQ(cmd.weight(), ((2U * 2) + 32) + ((1U * 2) + 32));
}

TEST(ReplaceAllCommandTest, IntegrationFindAllExpandTemplateThenReplaceAllProducesExpectedDocument) {
    // Proves the full (headless) replace-all pipeline composes correctly:
    // SearchService::findAll() -> expandReplacementTemplate() per match
    // (manually turning each into a PerCursorEdit here, since the
    // permanent glue code is deferred to Phase 5b3's UI wiring - see
    // replace_all_command.h's class comment) -> ReplaceAllCommand.
    Document doc;
    doc.insertText(0, u"name: alice, name: bob");
    const Query query{.pattern = u"name: (\\w+)", .regex = true};

    const std::vector<Match> matches = SearchService::findAll(doc, query);
    ASSERT_EQ(matches.size(), 2U);

    std::vector<PerCursorEdit> edits;
    edits.reserve(matches.size());
    for (const Match& match : matches) {
        edits.push_back(PerCursorEdit{
            .range        = match.range,
            .insertedText = expandReplacementTemplate(u"user=$1", doc, match),
        });
    }

    SelectionModel   selection;
    ExecutionContext ctx(doc, selection);
    ReplaceAllCommand cmd(std::move(edits), {});

    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"user=alice, user=bob");

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"name: alice, name: bob");
}

}  // namespace
