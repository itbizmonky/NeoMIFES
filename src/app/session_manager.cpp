#include "neomifes/app/session_manager.h"

#include <utility>
#include <variant>

#include "neomifes/app/launch_setup.h"
#include "neomifes/app/menu_bar.h"
#include "neomifes/app/message_dialogs.h"
#include "neomifes/app/normal_mode_wiring.h"
#include "neomifes/app/theme_settings.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/logmode/log_pattern_file.h"
#include "neomifes/platform/app_data_dir.h"

namespace neomifes::app {

namespace {

using platform::resolveAppDataDir;

// WI-20a: moved verbatim from main.cpp's wWinMain (pre-WI-20a) - the
// crash-recovery prompt loop, unchanged behavior. `owner=nullptr` for
// showCrashRecoveryDialog() is deliberate (see that function's own doc
// comment) - the new EditorWindow's own window hasn't been created yet at
// this point. Regardless of the user's choice, the autosave copy for THIS
// candidate is always cleaned up (tmp file + index entry) - declining must
// not leave it around to be wrongly re-offered on the next launch, and
// accepting means the content has already been adopted into a real session
// (the .tmp copy is now redundant).
void processRecoverableAutoSaves(Workspace& workspace, const std::vector<RecoverableAutoSave>& recoverableAutoSaves,
                                 core::AutosaveIndex&                        autosaveIndex,
                                 const std::optional<std::filesystem::path>& autosaveIndexPath) {
    for (const RecoverableAutoSave& candidate : recoverableAutoSaves) {
        const bool accepted =
            neomifes::app::showCrashRecoveryDialog(nullptr, candidate.originalPath.filename().wstring());
        if (accepted) {
            auto loaded = neomifes::document::loadFile(candidate.autosaveTmpPath);
            if (auto* result = std::get_if<neomifes::document::LoadResult>(&loaded)) {
                const DocumentFileState recoveredFileState{.encoding   = result->detectedEncoding,
                                                            .lineEnding = result->lineEnding,
                                                            .writeBom   = result->hadBom};
                auto recoveredSession = std::make_unique<EditorSession>(
                    std::move(*result->document), recoveredFileState, candidate.originalPath);
                // The recovered content differs from originalPath on disk
                // (that's the whole point of offering recovery) - a freshly
                // constructed Document starts clean (isDirty()==false), so
                // this must be marked dirty explicitly or the tab would
                // silently look saved despite representing unsaved,
                // recovered content.
                recoveredSession->document().markDirty();
                static_cast<void>(workspace.adoptSession(std::move(recoveredSession)));
            }
            // A failed load (LoadError) leaves nothing adopted - the
            // autosave copy is still cleaned up below, same as a decline.
        }
        if (autosaveIndexPath) {
            std::error_code ec;
            std::filesystem::remove(candidate.autosaveTmpPath, ec);
            autosaveIndex.remove(candidate.hash);
            autosaveIndex.saveTo(*autosaveIndexPath);
        }
    }
}

}  // namespace

SessionManager::SessionManager(HINSTANCE hInstance)
    : m_hInstance(hInstance),
      m_keyBindings(core::KeyBindings::forPreset(u"neomifes")),
      // Bound once, here - AutosaveContext::index is a REFERENCE, so this
      // binding must happen in the mem-initializer list (m_autosaveIndex is
      // declared earlier, so it already exists). Its VALUE is filled in by
      // the body below via ordinary assignment - the reference
      // transparently observes that later update, same as any C++
      // reference would. autosaveDir/indexPath are plain
      // std::optional<path> fields (not references), so - unlike index -
      // they do NOT auto-track later assignment to m_autosaveDir/
      // m_autosaveIndexPath; the body re-assigns them explicitly at the end
      // once those are resolved.
      m_autosave{.autosaveDir = std::nullopt, .index = m_autosaveIndex, .indexPath = std::nullopt} {
    // WI-20a: this constructor is only ever invoked for a real (Normal-mode)
    // launch - main.cpp's measurement-mode branches never construct a
    // SessionManager at all - so every %APPDATA% resolution below happens
    // unconditionally (the pre-WI-20a code's own `if (args.mode ==
    // LaunchMode::Normal)` guards are simply not needed here anymore).

    if (const auto appDataDir = resolveAppDataDir(); appDataDir) {
        m_settingsPath = *appDataDir / L"settings.json";
        m_settings     = core::Settings::loadFrom(*m_settingsPath);

        m_keyBindingsPath = *appDataDir / L"keybindings.json";
        m_keyBindings     = core::KeyBindings::loadFrom(*m_keyBindingsPath);

        m_recentFilesPath = *appDataDir / L"recent.json";
        m_recentFiles     = core::RecentFiles::loadFrom(*m_recentFilesPath);

        m_searchHistoryPath = *appDataDir / L"search_history.json";
        m_searchHistory     = core::SearchHistory::loadFrom(*m_searchHistoryPath);

        // Autosave directory - unlike resolveAppDataDir() itself, this
        // subdirectory is NOT created automatically, so it's created here,
        // best-effort (every autosave-related member simply stays at its
        // default-constructed/empty value on failure, same graceful-
        // degradation contract every other %APPDATA%-backed piece of state
        // in this constructor already has).
        const std::filesystem::path candidateAutosaveDir = *appDataDir / L"autosave";
        std::error_code              ec;
        std::filesystem::create_directories(candidateAutosaveDir, ec);
        if (!ec) {
            m_autosaveDir       = candidateAutosaveDir;
            m_autosaveIndexPath = candidateAutosaveDir / L"index.json";
            m_autosaveIndex     = core::AutosaveIndex::loadFrom(*m_autosaveIndexPath);
        }

        // User-editable log-pattern files (WI-14d) - same "create the
        // subdirectory best-effort, degrade to empty state on any failure"
        // shape as autosave above.
        const std::filesystem::path candidateLogPatternsDir = *appDataDir / L"log_patterns";
        std::error_code              logPatternsEc;
        std::filesystem::create_directories(candidateLogPatternsDir, logPatternsEc);
        if (!logPatternsEc) {
            m_logPatternsDir  = candidateLogPatternsDir;
            m_userLogPatterns = logmode::loadUserLogPatternsFromDirectory(candidateLogPatternsDir);
        }
    }

    // m_autosave's non-reference fields need the real values now that
    // they're resolved - see this constructor's own mem-initializer-list
    // comment above for why this can't happen there.
    m_autosave.autosaveDir = m_autosaveDir;
    m_autosave.indexPath   = m_autosaveIndexPath;

    // WI-10: built AFTER m_keyBindings is fully resolved above (needs no
    // HWND, so this is safe before any window exists). Re-built in place by
    // the "keybindings.reload"/"keybindings.preset.*" palette commands via
    // wireNormalMode()'s own accelTable& parameter - HandleGuard::
    // operator=(HandleGuard&&) destroys the old HACCEL first, a safe,
    // leak-free live swap.
    m_accelTable = buildAcceleratorTable(m_keyBindings);
}

bool SessionManager::adoptFirstWindow(document::Document doc, DocumentFileState fileState,
                                      const std::optional<std::filesystem::path>& path) {
    auto w = std::make_unique<EditorWindow>(std::move(doc), fileState, path);

    // WI-11 crash-recovery prompt loop, right after the new Workspace
    // exists (adoptSession() needs it) and before the window is shown -
    // same ordering main.cpp's wWinMain used before this WI.
    if (m_autosaveDir) {
        const std::vector<RecoverableAutoSave> recoverable =
            scanForRecoverableAutoSaves(*m_autosaveDir, m_autosaveIndex);
        processRecoverableAutoSaves(w->workspace, recoverable, m_autosaveIndex, m_autosaveIndexPath);
    }

    if (!wireAndShow(*w)) {
        return false;
    }
    m_windows.push_back(std::move(w));
    return true;
}

bool SessionManager::createWindow(const std::optional<std::filesystem::path>& openPath) {
    DocumentFileState                    fileState;
    std::optional<std::filesystem::path> currentDocumentPath;
    document::Document doc = loadDocumentForOpenPath(openPath, fileState, currentDocumentPath);

    auto w = std::make_unique<EditorWindow>(std::move(doc), fileState, currentDocumentPath);
    if (!wireAndShow(*w)) {
        return false;
    }
    m_windows.push_back(std::move(w));
    return true;
}

bool SessionManager::wireAndShow(EditorWindow& w) {
    // WI-08: applied once per window, before wireNormalMode()/create() -
    // setters only touch plain member state (same reasoning
    // renderPipeline.setLanguage() below relies on), so this is safe
    // pre-attach. Moved here (was a one-time wWinMain call before this WI)
    // so a future second window (WI-20b) gets the same settings applied,
    // not just the first.
    w.renderPipeline.setFontSettings(m_settings.fontFamily, m_settings.fontSizeDips);
    w.renderPipeline.setTabWidth(m_settings.tabWidth);
    w.renderPipeline.setLineNumbersVisible(m_settings.showLineNumbers);
    w.renderPipeline.setMinimapVisible(m_settings.showMinimap);
    w.renderPipeline.setTheme(parseThemeKind(m_settings.themeName));
    // WI-21e: the freshly-constructed EditorWindow's initial session's own
    // Viewport picks this up via handlePaintEvent()'s per-frame sync (see
    // that call site's own comment) - no separate viewport push needed here.
    w.renderPipeline.setWordWrap(m_settings.wordWrap);

    w.menuHandles = buildMenuBar(m_recentFiles);

    ui::MainWindowConfig cfg{};
    cfg.menuBar = w.menuHandles.menuBar;
    // WI-20a: the "only quit once every window is gone" hook - see
    // ui::MainWindowConfig::onDestroyed's own header comment.
    cfg.onDestroyed = [this, target = &w](HWND) { onWindowDestroyed(target); };
    // WI-20b: the WM_COPYDATA handoff from a second NeoMIFES.exe launch
    // (claimSingleInstance(), launch_setup.cpp). Wired on EVERY window
    // (not just the first) since FindWindowW - the sender's own lookup -
    // can return ANY currently-open window (Z-order-dependent, not
    // creation-order-guaranteed): whichever one happens to receive it must
    // behave identically. `source`/`hwnd` (this window's own HWND) are
    // unused here - the new window always opens independently of which
    // existing window physically received the message.
    cfg.onCopyData = [this](HWND, ULONG_PTR dwData, std::wstring_view payload) {
        if (dwData != kCopyDataOpenPathId) {
            return;
        }
        std::optional<std::filesystem::path> path;
        if (!payload.empty()) {
            path = std::filesystem::path(payload);
        }
        static_cast<void>(createWindow(path));
    };

    wireNormalMode(cfg, w.window, w.renderPipeline, w.workspace, m_hInstance, w.findDialog, w.findReplaceDialog,
                   w.commandPalette, w.gotoLineBar, w.structuralViews.jsonPathBar, w.grepBar, w.grepState,
                   m_searchHistory, w.outlinePane, w.tabBar, w.statusBar, m_settings, m_settingsPath, m_keyBindings,
                   m_keyBindingsPath, m_accelTable, w.freeCursorModeEnabled, w.isDraggingMinimap, w.imeComposing,
                   m_recentFiles, w.menuHandles, m_autosave, w.structuralViews.logIndexWorker, m_userLogPatterns,
                   m_logPatternsDir, w.structuralViews.jsonTreeWorker, w.structuralViews.csvModelWorker,
                   w.structuralViews.jsonTreePane, w.structuralViews.jsonTreePanePendingSessionToken,
                   w.structuralViews.csvGridPane, w.structuralViews.csvGridPanePendingSessionToken,
                   w.structuralViews.gitDiffWorker, w.structuralViews.xmlTreeWorker,
                   w.structuralViews.jsonPathBarIsForXml, w.structuralViews.gitPane,
                   w.structuralViews.gitStatusWorker, w.structuralViews.diffViewDocument, *this);

    // Phase 7b/7d: reflect the startup document's language before the first
    // paint - attach() itself happens later inside onDeferredInit, but
    // setLanguage() only touches plain member state, so it's safe to call
    // before RenderPipeline is attached.
    w.renderPipeline.setLanguage(w.workspace.active().language());

    return w.window.create(m_hInstance, cfg);
}

void SessionManager::onWindowDestroyed(EditorWindow* w) noexcept {
    std::erase_if(m_windows, [w](const std::unique_ptr<EditorWindow>& p) { return p.get() == w; });
    if (m_windows.empty()) {
        ::PostQuitMessage(0);
    }
}

void SessionManager::persistOnExit() const {
    // Same batched-at-exit contract these two had as wWinMain locals before
    // this WI (see core::SearchHistory/core::RecentFiles' own header
    // comments on why this - unlike core::AutosaveIndex, written
    // immediately on every autosave/clear - doesn't need to happen more
    // often than once, at clean exit).
    if (m_searchHistoryPath) {
        m_searchHistory.saveTo(*m_searchHistoryPath);
    }
    if (m_recentFilesPath) {
        m_recentFiles.saveTo(*m_recentFilesPath);
    }
}

}  // namespace neomifes::app
