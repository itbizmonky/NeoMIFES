#pragma once

// TextSurfaceAccessible - minimal custom IAccessible (MSAA) wrapper for
// MainWindow's text surface (text_surface_no_screen_reader_exposure.md).
//
// NeoMIFES draws its document text directly via Direct2D onto the main
// window's own client area (no child HWND) - unlike an EDIT/RichEdit
// control, Windows provides no built-in accessible object for this, and the
// default WM_GETOBJECT response exposes it as ControlType.Custom with an
// empty Name (confirmed via UI Automation query, see the issue). The user
// chose the "minimal live-region announcement" tier over a full UI
// Automation ITextProvider/ITextRangeProvider implementation (2026-09-02;
// the latter is a large new subsystem - see the issue's "対応方針" options 1
// vs 2): screen readers are told the CURRENT LINE's content whenever the
// cursor moves to a different line, via the classic MSAA live-region
// mechanism (NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED, ...) plus
// get_accName() - the pre-UIA technique many AT products, including
// Narrator, still recognize via UI Automation's built-in MSAA bridge).
// Column-level caret tracking, range selection reading, and arbitrary
// character-by-character navigation are NOT covered by this tier - see the
// issue for the full-TextPattern alternative this deliberately isn't.
//
// Implementation: wraps a standard accessible object obtained from
// ::CreateStdAccessibleObject() and delegates every IAccessible/IDispatch
// method to it EXCEPT get_accName(), which returns the current line's text
// instead. This keeps parent/child navigation, accLocation, accFocus,
// accRole/accState, etc. exactly as Windows would already provide for a
// plain window, without reimplementing IAccessible's full semantics from
// scratch - only the one property this feature actually needs to control is
// overridden.
//
// IAccessible extends IDispatch, so implementing it means providing all of
// IUnknown+IDispatch+IAccessible's ~28 methods - most are one-line
// delegating forwards to m_inner. This is the size the interface's own
// contract demands, not a design choice to author a larger class than
// necessary (CLAUDE.md's per-class size guideline is a soft target for
// self-designed abstractions, not for a fixed OS interface).

#include <oleacc.h>
#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <string>
#include <string_view>

namespace neomifes::ui {

class TextSurfaceAccessible final : public IAccessible {
public:
    // Fails (returns an empty ComPtr) if ::CreateStdAccessibleObject()
    // fails - callers should treat that as "no accessible object available
    // this session" and fall back to DefWindowProcW for WM_GETOBJECT, same
    // as if no assistive technology ever queried at all.
    [[nodiscard]] static Microsoft::WRL::ComPtr<TextSurfaceAccessible> create(HWND hwnd) noexcept;

    // Updates the text get_accName() returns. Does NOT itself fire
    // EVENT_OBJECT_LIVEREGIONCHANGED - MainWindow owns that call (it also
    // owns the "did anything actually change since last frame" decision,
    // see MainWindow::announceCurrentLineIfChanged()'s doc comment - firing
    // this on every WM_PAINT would announce the same line dozens of times a
    // second).
    void setCurrentLineText(std::wstring_view text);

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE   AddRef() override;
    ULONG STDMETHODCALLTYPE   Release() override;

    // IDispatch - pure delegation. m_inner already implements this
    // correctly for a plain window, and nothing here needs script/Automation
    // dispatch-by-name.
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* pctinfo) override;
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo) override;
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid,
                                            DISPID* rgDispId) override;
    HRESULT STDMETHODCALLTYPE Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags,
                                     DISPPARAMS* pDispParams, VARIANT* pVarResult, EXCEPINFO* pExcepInfo,
                                     UINT* puArgErr) override;

    // IAccessible - every method delegates to m_inner except get_accName().
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** ppdispParent) override;
    HRESULT STDMETHODCALLTYPE get_accChildCount(long* pcountChildren) override;
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT varChild, IDispatch** ppdispChild) override;
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT varChild, BSTR* pszName) override;
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT varChild, BSTR* pszValue) override;
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT varChild, BSTR* pszDescription) override;
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT varChild, VARIANT* pvarRole) override;
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT varChild, VARIANT* pvarState) override;
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT varChild, BSTR* pszHelp) override;
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* pszHelpFile, VARIANT varChild, long* pidTopic) override;
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT varChild, BSTR* pszKeyboardShortcut) override;
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* pvarChild) override;
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* pvarChildren) override;
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT varChild, BSTR* pszDefaultAction) override;
    HRESULT STDMETHODCALLTYPE accSelect(long flagsSelect, VARIANT varChild) override;
    HRESULT STDMETHODCALLTYPE accLocation(long* pxLeft, long* pyTop, long* pcxWidth, long* pcyHeight,
                                          VARIANT varChild) override;
    HRESULT STDMETHODCALLTYPE accNavigate(long navDir, VARIANT varStart, VARIANT* pvarEndUpAt) override;
    HRESULT STDMETHODCALLTYPE accHitTest(long xLeft, long yTop, VARIANT* pvarChild) override;
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT varChild) override;
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT varChild, BSTR szName) override;
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT varChild, BSTR szValue) override;

private:
    explicit TextSurfaceAccessible(Microsoft::WRL::ComPtr<IAccessible> inner) noexcept;

    Microsoft::WRL::ComPtr<IAccessible> m_inner;
    std::wstring                        m_currentLineText;
    std::atomic<ULONG>                  m_refCount{1};
};

}  // namespace neomifes::ui
