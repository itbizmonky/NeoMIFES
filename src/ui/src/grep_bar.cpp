#include "neomifes/ui/grep_bar.h"

#include <commctrl.h>

#include <algorithm>
#include <cstddef>
#include <string>

#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

constexpr int      kQueryEditId  = 4001;
constexpr int      kFolderEditId = 4002;
constexpr int      kListId       = 4003;
constexpr UINT_PTR kSubclassId   = 1;

// Layout constants in DIPs (96-DPI baseline, same convention as
// find_bar.cpp/command_palette.cpp) - centered near the top of the parent
// like CommandPalette, but with an extra edit row (folder path) above the
// results list.
constexpr float kWidthDips      = 480.0F;
constexpr float kEditHeightDips = 28.0F;
constexpr float kListHeightDips = 240.0F;
constexpr float kTopMarginDips  = 60.0F;
constexpr float kMarginDips     = 4.0F;
constexpr float kFontSizeDips   = 14.0F;

}  // namespace

bool GrepBar::create(HWND parent, HINSTANCE hInstance, const GrepBarConfig& config) {
    m_config = config;

    HWND queryEdit = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0,
                                       10, 10, parent,
                                       reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kQueryEditId)),
                                       hInstance, nullptr);
    if (queryEdit == nullptr) {
        return false;
    }
    m_hwndQueryEdit.reset(queryEdit);

    HWND folderEdit = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0,
                                        10, 10, parent,
                                        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFolderEditId)),
                                        hInstance, nullptr);
    if (folderEdit == nullptr) {
        return false;
    }
    m_hwndFolderEdit.reset(folderEdit);

    HWND list = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW,
                                  L"", WS_CHILD | LBS_NOTIFY | LBS_USETABSTOPS | WS_VSCROLL, 0, 0, 10,
                                  10, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kListId)),
                                  hInstance, nullptr);
    if (list == nullptr) {
        return false;
    }
    m_hwndList.reset(list);

    // All three controls share one subclass callback/dwRefData -
    // handleSubclassMessage() distinguishes them by the `hwnd` it receives
    // (same pattern as FindBar's two edits / CommandPalette's edit+list).
    if (::SetWindowSubclass(m_hwndQueryEdit.get(), &GrepBar::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }
    if (::SetWindowSubclass(m_hwndFolderEdit.get(), &GrepBar::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }
    if (::SetWindowSubclass(m_hwndList.get(), &GrepBar::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    ensureFont(1.0F);
    return true;
}

void GrepBar::show() noexcept {
    if (!m_hwndQueryEdit || !m_hwndFolderEdit || !m_hwndList) {
        return;
    }
    ::ShowWindow(m_hwndQueryEdit.get(), SW_SHOW);
    ::ShowWindow(m_hwndFolderEdit.get(), SW_SHOW);
    ::ShowWindow(m_hwndList.get(), SW_SHOW);
    ::SetFocus(m_hwndQueryEdit.get());
    // Select-all, same "re-press reselects" convention as FindBar::show().
    ::SendMessageW(m_hwndQueryEdit.get(), EM_SETSEL, 0, -1);
}

void GrepBar::hide() noexcept {
    if (!m_hwndQueryEdit || !m_hwndFolderEdit || !m_hwndList) {
        return;
    }
    ::ShowWindow(m_hwndQueryEdit.get(), SW_HIDE);
    ::ShowWindow(m_hwndFolderEdit.get(), SW_HIDE);
    ::ShowWindow(m_hwndList.get(), SW_HIDE);
}

bool GrepBar::isVisible() const noexcept {
    return static_cast<bool>(m_hwndQueryEdit) && ::IsWindowVisible(m_hwndQueryEdit.get()) != FALSE;
}

void GrepBar::setResults(const std::vector<std::u16string>& rows) noexcept {
    if (!m_hwndList) {
        return;
    }
    ::SendMessageW(m_hwndList.get(), LB_RESETCONTENT, 0, 0);
    for (const auto& row : rows) {
        const std::wstring wideRow(neomifes::util::toWstringView(row));
        ::SendMessageW(m_hwndList.get(), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wideRow.c_str()));
    }
    m_resultCount   = rows.size();
    m_selectedIndex = 0;
    if (!rows.empty()) {
        ::SendMessageW(m_hwndList.get(), LB_SETCURSEL, 0, 0);
    }
}

void GrepBar::onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept {
    if (!m_hwndQueryEdit || !m_hwndFolderEdit || !m_hwndList) {
        return;
    }
    ensureFont(dpiScale);

    const auto widthPx      = static_cast<int>(kWidthDips * dpiScale);
    const auto editHeightPx = static_cast<int>(kEditHeightDips * dpiScale);
    const auto listHeightPx = static_cast<int>(kListHeightDips * dpiScale);
    const auto topMarginPx  = static_cast<int>(kTopMarginDips * dpiScale);
    const auto marginPx     = static_cast<int>(kMarginDips * dpiScale);

    const int startX  = (static_cast<int>(parentWidth) - widthPx) / 2;
    const int queryY  = topMarginPx;
    const int folderY = queryY + editHeightPx + marginPx;
    const int listY   = folderY + editHeightPx + marginPx;

    ::SetWindowPos(m_hwndQueryEdit.get(), nullptr, startX, queryY, widthPx, editHeightPx,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowPos(m_hwndFolderEdit.get(), nullptr, startX, folderY, widthPx, editHeightPx,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowPos(m_hwndList.get(), nullptr, startX, listY, widthPx, listHeightPx,
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

void GrepBar::handleCommand(WPARAM wParam, LPARAM /*lParam*/) noexcept {
    const int controlId  = LOWORD(wParam);
    const int notifyCode = HIWORD(wParam);
    if (controlId != kListId || !m_hwndList) {
        return;
    }
    if (notifyCode != LBN_SELCHANGE && notifyCode != LBN_DBLCLK) {
        return;
    }
    const LRESULT selection = ::SendMessageW(m_hwndList.get(), LB_GETCURSEL, 0, 0);
    if (selection == LB_ERR) {
        return;
    }
    // A mouse click changes the listbox's real selection without going
    // through moveSelection()'s keyboard path - resync from the control's
    // own state rather than trusting m_selectedIndex.
    m_selectedIndex = static_cast<std::size_t>(selection);
    if (notifyCode == LBN_DBLCLK) {
        activateSelectedResult();
    }
}

void GrepBar::ensureFont(float dpiScale) noexcept {
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
    if (m_hwndQueryEdit) {
        ::SendMessageW(m_hwndQueryEdit.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (m_hwndFolderEdit) {
        ::SendMessageW(m_hwndFolderEdit.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (m_hwndList) {
        ::SendMessageW(m_hwndList.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

std::u16string GrepBar::readEditText(HWND hwnd) {
    const int length = ::GetWindowTextLengthW(hwnd);
    std::wstring buffer(static_cast<std::size_t>(length), L'\0');
    if (length > 0) {
        ::GetWindowTextW(hwnd, buffer.data(), length + 1);
    }
    return std::u16string(neomifes::util::fromWstringView(buffer));
}

void GrepBar::fireRunQuery() noexcept {
    if (!m_config.onRunQuery || !m_hwndQueryEdit || !m_hwndFolderEdit) {
        return;
    }
    m_config.onRunQuery(readEditText(m_hwndQueryEdit.get()), readEditText(m_hwndFolderEdit.get()));
}

void GrepBar::activateSelectedResult() noexcept {
    if (m_selectedIndex >= m_resultCount) {
        return;
    }
    if (m_config.onResultActivated) {
        m_config.onResultActivated(m_selectedIndex);
    }
    hide();
}

void GrepBar::cycleFocus(HWND hwnd) noexcept {
    if (!m_hwndQueryEdit || !m_hwndFolderEdit) {
        return;
    }
    const bool onFolderEdit = (hwnd == m_hwndFolderEdit.get());
    ::SetFocus(onFolderEdit ? m_hwndQueryEdit.get() : m_hwndFolderEdit.get());
}

void GrepBar::moveSelection(int delta) noexcept {
    if (m_resultCount == 0 || !m_hwndList) {
        return;
    }
    const auto count = static_cast<int>(m_resultCount);
    // Clamped, not wrapped - same CommandPalette convention (Up at the top
    // row stays put).
    const int newIndex = std::clamp(static_cast<int>(m_selectedIndex) + delta, 0, count - 1);
    m_selectedIndex     = static_cast<std::size_t>(newIndex);
    ::SendMessageW(m_hwndList.get(), LB_SETCURSEL, m_selectedIndex, 0);
}

bool GrepBar::handleEditKeyDown(HWND hwnd, UINT vkCode) noexcept {
    // While an IME composition is active, Up/Down/Enter/Escape belong to the
    // IME (candidate navigation, confirm/cancel) - same guard as FindBar/
    // CommandPalette.
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
            fireRunQuery();
            return true;
        case VK_ESCAPE:
            hide();
            if (m_config.onClosed) {
                m_config.onClosed();
            }
            return true;
        case VK_TAB:
            cycleFocus(hwnd);
            return true;
        default:
            return false;
    }
}

LRESULT GrepBar::handleListSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            // Let the default listbox click handling run first (selects the
            // row under the cursor, and for a double-click also fires
            // LBN_DBLCLK synchronously to the parent - see handleCommand(),
            // which may already run activateSelectedResult() and hide()
            // this bar before DefSubclassProc returns here).
            const LRESULT result = ::DefSubclassProc(hwnd, msg, wParam, lParam);
            // Standard WC_LISTBOX SetFocus()s itself inside that default
            // handling, stealing focus from the query edit - reclaim it,
            // but ONLY if still visible (same subtlety as CommandPalette's
            // identical listbox subclass - a double-click's jump may have
            // already hidden this bar and moved focus elsewhere).
            if (isVisible()) {
                ::SetFocus(m_hwndQueryEdit.get());
            }
            return result;
        }
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hwnd, &GrepBar::subclassProc, kSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT GrepBar::handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
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
            if (handleEditKeyDown(hwnd, static_cast<UINT>(wParam))) {
                return 0;
            }
            break;
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hwnd, &GrepBar::subclassProc, kSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK GrepBar::subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR /*subclassId*/, DWORD_PTR refData) noexcept {
    auto* self = reinterpret_cast<GrepBar*>(refData);
    if (self == nullptr) {
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return self->handleSubclassMessage(hwnd, msg, wParam, lParam);
}

}  // namespace neomifes::ui
