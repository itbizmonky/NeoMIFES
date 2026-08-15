#include <gtest/gtest.h>

#include <algorithm>

#include <windows.h>

#include "neomifes/app/keybinding_dispatch.h"

namespace {

using neomifes::app::buildAcceleratorRows;
using neomifes::app::chordMatches;
using neomifes::app::resolveKeyBindingConflicts;
using neomifes::core::KeyBindings;
using neomifes::ui::CommandId;

TEST(ResolveKeyBindingConflictsTest, NeomifesPresetResolvesEachConfiguredCommandToItself) {
    const KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    const auto         winners = resolveKeyBindingConflicts(bindings);
    EXPECT_EQ(winners.at(u"Ctrl+S"), CommandId::Save);
    EXPECT_EQ(winners.at(u"Ctrl+F"), CommandId::FindShow);
    EXPECT_EQ(winners.at(u"F3"), CommandId::FindNext);
}

TEST(ResolveKeyBindingConflictsTest, LaterEnumDeclaredCommandWinsAConflict) {
    // command_ids.h declares Save (an HACCEL-eligible command) before
    // FindShow... no wait: FindShow is declared FIRST in command_ids.h,
    // Save comes after. Deliberately construct the collision so the
    // "later wins" rule is unambiguous either way it's phrased: bind both
    // to the identical chord and check the declared-later one (Save) wins
    // over the declared-earlier one (FindShow).
    KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    bindings.setChords(u"find.show", {u"Ctrl+Q"});
    bindings.setChords(u"file.save", {u"Ctrl+Q"});
    const auto winners = resolveKeyBindingConflicts(bindings);
    EXPECT_EQ(winners.at(u"Ctrl+Q"), CommandId::Save);
}

TEST(ResolveKeyBindingConflictsTest, ChordStringCaseDoesNotProduceSeparateEntries) {
    KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    bindings.setChords(u"find.show", {u"ctrl+q"});
    bindings.setChords(u"file.save", {u"Ctrl+Q"});
    const auto winners = resolveKeyBindingConflicts(bindings);
    // Both entries collapse to the same canonical "Ctrl+Q" key - only one
    // winner should be recorded, not two independent (differently-cased)
    // entries that would let both commands fire. file.save is declared
    // AFTER find.show in command_ids.h (ui::kAllRemappableCommandIds), so
    // it wins - same as LaterEnumDeclaredCommandWinsAConflict above.
    EXPECT_FALSE(winners.empty());
    EXPECT_EQ(winners.at(u"Ctrl+Q"), CommandId::Save);
}

TEST(ChordMatchesTest, ReturnsTrueForAConfiguredChord) {
    const KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    EXPECT_TRUE(chordMatches(bindings, CommandId::FindShow, /*ctrlDown=*/true, /*shiftDown=*/false,
                              /*altDown=*/false, static_cast<UINT>(u'F')));
}

TEST(ChordMatchesTest, ReturnsFalseForAnUnconfiguredKeyCombination) {
    const KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    EXPECT_FALSE(chordMatches(bindings, CommandId::FindShow, /*ctrlDown=*/false, /*shiftDown=*/false,
                               /*altDown=*/false, static_cast<UINT>(u'F')));
}

TEST(ChordMatchesTest, ReturnsFalseWhenCommandIsUnboundUnderThisPreset) {
    // Hidemaru's preset leaves search.grep.show unconfirmed/unbound.
    const KeyBindings bindings = KeyBindings::forPreset(u"hidemaru");
    EXPECT_FALSE(chordMatches(bindings, CommandId::GrepShow, /*ctrlDown=*/true, /*shiftDown=*/true,
                               /*altDown=*/false, static_cast<UINT>(u'F')));
}

TEST(ChordMatchesTest, MatchesEitherOfMultipleConfiguredChords) {
    const KeyBindings bindings = KeyBindings::forPreset(u"neomifes");  // tab.next: Ctrl+Tab, Ctrl+PageDown
    EXPECT_TRUE(chordMatches(bindings, CommandId::TabNext, true, false, false, static_cast<UINT>(VK_TAB)));
    EXPECT_TRUE(chordMatches(bindings, CommandId::TabNext, true, false, false, static_cast<UINT>(VK_NEXT)));
}

// Split into 2 TEST cases (was 1, combining both assertions below) - purely
// to keep each function's clang-tidy cognitive-complexity score under the
// project's threshold; no coverage change.
TEST(BuildAcceleratorRowsTest, ProducesAWellFormedRowForAConfiguredHacceleratorEligibleChord) {
    const KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    const auto        rows     = buildAcceleratorRows(bindings);
    bool              foundSave = false;
    for (const ACCEL& row : rows) {
        if (row.cmd != static_cast<WORD>(CommandId::Save)) {
            continue;
        }
        foundSave = true;
        EXPECT_EQ(row.key, static_cast<WORD>(u'S'));
        EXPECT_TRUE((row.fVirt & FCONTROL) != 0);
        EXPECT_TRUE((row.fVirt & FVIRTKEY) != 0);
    }
    EXPECT_TRUE(foundSave);
}

TEST(BuildAcceleratorRowsTest, ProducesOneRowPerConfiguredChordForACommandWithMultipleChords) {
    // tab.next has 2 chords -> 2 rows for TabNext.
    const KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    const auto        rows     = buildAcceleratorRows(bindings);
    const auto        tabNextRows = std::ranges::count_if(
        rows, [](const ACCEL& row) { return row.cmd == static_cast<WORD>(CommandId::TabNext); });
    EXPECT_EQ(tabNextRows, 2);
}

TEST(BuildAcceleratorRowsTest, OmitsRowWhenAnHacceleratorEligibleCommandLosesToAManualChainCommand) {
    // Rebind Save (HACCEL-eligible) onto Ctrl+Q, which edit.copy (Copy -
    // manual-chain only, NOT in kAcceleratorEligibleCommands) is ALSO bound
    // to here. Copy is declared AFTER every HACCEL-eligible command in
    // command_ids.h (ui::kAllRemappableCommandIds lists Copy/Cut/Paste/
    // Undo/Redo/ToggleOverwriteMode last), so it wins - Save must not get
    // an ACCEL row for Ctrl+Q (that would let HACCEL steal the keystroke
    // away from the manual chain's Copy handling before it ever runs).
    KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    bindings.setChords(u"file.save", {u"Ctrl+Q"});
    bindings.setChords(u"edit.copy", {u"Ctrl+Q"});
    const auto rows = buildAcceleratorRows(bindings);
    for (const ACCEL& row : rows) {
        EXPECT_FALSE(row.cmd == static_cast<WORD>(CommandId::Save) && row.key == static_cast<WORD>(u'Q'));
    }
}

TEST(BuildAcceleratorRowsTest, SkipsUnparsableChordsWithoutCrashing) {
    KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    bindings.setChords(u"file.save", {u"NotAValidChord"});
    const auto rows = buildAcceleratorRows(bindings);
    for (const ACCEL& row : rows) {
        EXPECT_NE(row.cmd, static_cast<WORD>(CommandId::Save));
    }
}

}  // namespace
