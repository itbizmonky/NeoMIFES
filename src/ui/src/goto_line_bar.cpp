#include "neomifes/ui/goto_line_bar.h"

#include <commctrl.h>

#include <cstddef>
#include <string>

#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

constexpr int      kEditId     = 3001;
constexpr UINT_PTR kSubclassId = 1;

// Layout constants in DIPs (96-DPI baseline), centered near the top of the
// parent - same positioning family as CommandPalette, a different overlay
// from FindBar's right-aligned placement.
constexpr float kWidthDips     = 240.0F;
constexpr float kHeightDips    = 28.0F;
constexpr float kTopMarginDips = 60.0F;
constexpr float kFontSizeDips  = 14.0F;

}  // namespace

bool GotoLineBar::create(HWND parent, HINSTANCE hInstance, const GotoLineBarConfig& config) {
    m_config = config;

    HWND edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10,
                                  10, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kEditId)),
                                  hInstance, nullptr);
    if (edit == nullptr) {
        return false;
    }
    m_hwndEdit.reset(edit);

    if (::SetWindowSubclass(m_hwndEdit.get(), &GotoLineBar::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    ensureFont(1.0F);
    return true;
}

void GotoLineBar::show() noexcept {
    if (!m_hwndEdit) {
        return;
    }
    ::SetWindowTextW(m_hwndEdit.get(), L"");
    ::ShowWindow(m_hwndEdit.get(), SW_SHOW);
    ::SetFocus(m_hwndEdit.get());
}

void GotoLineBar::hide() noexcept {
    if (!m_hwndEdit) {
        return;
    }
    ::ShowWindow(m_hwndEdit.get(), SW_HIDE);
}

bool GotoLineBar::isVisible() const noexcept {
    return static_cast<bool>(m_hwndEdit) && ::IsWindowVisible(m_hwndEdit.get()) != FALSE;
}

void GotoLineBar::onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept {
    if (!m_hwndEdit) {
        return;
    }
    ensureFont(dpiScale);

    const auto widthPx      = static_cast<int>(kWidthDips * dpiScale);
    const auto heightPx     = static_cast<int>(kHeightDips * dpiScale);
    const auto topMarginPx  = static_cast<int>(kTopMarginDips * dpiScale);

    const int startX = (static_cast<int>(parentWidth) - widthPx) / 2;
    ::SetWindowPos(m_hwndEdit.get(), nullptr, startX, topMarginPx, widthPx, heightPx,
                  SWP_NOZORDER | SWP_NOACTIVATE);
}

void GotoLineBar::ensureFont(float dpiScale) noexcept {
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
}

std::u16string GotoLineBar::readEditText() const {
    if (!m_hwndEdit) {
        return {};
    }
    const int length = ::GetWindowTextLengthW(m_hwndEdit.get());
    std::wstring buffer(static_cast<std::size_t>(length), L'\0');
    if (length > 0) {
        ::GetWindowTextW(m_hwndEdit.get(), buffer.data(), length + 1);
    }
    return std::u16string(neomifes::util::fromWstringView(buffer));
}

bool GotoLineBar::handleSubclassKeyDown(UINT vkCode) noexcept {
    // While an IME composition is active, Enter/Escape belong to the IME -
    // same guard as FindBar/CommandPalette (find_bar.cpp's class comment).
    if (m_composing) {
        return false;
    }
    switch (vkCode) {
        case VK_RETURN:
            if (m_config.onSubmit) {
                m_config.onSubmit(readEditText());
            }
            return true;
        case VK_ESCAPE:
            if (m_config.onClosed) {
                m_config.onClosed();
            }
            return true;
        default:
            return false;
    }
}

LRESULT GotoLineBar::handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
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
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hwnd, &GotoLineBar::subclassProc, kSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK GotoLineBar::subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR /*subclassId*/, DWORD_PTR refData) noexcept {
    auto* self = reinterpret_cast<GotoLineBar*>(refData);
    if (self == nullptr) {
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return self->handleSubclassMessage(hwnd, msg, wParam, lParam);
}

}  // namespace neomifes::ui
