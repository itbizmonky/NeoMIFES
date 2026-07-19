#include "neomifes/ui/find_bar.h"

#include <commctrl.h>

#include <cstddef>
#include <string>

#include "neomifes/ui/find_navigation.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

constexpr int      kFindEditId      = 1001;
constexpr int      kInfoLabelId     = 1002;
constexpr UINT_PTR kSubclassId      = 1;
constexpr UINT_PTR kDebounceTimerId = 1;
constexpr UINT     kDebounceMs      = 150;

// Layout constants in DIPs (96-DPI baseline, same convention as
// render_pipeline.cpp's kFontSizeDips) - scaled to device pixels by
// onParentResized()'s dpiScale before being handed to Win32.
constexpr float kEditWidthDips  = 220.0F;
constexpr float kLabelWidthDips = 70.0F;
constexpr float kHeightDips     = 24.0F;
constexpr float kMarginDips     = 8.0F;
constexpr float kFontSizeDips   = 14.0F;

}  // namespace

bool FindBar::create(HWND parent, HINSTANCE hInstance, const FindBarConfig& config) {
    m_config = config;

    HWND edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0,
                                  10, 10, parent,
                                  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFindEditId)),
                                  hInstance, nullptr);
    if (edit == nullptr) {
        return false;
    }
    m_hwndFindEdit.reset(edit);

    HWND label = ::CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | SS_LEFT, 0, 0, 10, 10, parent,
                                   reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kInfoLabelId)),
                                   hInstance, nullptr);
    if (label == nullptr) {
        return false;
    }
    m_hwndInfoLabel.reset(label);

    if (::SetWindowSubclass(m_hwndFindEdit.get(), &FindBar::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    ensureFont(1.0F);
    return true;
}

void FindBar::show() noexcept {
    if (!m_hwndFindEdit || !m_hwndInfoLabel) {
        return;
    }
    ::ShowWindow(m_hwndFindEdit.get(), SW_SHOW);
    ::ShowWindow(m_hwndInfoLabel.get(), SW_SHOW);
    ::SetFocus(m_hwndFindEdit.get());
    // Select-all: standard Ctrl+F convention, also covers "re-press Ctrl+F
    // while already focused" since that keystroke reaches this same show()
    // call via handleSubclassKeyDown()'s Ctrl+F case.
    ::SendMessageW(m_hwndFindEdit.get(), EM_SETSEL, 0, -1);
}

void FindBar::hide() noexcept {
    if (!m_hwndFindEdit || !m_hwndInfoLabel) {
        return;
    }
    ::KillTimer(m_hwndFindEdit.get(), kDebounceTimerId);
    ::ShowWindow(m_hwndFindEdit.get(), SW_HIDE);
    ::ShowWindow(m_hwndInfoLabel.get(), SW_HIDE);
}

bool FindBar::isVisible() const noexcept {
    return static_cast<bool>(m_hwndFindEdit) && ::IsWindowVisible(m_hwndFindEdit.get()) != FALSE;
}

void FindBar::setMatchCount(std::size_t currentIndex, std::size_t count) noexcept {
    if (!m_hwndInfoLabel) {
        return;
    }
    const std::wstring label = formatMatchCountLabel(currentIndex, count);
    ::SetWindowTextW(m_hwndInfoLabel.get(), label.c_str());
}

void FindBar::onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept {
    if (!m_hwndFindEdit || !m_hwndInfoLabel) {
        return;
    }
    ensureFont(dpiScale);

    const auto editWidthPx  = static_cast<int>(kEditWidthDips * dpiScale);
    const auto labelWidthPx = static_cast<int>(kLabelWidthDips * dpiScale);
    const auto heightPx     = static_cast<int>(kHeightDips * dpiScale);
    const auto marginPx     = static_cast<int>(kMarginDips * dpiScale);

    const int totalWidthPx = editWidthPx + marginPx + labelWidthPx;
    const int startX       = static_cast<int>(parentWidth) - totalWidthPx - marginPx;
    constexpr int kTopY     = 0;
    const int y             = kTopY + marginPx;

    ::SetWindowPos(m_hwndFindEdit.get(), nullptr, startX, y, editWidthPx, heightPx,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowPos(m_hwndInfoLabel.get(), nullptr, startX + editWidthPx + marginPx, y, labelWidthPx,
                   heightPx, SWP_NOZORDER | SWP_NOACTIVATE);
}

void FindBar::handleCommand(WPARAM wParam, LPARAM /*lParam*/) noexcept {
    if (LOWORD(wParam) != kFindEditId || HIWORD(wParam) != EN_CHANGE || !m_hwndFindEdit) {
        return;
    }
    // Debounced (Phase 5b3a): rapid keystrokes each restart the timer, so
    // onQueryChanged only fires once the user pauses for kDebounceMs.
    ::KillTimer(m_hwndFindEdit.get(), kDebounceTimerId);
    ::SetTimer(m_hwndFindEdit.get(), kDebounceTimerId, kDebounceMs, nullptr);
}

void FindBar::ensureFont(float dpiScale) noexcept {
    const auto fontHeightPx = -static_cast<int>(kFontSizeDips * dpiScale);
    // DEFAULT_PITCH | FF_DONTCARE is the conventional CreateFontW idiom for
    // "no pitch/family preference" - both macros happen to expand to 0,
    // which clang-tidy's misc-redundant-expression flags as if the OR were
    // pointless; isolated on its own line (with NOLINT) and kept for
    // documentation value (pitch vs. family are conceptually distinct
    // fields) rather than collapsed to a bare 0.
    // NOLINTNEXTLINE(misc-redundant-expression)
    constexpr int kPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    HFONT font = ::CreateFontW(fontHeightPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               kPitchAndFamily, L"Segoe UI");
    if (font == nullptr) {
        return;
    }
    m_font.reset(reinterpret_cast<HGDIOBJ>(font));
    if (m_hwndFindEdit) {
        ::SendMessageW(m_hwndFindEdit.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (m_hwndInfoLabel) {
        ::SendMessageW(m_hwndInfoLabel.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void FindBar::fireQueryChanged() noexcept {
    if (m_hwndFindEdit) {
        ::KillTimer(m_hwndFindEdit.get(), kDebounceTimerId);
    }
    if (!m_config.onQueryChanged || !m_hwndFindEdit) {
        return;
    }
    const int length = ::GetWindowTextLengthW(m_hwndFindEdit.get());
    std::wstring buffer(static_cast<std::size_t>(length), L'\0');
    if (length > 0) {
        ::GetWindowTextW(m_hwndFindEdit.get(), buffer.data(), length + 1);
    }
    m_config.onQueryChanged(neomifes::util::fromWstringView(buffer), m_caseSensitive, m_wholeWord,
                            m_regex);
}

bool FindBar::handleSubclassKeyDown(UINT vkCode) noexcept {
    // While an IME composition is active, Enter/Escape belong to the IME
    // (confirm/cancel the current conversion) - intercepting them as Find
    // bar shortcuts would break Japanese/Chinese/Korean input entirely.
    if (m_composing) {
        return false;
    }
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
            if (m_config.onClosed) {
                m_config.onClosed();
            }
            return true;
        case 'F':
            if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                show();  // re-select-all when Ctrl+F is pressed while already focused
                return true;
            }
            return false;
        default:
            return false;
    }
}

void FindBar::handleSubclassSysKeyDown(UINT vkCode) noexcept {
    switch (vkCode) {
        case 'C':
            m_caseSensitive = !m_caseSensitive;
            break;
        case 'W':
            m_wholeWord = !m_wholeWord;
            break;
        case 'R':
            m_regex = !m_regex;
            break;
        default:
            return;
    }
    // A toggle is a single discrete event, not a keystroke burst - fire
    // immediately rather than waiting for the debounce.
    fireQueryChanged();
}

LRESULT FindBar::handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
        case WM_IME_STARTCOMPOSITION:
            m_composing = true;
            break;
        case WM_IME_ENDCOMPOSITION:
            m_composing = false;
            break;
        case WM_KEYDOWN:
            if (handleSubclassKeyDown(static_cast<UINT>(wParam))) {
                return 0;
            }
            break;
        case WM_SYSKEYDOWN:
            if (wParam == 'C' || wParam == 'W' || wParam == 'R') {
                handleSubclassSysKeyDown(static_cast<UINT>(wParam));
                // Suppress default Alt-key handling (would otherwise beep /
                // try to activate a nonexistent system menu) for the 3 keys
                // this class actually uses.
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == kDebounceTimerId) {
                fireQueryChanged();
                return 0;
            }
            break;
        case WM_NCDESTROY:
            // Standard comctl32 subclassing hygiene: removes the subclass
            // before the HWND becomes fully invalid. DestroyWindow() (via
            // WindowHandle's deleter) would remove it automatically anyway,
            // but Microsoft's own guidance recommends the explicit call.
            ::RemoveWindowSubclass(hwnd, &FindBar::subclassProc, kSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FindBar::subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR /*subclassId*/, DWORD_PTR refData) noexcept {
    auto* self = reinterpret_cast<FindBar*>(refData);
    if (self == nullptr) {
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return self->handleSubclassMessage(hwnd, msg, wParam, lParam);
}

}  // namespace neomifes::ui
