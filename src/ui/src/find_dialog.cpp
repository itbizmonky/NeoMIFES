#include "neomifes/ui/find_dialog.h"

#include <algorithm>
#include <commctrl.h>

#include <string>

#include "neomifes/ui/find_navigation.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

// This dialog's own child-control id block - a fresh, unused range distinct
// from find_replace_dialog.cpp's 9001-9010 (see that file's own comment on
// why collision isn't actually possible: separate top-level windows each
// have their own WM_COMMAND space, this is purely for debugging
// readability).
constexpr int kFindEditId       = 9101;
constexpr int kCaseCheckId      = 9102;
constexpr int kWordCheckId      = 9103;
constexpr int kRegexCheckId     = 9104;
constexpr int kInfoLabelId      = 9105;
constexpr int kFindNextButtonId = 9106;
constexpr int kFindPrevButtonId = 9107;

constexpr UINT_PTR kEditSubclassId  = 1;
constexpr UINT_PTR kDebounceTimerId = 1;
constexpr UINT     kDebounceMs      = 150;

// Layout constants in DIPs (96-DPI baseline) - same fixed-size,
// non-resizable, DPI-change-agnostic convention as find_replace_dialog.cpp
// (see its own comment for why WM_DPICHANGED is out of scope).
constexpr float kMarginDips      = 10.0F;
constexpr float kLabelWidthDips  = 60.0F;
constexpr float kEditWidthDips   = 300.0F;
constexpr float kRowHeightDips   = 24.0F;
constexpr float kCheckWidthDips  = 130.0F;
constexpr float kButtonWidthDips = 96.0F;
constexpr float kFontSizeDips    = 14.0F;
// The dialog's own width must fit its WIDEST row. With only 2 buttons (vs.
// FindReplaceDialog's 4), the button row is no longer automatically the
// widest - the 3-checkbox row (marginPx + 3*checkWidthPx, no explicit
// right margin) now exceeds it, so it must be included in this max()
// explicitly. FindReplaceDialog's own history (find_replace_dialog.cpp's
// comment on kButtonRowWidthDips) is the exact bug class this guards
// against: an earlier version there used only the narrower row's width and
// silently clipped a button past the window's right edge.
constexpr float kButtonRowWidthDips    = (3 * kMarginDips) + (2 * kButtonWidthDips);
constexpr float kLabelEditRowWidthDips = (2 * kMarginDips) + kLabelWidthDips + kEditWidthDips;
constexpr float kCheckRowWidthDips     = kMarginDips + (3 * kCheckWidthDips);
constexpr float kDialogWidthDips =
    std::max({kButtonRowWidthDips, kLabelEditRowWidthDips, kCheckRowWidthDips});
constexpr float kDialogHeightDips = (4 * kRowHeightDips) + (5 * kMarginDips) + 20.0F;  // +titlebar allowance

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays)
constexpr wchar_t kWindowClassName[] = L"NeoMIFES.FindDialog";

bool ensureWindowClass(HINSTANCE hInstance) noexcept {
    static bool sRegistered = false;
    if (sRegistered) {
        return true;
    }
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &FindDialog::wndProcTrampoline;
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

FindDialog::~FindDialog() = default;

bool FindDialog::create(HWND owner, HINSTANCE hInstance, const FindDialogConfig& config) {
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
    m_hwnd.reset(::CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClassName, L"検索", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                   CW_USEDEFAULT, CW_USEDEFAULT, width, height, owner, nullptr, hInstance, this));
    if (!m_hwnd) {
        return false;
    }

    const auto marginPx  = static_cast<int>(kMarginDips * dpiScale);
    const auto labelWPx  = static_cast<int>(kLabelWidthDips * dpiScale);
    const auto editWPx   = static_cast<int>(kEditWidthDips * dpiScale);
    const auto rowHPx    = static_cast<int>(kRowHeightDips * dpiScale);
    const auto checkWPx  = static_cast<int>(kCheckWidthDips * dpiScale);
    const auto buttonWPx = static_cast<int>(kButtonWidthDips * dpiScale);
    const int  editX     = marginPx + labelWPx;

    const int row1Y = marginPx;
    const int row2Y = row1Y + rowHPx + marginPx;
    const int row3Y = row2Y + rowHPx + marginPx;
    const int row4Y = row3Y + rowHPx + marginPx;

    HWND findLabel = ::CreateWindowExW(0, WC_STATICW, L"検索:", WS_CHILD | WS_VISIBLE | SS_LEFT, marginPx, row1Y,
                                       labelWPx, rowHPx, m_hwnd.get(), nullptr, hInstance, nullptr);
    HWND find = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, editX,
                                  row1Y, editWPx, rowHPx, m_hwnd.get(),
                                  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFindEditId)), hInstance, nullptr);
    HWND caseCheck = ::CreateWindowExW(
        0, WC_BUTTONW, L"大文字小文字を区別", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, marginPx, row2Y, checkWPx,
        rowHPx, m_hwnd.get(), reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kCaseCheckId)), hInstance, nullptr);
    HWND wordCheck = ::CreateWindowExW(0, WC_BUTTONW, L"単語単位", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                       marginPx + checkWPx, row2Y, checkWPx, rowHPx, m_hwnd.get(),
                                       reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kWordCheckId)), hInstance,
                                       nullptr);
    HWND regexCheck = ::CreateWindowExW(0, WC_BUTTONW, L"正規表現", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        marginPx + (2 * checkWPx), row2Y, checkWPx, rowHPx, m_hwnd.get(),
                                        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kRegexCheckId)), hInstance,
                                        nullptr);
    HWND infoLabel = ::CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_LEFT, marginPx, row3Y,
                                       editWPx + labelWPx, rowHPx, m_hwnd.get(),
                                       reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kInfoLabelId)), hInstance,
                                       nullptr);
    HWND findNextButton = ::CreateWindowExW(
        0, WC_BUTTONW, L"次を検索", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, marginPx, row4Y, buttonWPx, rowHPx,
        m_hwnd.get(), reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFindNextButtonId)), hInstance, nullptr);
    HWND findPrevButton =
        ::CreateWindowExW(0, WC_BUTTONW, L"前を検索", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          marginPx + buttonWPx + marginPx, row4Y, buttonWPx, rowHPx, m_hwnd.get(),
                          reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFindPrevButtonId)), hInstance, nullptr);

    if (findLabel == nullptr || find == nullptr || caseCheck == nullptr || wordCheck == nullptr ||
        regexCheck == nullptr || infoLabel == nullptr || findNextButton == nullptr || findPrevButton == nullptr) {
        return false;
    }
    m_hwndFindEdit.reset(find);
    m_hwndCaseCheck.reset(caseCheck);
    m_hwndWordCheck.reset(wordCheck);
    m_hwndRegexCheck.reset(regexCheck);
    m_hwndInfoLabel.reset(infoLabel);
    m_hwndFindNextButton.reset(findNextButton);
    m_hwndFindPrevButton.reset(findPrevButton);

    if (::SetWindowSubclass(m_hwndFindEdit.get(), &FindDialog::editSubclassProc, kEditSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    const auto fontHeightPx = -static_cast<int>(kFontSizeDips * dpiScale);
    // See find_replace_dialog.cpp's identical NOLINT for why
    // DEFAULT_PITCH|FF_DONTCARE is kept explicit despite both expanding to 0.
    // NOLINTNEXTLINE(misc-redundant-expression)
    constexpr int kPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    HFONT font = ::CreateFontW(fontHeightPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, kPitchAndFamily,
                               L"Segoe UI");
    if (font != nullptr) {
        m_font.reset(reinterpret_cast<HGDIOBJ>(font));
        for (HWND control :
            {findLabel, find, caseCheck, wordCheck, regexCheck, infoLabel, findNextButton, findPrevButton}) {
            ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }
    return true;
}

void FindDialog::show(HWND owner) noexcept {
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
    // Select-all: same "re-press to re-select" convention as
    // FindReplaceDialog::show().
    ::SendMessageW(m_hwndFindEdit.get(), EM_SETSEL, 0, -1);
}

void FindDialog::hide() noexcept {
    if (!m_hwnd) {
        return;
    }
    ::KillTimer(m_hwnd.get(), kDebounceTimerId);
    ::ShowWindow(m_hwnd.get(), SW_HIDE);
}

bool FindDialog::isVisible() const noexcept {
    return static_cast<bool>(m_hwnd) && ::IsWindowVisible(m_hwnd.get()) != FALSE;
}

void FindDialog::setMatchCount(std::size_t currentIndex, std::size_t count) noexcept {
    if (!m_hwndInfoLabel) {
        return;
    }
    const std::wstring label = formatMatchCountLabel(currentIndex, count);
    ::SetWindowTextW(m_hwndInfoLabel.get(), label.c_str());
}

void FindDialog::setQueryText(std::u16string_view text) noexcept {
    if (!m_hwndFindEdit) {
        return;
    }
    // Owning std::wstring (not just toWstringView()'s view): SetWindowTextW
    // needs a null terminator, which an arbitrary substring view is not
    // guaranteed to have.
    const std::wstring wide(neomifes::util::toWstringView(text));
    ::SetWindowTextW(m_hwndFindEdit.get(), wide.c_str());
    const auto caretPos = static_cast<LPARAM>(wide.size());
    ::SendMessageW(m_hwndFindEdit.get(), EM_SETSEL, caretPos, caretPos);
}

std::u16string FindDialog::readEditText(HWND hwnd) {
    const int length = ::GetWindowTextLengthW(hwnd);
    std::wstring buffer(static_cast<std::size_t>(length), L'\0');
    if (length > 0) {
        ::GetWindowTextW(hwnd, buffer.data(), length + 1);
    }
    return std::u16string(neomifes::util::fromWstringView(buffer));
}

void FindDialog::fireQueryChanged() noexcept {
    if (m_hwnd) {
        ::KillTimer(m_hwnd.get(), kDebounceTimerId);
    }
    if (!m_config.onQueryChanged || !m_hwndFindEdit) {
        return;
    }
    m_config.onQueryChanged(readEditText(m_hwndFindEdit.get()), isChecked(m_hwndCaseCheck.get()),
                            isChecked(m_hwndWordCheck.get()), isChecked(m_hwndRegexCheck.get()));
}

void FindDialog::requestClose() noexcept {
    hide();
    if (m_config.onClosed) {
        m_config.onClosed();
    }
}

bool FindDialog::handleEditKeyDown(HWND hwnd, UINT vkCode) noexcept {
    // While an IME composition is active, Enter/Escape belong to the IME -
    // same rationale as FindBar::handleSubclassKeyDown()/
    // FindReplaceDialog::handleEditKeyDown().
    if (m_composing) {
        return false;
    }
    const bool ctrlDown  = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;

    switch (vkCode) {
        case VK_RETURN:
        case VK_F3:
            if (shiftDown) {
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
        case 'F':
            // This dialog's own find edit already has focus - re-pressing
            // Ctrl+F re-selects rather than doing nothing, same convention
            // FindBar::handleSubclassKeyDown() established. GW_OWNER (not a
            // stored member) matches show()'s own owner-rect lookup: this
            // dialog only ever has one owner, MainWindow, for its lifetime.
            if (ctrlDown) {
                show(::GetWindow(m_hwnd.get(), GW_OWNER));
                return true;
            }
            return false;
        // Ported from FindBarConfig::onReplaceRequested - see this class's
        // FindDialogConfig::onReplaceRequested doc comment for why Ctrl+H
        // still needs interception here.
        case 'H':
            if (ctrlDown) {
                if (m_config.onReplaceRequested) {
                    m_config.onReplaceRequested();
                }
                return true;
            }
            return false;
        case VK_UP:
            if (ctrlDown && m_config.onHistoryOlder) {
                m_config.onHistoryOlder(readEditText(hwnd));
                return true;
            }
            return false;
        case VK_DOWN:
            if (ctrlDown && m_config.onHistoryNewer) {
                m_config.onHistoryNewer(readEditText(hwnd));
                return true;
            }
            return false;
        default:
            return false;
    }
}

void FindDialog::handleCommand(WPARAM wParam) noexcept {
    const WORD id     = LOWORD(wParam);
    const WORD notify = HIWORD(wParam);
    switch (id) {
        case kFindEditId:
            if (notify == EN_CHANGE && m_hwnd) {
                // Debounced (same FindReplaceDialog convention):
                // rapid keystrokes each restart the timer, so
                // onQueryChanged only fires once the user pauses for
                // kDebounceMs.
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
        default:
            return;
    }
}

LRESULT FindDialog::handleEditSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
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
            // Standard comctl32 subclassing hygiene - see
            // FindReplaceDialog's identical handling for why this is
            // needed despite DestroyWindow() removing the subclass
            // automatically anyway.
            ::RemoveWindowSubclass(hwnd, &FindDialog::editSubclassProc, kEditSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FindDialog::editSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR /*subclassId*/, DWORD_PTR refData) noexcept {
    auto* self = reinterpret_cast<FindDialog*>(refData);
    if (self == nullptr) {
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return self->handleEditSubclassMessage(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FindDialog::wndProcTrampoline(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    FindDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self     = static_cast<FindDialog*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<FindDialog*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr) {
        return self->wndProc(hwnd, msg, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT FindDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
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
            // codebase (FindReplaceDialog/GrepBar/etc.) already follows,
            // just reached via a real WM_CLOSE (title bar close button/
            // Alt+F4/system menu) instead of Escape/a config callback.
            requestClose();
            return 0;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace neomifes::ui
