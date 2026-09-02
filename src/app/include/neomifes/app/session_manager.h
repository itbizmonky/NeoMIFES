#pragma once

// SessionManager (WI-20a) - owns the collection of open EditorWindows plus
// every piece of app-wide (not per-window) persisted state (Settings,
// KeyBindings+accelerator table, RecentFiles, SearchHistory, the autosave
// index) - basic_design.md sec.2.3's own vocabulary ("ウィンドウは独立した
// Sessionを持ち、SessionManagerが集約管理する"). Before this WI, every one
// of these was a wWinMain local; wWinMain now constructs exactly one
// SessionManager and delegates to it instead.
//
// Because this is still a SINGLE process (basic_design.md sec.2.3 rejects
// process-per-window - see this WI's plan for the full "why", including the
// now-obsolete 0.3s-startup rationale that originally motivated it), every
// piece of app-wide state below has exactly ONE in-memory copy, referenced
// by every window's own wireNormalMode() wiring - there is no settings.json/
// autosave-index.json cross-process race to guard against, and no
// additional locking is needed: the whole app still runs on one UI thread,
// exactly as before this WI.
//
// WI-20a only ever creates ONE window (adoptFirstWindow(), called once by
// main.cpp with wWinMain's own already-loaded startup Document - see that
// function's own comment for why this avoids a redundant reload). The
// general "create a new window on demand" capability (CommandId::NewWindow,
// the WM_COPYDATA second-launch handoff) is WI-20b's own addition, layered
// on top of this WI's `m_windows` plumbing without changing it.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>
#include <windows.h>

#include "neomifes/app/command_dispatch.h"
#include "neomifes/app/editor_window.h"
#include "neomifes/core/autosave_index.h"
#include "neomifes/core/key_bindings.h"
#include "neomifes/core/recent_files.h"
#include "neomifes/core/search_history.h"
#include "neomifes/core/settings.h"
#include "neomifes/logmode/log_pattern.h"
#include "neomifes/platform/handle_guard.h"

namespace neomifes::app {

class SessionManager {
public:
    explicit SessionManager(HINSTANCE hInstance);

    SessionManager(const SessionManager&)            = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    SessionManager(SessionManager&&)                 = delete;
    SessionManager& operator=(SessionManager&&)      = delete;
    ~SessionManager() = default;

    // WI-20a: the only window-creation entry point that exists yet. Adopts
    // wWinMain's own already-prepared startup Document/fileState/path
    // (main.cpp's prepareDocument() call), runs the crash-recovery prompt
    // loop against the new EditorWindow's Workspace (same ordering as
    // before this WI: after the Workspace exists, before the window is
    // shown), then wires + shows it via wireNormalMode(). Returns false
    // only if ui::MainWindow::create() itself fails (mirrors wWinMain's own
    // pre-WI-20a `if (!window.create(...)) return 1;` contract) - the
    // partially built EditorWindow is simply not added to m_windows (RAII,
    // no leak).
    [[nodiscard]] bool adoptFirstWindow(document::Document doc, DocumentFileState fileState,
                                        const std::optional<std::filesystem::path>& path);

    [[nodiscard]] std::size_t windowCount() const noexcept { return m_windows.size(); }

    [[nodiscard]] const platform::AcceleratorTableHandle& acceleratorTable() const noexcept {
        return m_accelTable;
    }

    // Called once by main.cpp at clean exit (after the message loop
    // returns) - same batched-at-exit contract core::SearchHistory/
    // core::RecentFiles already had as wWinMain locals (see their own
    // header comments on why this - unlike core::AutosaveIndex, written
    // immediately on every change - doesn't need to happen more often).
    void persistOnExit() const;

private:
    // Constructs `w`'s HMENU, wires every wireNormalMode() callback into
    // its MainWindowConfig, and creates its HWND. Extracted from
    // adoptFirstWindow() purely so a future WI-20b createWindow() can share
    // it without duplicating the wiring call.
    [[nodiscard]] bool wireAndShow(EditorWindow& w);

    // MainWindowConfig::onDestroyed hook target (WI-20a) - erases `w` from
    // m_windows and, once the last window is gone, calls
    // ::PostQuitMessage(0) (the "only quit when every window is closed"
    // requirement this WI exists to satisfy).
    void onWindowDestroyed(EditorWindow* w) noexcept;

    HINSTANCE m_hInstance;
    std::vector<std::unique_ptr<EditorWindow>> m_windows;

    // App-wide state, one in-memory copy each - see this header's own top
    // comment for why no per-process race/locking concern applies here.
    // Declaration order matters: m_autosaveIndex must precede m_autosave
    // (AutosaveContext holds a REFERENCE into it - members initialize in
    // declaration order regardless of mem-initializer-list order).
    core::Settings                       m_settings;
    std::optional<std::filesystem::path> m_settingsPath;
    core::KeyBindings                    m_keyBindings;
    std::optional<std::filesystem::path> m_keyBindingsPath;
    platform::AcceleratorTableHandle     m_accelTable;
    core::RecentFiles                    m_recentFiles;
    std::optional<std::filesystem::path> m_recentFilesPath;
    core::SearchHistory                  m_searchHistory;
    std::optional<std::filesystem::path> m_searchHistoryPath;
    std::optional<std::filesystem::path> m_autosaveDir;
    core::AutosaveIndex                  m_autosaveIndex;
    std::optional<std::filesystem::path> m_autosaveIndexPath;
    AutosaveContext                      m_autosave;
    std::vector<logmode::LogPatternRule> m_userLogPatterns;
    std::optional<std::filesystem::path> m_logPatternsDir;
};

}  // namespace neomifes::app
