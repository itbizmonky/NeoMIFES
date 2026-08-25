#pragma once

// GitPane - the "changed files" panel's WC_LISTVIEW child control (WI-17e,
// Phase 11.1). Win32-mechanics-only, knows nothing about neomifes::git - see
// ui::OutlinePane's own class comment for why (the same "ui:: never depends
// on the domain module that produces its data" principle this codebase
// already established for OutlinePane/syntax:: and JsonTreePane/jsontree::).
//
// 260dip right-docked strip (ui::OutlinePane's own layout template, NOT
// ui::CsvGridPane's full-client-area replacement - a two-column file list
// fits comfortably in a narrow side panel the same way a symbol outline
// does). Stays open after activating a file, same "browsing means visiting
// several entries in a row" reasoning outline_pane.h's own top comment
// gives - only Escape closes it.
//
// Plain WS_CHILD | LVS_REPORT (real items, NOT LVS_OWNERDATA) - unlike
// ui::CsvGridPane's 10-million-row virtual-mode requirement, a repository's
// changed-file count is a few hundred at most, so real LVITEM storage costs
// nothing worth optimizing away. LVS_EX_FULLROWSELECT is still required
// (not just cosmetic) - without it, NM_CLICK hit-testing only resolves a
// valid iItem when the click lands in subitem 0's own column bounds, a
// real-machine dogfooding discovery from WI-16c/WI-16f that this class
// applies preemptively rather than repeating.
//
// Single-click (NM_CLICK) activates a row and opens the file - "a list of
// files to jump to" reads closer to a recent-files menu (click = open) than
// to OutlinePane/JsonTreePane's own "browse, then jump" outline model, and
// CsvGridPane::handleClick() (WI-16f) already established NM_CLICK as this
// codebase's convention for that shape of interaction.

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "neomifes/platform/handle_guard.h"

namespace neomifes::ui {

// UI-only row shape - see this header's top comment for why ui:: does not
// depend on neomifes::git directly. `absolutePath` empty means a
// placeholder row (git_pane_bridge.h's "not a repository"/"no changes"
// text) - handleClick() no-ops on those rather than calling onFileActivated
// with a meaningless path.
struct GitPaneItem {
    std::u16string        statusGlyph;   // "M"/"A"/"D"/"R"/"U", empty for a placeholder row
    std::u16string        displayPath;   // relative path, or a placeholder's own explanatory text
    std::filesystem::path absolutePath;  // empty for a placeholder row
};

struct GitPaneConfig {
    // NM_CLICK on a real (non-placeholder) row.
    std::function<void(std::filesystem::path absolutePath)> onFileActivated;
    // Escape. Caller restores focus to the document (same contract as
    // OutlinePaneConfig::onClosed/JsonTreePaneConfig::onClosed).
    std::function<void()> onClosed;
};

class GitPane {
public:
    GitPane()  = default;
    ~GitPane() = default;

    GitPane(const GitPane&)            = delete;
    GitPane& operator=(const GitPane&) = delete;
    GitPane(GitPane&&)                 = delete;
    GitPane& operator=(GitPane&&)      = delete;

    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const GitPaneConfig& config);

    // Replaces the list contents and shows the panel. Re-invoking while
    // already visible refreshes in place (mirrors OutlinePane::showWith()'s
    // own contract).
    void showWith(std::vector<GitPaneItem> items) noexcept;
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

    void onParentResized(std::uint32_t parentWidth, std::uint32_t parentHeight, float dpiScale) noexcept;

    // Same "public static constexpr accessor" shape as
    // OutlinePane::widthDips()/JsonTreePane::widthDips() - see
    // OutlinePane::widthDips()'s own doc comment for why this duplicates the
    // .cpp's own kPanelWidthDips constant rather than sharing storage.
    [[nodiscard]] static constexpr float widthDips() noexcept { return 260.0F; }

    // Routes a WM_NOTIFY the owning MainWindow received (NM_CLICK arrives
    // here, not at the child itself - same routing OutlinePane/JsonTreePane/
    // CsvGridPane already use for their own WM_NOTIFY codes, see
    // MainWindowConfig::onNotify's doc comment). Call from
    // MainWindowConfig::onNotify.
    LRESULT handleNotify(WPARAM wParam, LPARAM lParam) noexcept;

private:
    static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId,
                                         DWORD_PTR refData) noexcept;
    LRESULT handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    void handleClick(LPARAM lParam) noexcept;
    void populateList() noexcept;
    void ensureFont(float dpiScale) noexcept;

    neomifes::platform::WindowHandle    m_hwndList;
    neomifes::platform::GdiObjectHandle m_font;
    std::vector<GitPaneItem>            m_items;
    GitPaneConfig                       m_config;
};

}  // namespace neomifes::ui
