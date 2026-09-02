#include "neomifes/ui/find_replace_dialog.h"

#include <commctrl.h>

#include <string>

#include "neomifes/ui/find_navigation.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

// This dialog's own child-control id block - a fresh, unused range (see
// command_ids.h's own comment on the existing per-widget blocks: find_bar.cpp
// 1001-1003, command_palette.cpp 2001-2002, goto_line_bar.cpp 3001,
// grep_bar.cpp 4001-4003, outline_pane.cpp 5001, tab_bar.cpp 6001,
// status_bar.cpp 7001, menu_bar.h's kRecentFileIdBase 8001-8020). Those all
// share MainWindow's own WM_COMMAND id space; this dialog is a SEPARATE
// top-level window with its own WM_COMMAND handling, so collision is not
// actually possible - the fresh block is purely for readability/debugging.
constexpr int kFindEditId        = 9001;
constexpr int kReplaceEditId     = 9002;
constexpr int kCaseCheckId       = 9003;
constexpr int kWordCheckId       = 9004;
constexpr int kRegexCheckId      = 9005;
constexpr int kInfoLabelId       = 9006;
constexpr int kFindNextButtonId  = 9007;
constexpr int kFindPrevButtonId  = 9008;
constexpr int kReplaceButtonId   = 9009;
constexpr int kReplaceAllButtonId = 9010;

constexpr UINT_PTR kEditSubclassId  = 1;
constexpr UINT_PTR kDebounceTimerId = 1;
constexpr UINT     kDebounceMs      = 150;

// Layout constants in DIPs (96-DPI baseline, same convention as
// find_bar.cpp's own kEditWidthDips etc.) - this dialog is fixed-size and
// non-resizable (no WS_THICKFRAME), so unlike FindBar::onParentResized()
// these are only ever applied once, at create() time, scaled by the owner
// window's DPI at that moment. Deliberately does not react to
// WM_DPICHANGED (moving the dialog to a different-DPI monitor mid-session)
// - an accepted simplification for a fixed small utility window, out of
// this WI's scope.
constexpr float kMarginDips     = 10.0F;
constexpr float kLabelWidthDips = 60.0F;
constexpr float kEditWidthDips  = 300.0F;
constexpr float kRowHeightDips  = 24.0F;
constexpr float kCheckWidthDips = 130.0F;
constexpr float kButtonWidthDips = 96.0F;
constexpr float kFontSizeDips   = 14.0F;
// The dialog's own width must fit its WIDEST row - the 4-button row (5
// margins: left edge + 3 gaps between buttons + right edge), not the
// label+edit row (only 3 margins) - an earlier version used the narrower
// row's width here and silently clipped the last button ("すべて置換")
// past the window's right edge, caught by real-machine dogfooding.
constexpr float kButtonRowWidthDips = (5 * kMarginDips) + (4 * kButtonWidthDips);
constexpr float kLabelEditRowWidthDips = (2 * kMarginDips) + kLabelWidthDips + kEditWidthDips;
constexpr float kDialogWidthDips =
    kButtonRowWidthDips > kLabelEditRowWidthDips ? kButtonRowWidthDips : kLabelEditRowWidthDips;
constexpr float kDialogHeightDips = (5 * kRowHeightDips) + (6 * kMarginDips) + 20.0F;  // +titlebar allowance

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays)
constexpr wchar_t kWindowClassName[] = L"NeoMIFES.FindReplaceDialog";

bool ensureWindowClass(HINSTANCE hInstance) noexcept {
    static bool sRegistered = false;
    if (sRegistered) {
        return true;
    }
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &FindReplaceDialog::wndProcTrampoline;
    wc.hInstance     = hInstance;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    // Unlike MainWindow (nullptr background, owns painting entirely via
    // Direct2D), this window does no custom painting of its own - a real
    // system background brush lets DefWindowProc's WM_ERASEBKGND handling
    // paint it correctly.
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<UINT_PTR>(COLOR_BTNFACE + 1));
    wc.lpszClassName = kWindowClassName;

    const ATOM atom = ::RegisterClassExW(&wc);
    if (atom == 0) {
        const DWORD err = ::GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }
    sRegistered = true;
    return true;
}

[[nodiscard]] bool isChecked(HWND checkbox) noexcept {
    return ::SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

}  // namespace

FindReplaceDialog::~FindReplaceDialog() = default;

bool FindReplaceDialog::create(HWND owner, HINSTANCE hInstance, const FindReplaceDialogConfig& config) {
    if (!ensureWindowClass(hInstance)) {
        return false;
    }
    m_config = config;

    const auto dpiScale = static_cast<float>(::GetDpiForWindow(owner)) / 96.0F;
    const auto width    = static_cast<int>(kDialogWidthDips * dpiScale);
    const auto height   = static_cast<int>(kDialogHeightDips * dpiScale);

    // WS_EX_TOOLWINDOW: no taskbar entry, thin title bar - the conventional
    // style for a small floating utility dialog like this one. `owner`
    // (not WS_CHILD) establishes ownership, not parentage - see this
    // class's header comment.
    m_hwnd.reset(::CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClassName, L"検索と置換",
                                   WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, width,
                                   height, owner, nullptr, hInstance, this));
    if (!m_hwnd) {
        return false;
    }

    const auto marginPx = static_cast<int>(kMarginDips * dpiScale);
    const auto labelWPx = static_cast<int>(kLabelWidthDips * dpiScale);
    const auto editWPx  = static_cast<int>(kEditWidthDips * dpiScale);
    const auto rowHPx   = static_cast<int>(kRowHeightDips * dpiScale);
    const auto checkWPx = static_cast<int>(kCheckWidthDips * dpiScale);
    const auto buttonWPx = static_cast<int>(kButtonWidthDips * dpiScale);
    const int  editX     = marginPx + labelWPx;

    const int row1Y = marginPx;
    const int row2Y = row1Y + rowHPx + marginPx;
    const int row3Y = row2Y + rowHPx + marginPx;
    const int row4Y = row3Y + rowHPx + marginPx;
    const int row5Y = row4Y + rowHPx + marginPx;

    HWND findLabel = ::CreateWindowExW(0, WC_STATICW, L"検索:", WS_CHILD | WS_VISIBLE | SS_LEFT, marginPx,
                                       row1Y, labelWPx, rowHPx, m_hwnd.get(), nullptr, hInstance, nullptr);
    HWND find = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, editX,
                                  row1Y, editWPx, rowHPx, m_hwnd.get(),
                                  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFindEditId)), hInstance, nullptr);
    HWND replaceLabel = ::CreateWindowExW(0, WC_STATICW, L"置換後:", WS_CHILD | WS_VISIBLE | SS_LEFT, marginPx,
                                          row2Y, labelWPx, rowHPx, m_hwnd.get(), nullptr, hInstance, nullptr);
    HWND replace = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, editX,
                                     row2Y, editWPx, rowHPx, m_hwnd.get(),
                                     reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kReplaceEditId)), hInstance,
                                     nullptr);
    HWND caseCheck = ::CreateWindowExW(
        0, WC_BUTTONW, L"大文字小文字を区別", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, marginPx, row3Y, checkWPx,
        rowHPx, m_hwnd.get(), reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kCaseCheckId)), hInstance, nullptr);
    HWND wordCheck = ::CreateWindowExW(0, WC_BUTTONW, L"単語単位", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                       marginPx + checkWPx, row3Y, checkWPx, rowHPx, m_hwnd.get(),
                                       reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kWordCheckId)), hInstance,
                                       nullptr);
    HWND regexCheck = ::CreateWindowExW(0, WC_BUTTONW, L"正規表現", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        marginPx + (2 * checkWPx), row3Y, checkWPx, rowHPx, m_hwnd.get(),
                                        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kRegexCheckId)), hInstance,
                                        nullptr);
    HWND infoLabel = ::CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_LEFT, marginPx, row4Y,
                                       editWPx + labelWPx, rowHPx, m_hwnd.get(),
                                       reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kInfoLabelId)), hInstance,
                                       nullptr);
    HWND findNextButton = ::CreateWindowExW(
        0, WC_BUTTONW, L"次を検索", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, marginPx, row5Y, buttonWPx, rowHPx,
        m_hwnd.get(), reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFindNextButtonId)), hInstance, nullptr);
    HWND findPrevButton =
        ::CreateWindowExW(0, WC_BUTTONW, L"前を検索", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          marginPx + buttonWPx + marginPx, row5Y, buttonWPx, rowHPx, m_hwnd.get(),
                          reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFindPrevButtonId)), hInstance, nullptr);
    HWND replaceButton =
        ::CreateWindowExW(0, WC_BUTTONW, L"置換", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          marginPx + (2 * (buttonWPx + marginPx)), row5Y, buttonWPx, rowHPx, m_hwnd.get(),
                          reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kReplaceButtonId)), hInstance, nullptr);
    HWND replaceAllButton =
        ::CreateWindowExW(0, WC_BUTTONW, L"すべて置換", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          marginPx + (3 * (buttonWPx + marginPx)), row5Y, buttonWPx, rowHPx, m_hwnd.get(),
                          reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kReplaceAllButtonId)), hInstance, nullptr);

    if (findLabel == nullptr || find == nullptr || replaceLabel == nullptr || replace == nullptr ||
        caseCheck == nullptr || wordCheck == nullptr || regexCheck == nullptr || infoLabel == nullptr ||
        findNextButton == nullptr || findPrevButton == nullptr || replaceButton == nullptr ||
        replaceAllButton == nullptr) {
        return false;
    }
    m_hwndFindEdit.reset(find);
    m_hwndReplaceEdit.reset(replace);
    m_hwndCaseCheck.reset(caseCheck);
    m_hwndWordCheck.reset(wordCheck);
    m_hwndRegexCheck.reset(regexCheck);
    m_hwndInfoLabel.reset(infoLabel);
    m_hwndFindNextButton.reset(findNextButton);
    m_hwndFindPrevButton.reset(findPrevButton);
    m_hwndReplaceButton.reset(replaceButton);
    m_hwndReplaceAllButton.reset(replaceAllButton);

    // Both edits share one subclass callback/dwRefData - same pattern
    // FindBar's own create() already established, handleEditSubclassMessage()
    // distinguishes them by the `hwnd` it receives.
    if (::SetWindowSubclass(m_hwndFindEdit.get(), &FindReplaceDialog::editSubclassProc, kEditSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }
    if (::SetWindowSubclass(m_hwndReplaceEdit.get(), &FindReplaceDialog::editSubclassProc, kEditSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    const auto fontHeightPx = -static_cast<int>(kFontSizeDips * dpiScale);
    // See find_bar.cpp's identical NOLINT for why DEFAULT_PITCH|FF_DONTCARE
    // is kept explicit despite both expanding to 0.
    // NOLINTNEXTLINE(misc-redundant-expression)
    constexpr int kPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    HFONT font = ::CreateFontW(fontHeightPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, kPitchAndFamily,
                               L"Segoe UI");
    if (font != nullptr) {
        m_font.reset(reinterpret_cast<HGDIOBJ>(font));
        for (HWND control : {findLabel, find, replaceLabel, replace, caseCheck, wordCheck, regexCheck, infoLabel,
                             findNextButton, findPrevButton, replaceButton, replaceAllButton}) {
            ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }
    return true;
}

void FindReplaceDialog::show(HWND owner) noexcept {
    if (!m_hwnd || !m_hwndFindEdit) {
        return;
    }
    if (!m_positionedOnce) {
        // Center over `owner`'s current window rect - a fresh dialog has no
        // prior position of its own to restore (see this method's own
        // header comment).
        RECT ownerRect{};
        RECT dialogRect{};
        if (::GetWindowRect(owner, &ownerRect) && ::GetWindowRect(m_hwnd.get(), &dialogRect)) {
            const int ownerW  = ownerRect.right - ownerRect.left;
            const int ownerH  = ownerRect.bottom - ownerRect.top;
            const int dialogW = dialogRect.right - dialogRect.left;
            const int dialogH = dialogRect.bottom - dialogRect.top;
            const int x       = ownerRect.left + ((ownerW - dialogW) / 2);
            const int y       = ownerRect.top + ((ownerH - dialogH) / 2);
            ::SetWindowPos(m_hwnd.get(), nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        m_positionedOnce = true;
    }
    ::ShowWindow(m_hwnd.get(), SW_SHOW);
    ::SetForegroundWindow(m_hwnd.get());
    ::SetFocus(m_hwndFindEdit.get());
    // Select-all: same "re-press to re-select" convention as FindBar::show().
    ::SendMessageW(m_hwndFindEdit.get(), EM_SETSEL, 0, -1);
}

void FindReplaceDialog::hide() noexcept {
    if (!m_hwnd) {
        return;
    }
    ::KillTimer(m_hwnd.get(), kDebounceTimerId);
    ::ShowWindow(m_hwnd.get(), SW_HIDE);
}

bool FindReplaceDialog::isVisible() const noexcept {
    return static_cast<bool>(m_hwnd) && ::IsWindowVisible(m_hwnd.get()) != FALSE;
}

void FindReplaceDialog::setMatchCount(std::size_t currentIndex, std::size_t count) noexcept {
    if (!m_hwndInfoLabel) {
        return;
    }
    const std::wstring label = formatMatchCountLabel(currentIndex, count);
    ::SetWindowTextW(m_hwndInfoLabel.get(), label.c_str());
}

std::u16string FindReplaceDialog::readEditText(HWND hwnd) {
    const int length = ::GetWindowTextLengthW(hwnd);
    std::wstring buffer(static_cast<std::size_t>(length), L'\0');
    if (length > 0) {
        ::GetWindowTextW(hwnd, buffer.data(), length + 1);
    }
    return std::u16string(neomifes::util::fromWstringView(buffer));
}

void FindReplaceDialog::fireQueryChanged() noexcept {
    if (m_hwnd) {
        ::KillTimer(m_hwnd.get(), kDebounceTimerId);
    }
    if (!m_config.onQueryChanged || !m_hwndFindEdit) {
        return;
    }
    m_config.onQueryChanged(readEditText(m_hwndFindEdit.get()), isChecked(m_hwndCaseCheck.get()),
                            isChecked(m_hwndWordCheck.get()), isChecked(m_hwndRegexCheck.get()));
}

void FindReplaceDialog::handleReplaceReturn(bool ctrlDown) noexcept {
    const std::u16string text = readEditText(m_hwndReplaceEdit.get());
    if (ctrlDown) {
        if (m_config.onReplaceAll) {
            m_config.onReplaceAll(text);
        }
    } else if (m_config.onReplaceCurrent) {
        m_config.onReplaceCurrent(text);
    }
}

void FindReplaceDialog::requestClose() noexcept {
    hide();
    if (m_config.onClosed) {
        m_config.onClosed();
    }
}

bool FindReplaceDialog::handleEditKeyDown(HWND hwnd, UINT vkCode) noexcept {
    // While an IME composition is active, Enter/Escape belong to the IME -
    // same rationale as FindBar::handleSubclassKeyDown().
    if (m_composing) {
        return false;
    }
    const bool ctrlDown      = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shiftDown     = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool onReplaceEdit = m_hwndReplaceEdit && (hwnd == m_hwndReplaceEdit.get());

    switch (vkCode) {
        case VK_RETURN:
            if (onReplaceEdit) {
                handleReplaceReturn(ctrlDown);
            } else if (shiftDown) {
                if (m_config.onFindPrevious) {
                    m_config.onFindPrevious();
                }
            } else if (m_config.onFindNext) {
                m_config.onFindNext();
            }
            return true;
        case VK_ESCAPE:
            requestClose();
            return true;
        default:
            return false;
    }
}

void FindReplaceDialog::handleCommand(WPARAM wParam) noexcept {
    const WORD id     = LOWORD(wParam);
    const WORD notify = HIWORD(wParam);
    switch (id) {
        case kFindEditId:
            if (notify == EN_CHANGE && m_hwnd) {
                // Debounced (same FindBar convention): rapid keystrokes each
                // restart the timer, so onQueryChanged only fires once the
                // user pauses for kDebounceMs.
                ::KillTimer(m_hwnd.get(), kDebounceTimerId);
                ::SetTimer(m_hwnd.get(), kDebounceTimerId, kDebounceMs, nullptr);
            }
            return;
        case kCaseCheckId:
        case kWordCheckId:
        case kRegexCheckId:
            if (notify == BN_CLICKED) {
                // A checkbox click is a single discrete event, not a
                // keystroke burst - fire immediately rather than waiting
                // for the debounce (BM_GETCHECK inside fireQueryChanged()
                // already reflects BS_AUTOCHECKBOX's own state flip, which
                // Windows applies before this notification is sent).
                fireQueryChanged();
            }
            return;
        case kFindNextButtonId:
            if (notify == BN_CLICKED && m_config.onFindNext) {
                m_config.onFindNext();
            }
            return;
        case kFindPrevButtonId:
            if (notify == BN_CLICKED && m_config.onFindPrevious) {
                m_config.onFindPrevious();
            }
            return;
        case kReplaceButtonId:
            if (notify == BN_CLICKED) {
                handleReplaceReturn(/*ctrlDown=*/false);
            }
            return;
        case kReplaceAllButtonId:
            if (notify == BN_CLICKED) {
                handleReplaceReturn(/*ctrlDown=*/true);
            }
            return;
        default:
            return;
    }
}

LRESULT FindReplaceDialog::handleEditSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
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
            // Standard comctl32 subclassing hygiene - see FindBar's
            // identical handling for why this is needed despite
            // DestroyWindow() removing the subclass automatically anyway.
            ::RemoveWindowSubclass(hwnd, &FindReplaceDialog::editSubclassProc, kEditSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FindReplaceDialog::editSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                     UINT_PTR /*subclassId*/, DWORD_PTR refData) noexcept {
    auto* self = reinterpret_cast<FindReplaceDialog*>(refData);
    if (self == nullptr) {
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return self->handleEditSubclassMessage(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FindReplaceDialog::wndProcTrampoline(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    FindReplaceDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self     = static_cast<FindReplaceDialog*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<FindReplaceDialog*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr) {
        return self->wndProc(hwnd, msg, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT FindReplaceDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
        case WM_COMMAND:
            handleCommand(wParam);
            return 0;
        case WM_TIMER:
            if (wParam == kDebounceTimerId) {
                fireQueryChanged();
                return 0;
            }
            break;
        case WM_CLOSE:
            // Hide, don't destroy - same "modeless dialog survives across
            // show/hide cycles" convention every other overlay in this
            // codebase (FindBar/GrepBar/etc.) already follows, just reached
            // via a real WM_CLOSE (title bar close button/Alt+F4/system
            // menu) instead of Escape/a config callback.
            requestClose();
            return 0;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace neomifes::ui
