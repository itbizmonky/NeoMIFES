#include "neomifes/ui/command_palette.h"

#include <commctrl.h>

#include <algorithm>
#include <cstddef>
#include <string>

#include "neomifes/ui/command_palette_filter.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

constexpr int      kEditId     = 2001;
constexpr int      kListId     = 2002;
constexpr UINT_PTR kSubclassId = 1;

// Layout constants in DIPs (96-DPI baseline, same convention as
// find_bar.cpp's kFontSizeDips) - centered near the top of the parent,
// unlike FindBar's top-right positioning.
constexpr float kWidthDips      = 480.0F;
constexpr float kEditHeightDips = 32.0F;
constexpr float kListHeightDips = 200.0F;  // ~8 rows
constexpr float kTopMarginDips  = 60.0F;
constexpr float kFontSizeDips   = 14.0F;

}  // namespace

bool CommandPalette::create(HWND parent, HINSTANCE hInstance, const CommandPaletteConfig& config,
                            std::vector<CommandDescriptor> commands) {
    m_config   = config;
    m_commands = std::move(commands);

    HWND edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10,
                                  10, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kEditId)),
                                  hInstance, nullptr);
    if (edit == nullptr) {
        return false;
    }
    m_hwndEdit.reset(edit);

    HWND list = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"",
                                  WS_CHILD | LBS_NOTIFY | LBS_USETABSTOPS | WS_VSCROLL, 0, 0, 10, 10,
                                  parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kListId)),
                                  hInstance, nullptr);
    if (list == nullptr) {
        return false;
    }
    m_hwndList.reset(list);

    // Both controls share one subclass callback/dwRefData -
    // handleSubclassMessage() distinguishes them by the `hwnd` it receives
    // (same pattern as FindBar's Find/Replace edits, see find_bar.cpp).
    if (::SetWindowSubclass(m_hwndEdit.get(), &CommandPalette::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }
    if (::SetWindowSubclass(m_hwndList.get(), &CommandPalette::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    ensureFont(1.0F);
    return true;
}

void CommandPalette::show() noexcept {
    if (!m_hwndEdit || !m_hwndList) {
        return;
    }
    // Explicit refreshFilter() call below rather than relying on WM_SETTEXT
    // to trigger EN_CHANGE (that notification is meant for user-initiated
    // edits and its behavior on a programmatic SetWindowTextW is not
    // something this codebase has relied on elsewhere) - if Windows does
    // also fire EN_CHANGE here, the resulting second refreshFilter() call
    // is idempotent (rebuilds the identical unfiltered list).
    ::SetWindowTextW(m_hwndEdit.get(), L"");
    ::ShowWindow(m_hwndEdit.get(), SW_SHOW);
    ::ShowWindow(m_hwndList.get(), SW_SHOW);
    refreshFilter();
    ::SetFocus(m_hwndEdit.get());
}

void CommandPalette::hide() noexcept {
    if (!m_hwndEdit || !m_hwndList) {
        return;
    }
    ::ShowWindow(m_hwndEdit.get(), SW_HIDE);
    ::ShowWindow(m_hwndList.get(), SW_HIDE);
}

bool CommandPalette::isVisible() const noexcept {
    return static_cast<bool>(m_hwndEdit) && ::IsWindowVisible(m_hwndEdit.get()) != FALSE;
}

void CommandPalette::onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept {
    if (!m_hwndEdit || !m_hwndList) {
        return;
    }
    ensureFont(dpiScale);

    const auto widthPx      = static_cast<int>(kWidthDips * dpiScale);
    const auto editHeightPx = static_cast<int>(kEditHeightDips * dpiScale);
    const auto listHeightPx = static_cast<int>(kListHeightDips * dpiScale);
    const auto topMarginPx  = static_cast<int>(kTopMarginDips * dpiScale);

    const int startX = (static_cast<int>(parentWidth) - widthPx) / 2;
    const int editY  = topMarginPx;
    const int listY  = editY + editHeightPx;

    ::SetWindowPos(m_hwndEdit.get(), nullptr, startX, editY, widthPx, editHeightPx,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowPos(m_hwndList.get(), nullptr, startX, listY, widthPx, listHeightPx,
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

void CommandPalette::handleCommand(WPARAM wParam, LPARAM /*lParam*/) noexcept {
    const int controlId  = LOWORD(wParam);
    const int notifyCode = HIWORD(wParam);

    if (controlId == kListId && (notifyCode == LBN_SELCHANGE || notifyCode == LBN_DBLCLK)) {
        if (!m_hwndList) {
            return;
        }
        const LRESULT selection = ::SendMessageW(m_hwndList.get(), LB_GETCURSEL, 0, 0);
        if (selection == LB_ERR) {
            return;
        }
        // A mouse click changes the listbox's real selection without going
        // through moveSelection()'s keyboard path - resync from the
        // control's own state rather than trusting m_selectedIndex.
        m_selectedIndex = static_cast<std::size_t>(selection);
        if (notifyCode == LBN_DBLCLK) {
            runSelectedCommand();
        }
        return;
    }
    if (controlId == kEditId && notifyCode == EN_CHANGE) {
        refreshFilter();
    }
}

void CommandPalette::ensureFont(float dpiScale) noexcept {
    const auto fontHeightPx = -static_cast<int>(kFontSizeDips * dpiScale);
    // NOLINTNEXTLINE(misc-redundant-expression)
    constexpr int kPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    HFONT font = ::CreateFontW(fontHeightPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               kPitchAndFamily, L"Segoe UI");
    if (font == nullptr) {
        return;
    }
    m_font.reset(reinterpret_cast<HGDIOBJ>(font));
    if (m_hwndEdit) {
        ::SendMessageW(m_hwndEdit.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (m_hwndList) {
        ::SendMessageW(m_hwndList.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

std::u16string CommandPalette::readEditText(HWND hwnd) {
    const int length = ::GetWindowTextLengthW(hwnd);
    std::wstring buffer(static_cast<std::size_t>(length), L'\0');
    if (length > 0) {
        ::GetWindowTextW(hwnd, buffer.data(), length + 1);
    }
    return std::u16string(neomifes::util::fromWstringView(buffer));
}

void CommandPalette::refreshFilter() noexcept {
    if (!m_hwndEdit || !m_hwndList) {
        return;
    }
    const std::u16string query = readEditText(m_hwndEdit.get());
    m_filtered                 = filterAndRankCommands(query, m_commands);
    populateListBox();
}

void CommandPalette::populateListBox() noexcept {
    if (!m_hwndList) {
        return;
    }
    ::SendMessageW(m_hwndList.get(), LB_RESETCONTENT, 0, 0);
    for (const std::size_t commandIndex : m_filtered) {
        const CommandDescriptor& command = m_commands[commandIndex];
        const std::wstring       row = std::wstring(neomifes::util::toWstringView(command.title)) +
                                      L"\t" +
                                      std::wstring(neomifes::util::toWstringView(command.keybindingLabel));
        ::SendMessageW(m_hwndList.get(), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    }
    m_selectedIndex = 0;
    if (!m_filtered.empty()) {
        ::SendMessageW(m_hwndList.get(), LB_SETCURSEL, 0, 0);
    }
}

void CommandPalette::moveSelection(int delta) noexcept {
    if (m_filtered.empty() || !m_hwndList) {
        return;
    }
    const auto count    = static_cast<int>(m_filtered.size());
    // Clamped, not wrapped (VSCode convention: Up at the top row stays put) -
    // unlike FindBar's F3 wraparound, which navigates matches in a
    // document with no natural "edge".
    const int  newIndex = std::clamp(static_cast<int>(m_selectedIndex) + delta, 0, count - 1);
    m_selectedIndex      = static_cast<std::size_t>(newIndex);
    ::SendMessageW(m_hwndList.get(), LB_SETCURSEL, m_selectedIndex, 0);
}

void CommandPalette::runSelectedCommand() noexcept {
    if (m_selectedIndex >= m_filtered.size()) {
        return;
    }
    const std::size_t commandIndex = m_filtered[m_selectedIndex];
    if (commandIndex < m_commands.size() && m_commands[commandIndex].action) {
        m_commands[commandIndex].action();
    }
    hide();
}

bool CommandPalette::handleEditKeyDown(UINT vkCode) noexcept {
    // While an IME composition is active, Up/Down/Enter/Escape belong to
    // the IME (candidate navigation, confirm/cancel) - same "CJK IME一級市民"
    // requirement as FindBar's identical guard (find_bar.cpp).
    if (m_composing) {
        return false;
    }
    switch (vkCode) {
        case VK_UP:
            moveSelection(-1);
            return true;
        case VK_DOWN:
            moveSelection(1);
            return true;
        case VK_RETURN:
            runSelectedCommand();
            return true;
        case VK_ESCAPE:
            hide();
            if (m_config.onClosed) {
                m_config.onClosed();
            }
            return true;
        default:
            return false;
    }
}

LRESULT CommandPalette::handleListSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam,
                                                  LPARAM lParam) noexcept {
    switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            // Let the default listbox click handling run first (selects
            // the row under the cursor, and for a double-click also fires
            // LBN_DBLCLK synchronously to the parent - see handleCommand(),
            // which may already run a command and hide() this palette
            // before DefSubclassProc returns here).
            const LRESULT result = ::DefSubclassProc(hwnd, msg, wParam, lParam);
            // Standard WC_LISTBOX SetFocus()s itself inside that default
            // handling, stealing focus from the query edit - reclaim it,
            // but ONLY if still visible. A double-click's command may have
            // already hidden this palette and moved focus elsewhere (e.g.
            // FindBar::show()); reclaiming unconditionally here would steal
            // focus right back from whatever the command just opened.
            if (isVisible()) {
                ::SetFocus(m_hwndEdit.get());
            }
            return result;
        }
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hwnd, &CommandPalette::subclassProc, kSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CommandPalette::handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam,
                                              LPARAM lParam) noexcept {
    if (m_hwndList && hwnd == m_hwndList.get()) {
        return handleListSubclassMessage(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_IME_STARTCOMPOSITION:
            m_composing = true;
            break;
        case WM_IME_ENDCOMPOSITION:
            m_composing = false;
            break;
        case WM_KEYDOWN:
            if (handleEditKeyDown(static_cast<UINT>(wParam))) {
                return 0;
            }
            break;
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hwnd, &CommandPalette::subclassProc, kSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK CommandPalette::subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR /*subclassId*/, DWORD_PTR refData) noexcept {
    auto* self = reinterpret_cast<CommandPalette*>(refData);
    if (self == nullptr) {
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return self->handleSubclassMessage(hwnd, msg, wParam, lParam);
}

}  // namespace neomifes::ui
