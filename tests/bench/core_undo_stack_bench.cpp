// Micro-benchmark proving the Phase 4 DoD from CLAUDE.md sec.7 / the
// requirements doc sec.5: "Undo: 100万回以上" (1,000,000+ undo operations).
// Phase 4a's UndoStack is a plain two-stack implementation with no
// bucketing/compression/disk-swap (see ADR-012); this benchmark's real
// numbers - not the design sketch's speculative memory budget - are the
// evidence for whether that simple approach is sufficient at 1M scale
// (CLAUDE.md rule #10) and the tripwire input for
// docs/issues/undo_stack_unbounded_memory.md.
//
// Each `state` iteration performs the full 1,000,000-command
// push (BM_UndoStack_PushOneMillion) or undo (BM_UndoStack_UndoOneMillion)
// sequence as a single unit of work, mirroring how
// BM_PieceTable_Snapshot_100K seeds its scale once rather than using
// benchmark::State::range() (not used anywhere in this codebase's bench
// suite).

#include <benchmark/benchmark.h>

#include <memory>
#include <string>

#include "neomifes/core/command.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/undo_stack.h"
#include "neomifes/document/document.h"

using neomifes::core::ExecutionContext;
using neomifes::core::InsertTextCommand;
using neomifes::core::SelectionModel;
using neomifes::core::UndoStack;
using neomifes::document::Document;

namespace {
constexpr int kOpCount = 1'000'000;
}  // namespace

static void BM_UndoStack_PushOneMillion(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        Document          doc;
        SelectionModel    selection;
        ExecutionContext  ctx(doc, selection);
        UndoStack         stack;
        state.ResumeTiming();

        for (int i = 0; i < kOpCount; ++i) {
            auto cmd = std::make_unique<InsertTextCommand>(doc.length(), std::u16string(1, u'x'));
            cmd->execute(ctx);
            stack.push(std::move(cmd));
        }
        benchmark::DoNotOptimize(stack.undoCount());
    }
    state.SetItemsProcessed(state.iterations() * kOpCount);
}
BENCHMARK(BM_UndoStack_PushOneMillion);

static void BM_UndoStack_UndoOneMillion(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        Document          doc;
        SelectionModel    selection;
        ExecutionContext  ctx(doc, selection);
        UndoStack         stack;
        for (int i = 0; i < kOpCount; ++i) {
            auto cmd = std::make_unique<InsertTextCommand>(doc.length(), std::u16string(1, u'x'));
            cmd->execute(ctx);
            stack.push(std::move(cmd));
        }
        state.ResumeTiming();

        while (stack.canUndo()) {
            stack.undo(ctx);
        }
        benchmark::DoNotOptimize(doc.length());
    }
    state.SetItemsProcessed(state.iterations() * kOpCount);
}
BENCHMARK(BM_UndoStack_UndoOneMillion);
