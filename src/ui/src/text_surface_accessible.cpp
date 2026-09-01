#include "neomifes/ui/text_surface_accessible.h"

#include <new>
#include <utility>

namespace neomifes::ui {

Microsoft::WRL::ComPtr<TextSurfaceAccessible> TextSurfaceAccessible::create(HWND hwnd) noexcept {
    Microsoft::WRL::ComPtr<IAccessible> inner;
    const HRESULT                       hr = ::CreateStdAccessibleObject(
        hwnd, OBJID_CLIENT, IID_IAccessible, reinterpret_cast<void**>(inner.GetAddressOf()));
    if (FAILED(hr) || !inner) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<TextSurfaceAccessible> result;
    try {
        // Attach() (not the raw-pointer constructor) - the object is
        // constructed with m_refCount==1 representing the one owning
        // reference this factory hands back; Attach() adopts that reference
        // without incrementing it, whereas the constructor would AddRef() a
        // second time and leak.
        result.Attach(new TextSurfaceAccessible(std::move(inner)));
    } catch (const std::bad_alloc&) {
        // Same "no accessible object available this session" fallback as
        // ::CreateStdAccessibleObject() failing above - this factory is
        // noexcept, so an allocation failure here must be turned into an
        // empty result rather than letting it terminate the process.
        return nullptr;
    }
    return result;
}

TextSurfaceAccessible::TextSurfaceAccessible(Microsoft::WRL::ComPtr<IAccessible> inner) noexcept
    : m_inner(std::move(inner)) {}

void TextSurfaceAccessible::setCurrentLineText(std::wstring_view text) {
    m_currentLineText.assign(text);
}

// --- IUnknown ---
// Raw `new`/`delete` here (not unique_ptr/shared_ptr) is the standard COM
// object lifetime contract: ownership is tracked via AddRef()/Release()
// refcounting shared across every ComPtr<> holder, not via a single C++
// owner - see create()'s comment for how the initial reference is handed
// off.

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr) {
        return E_POINTER;
    }
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDispatch) || riid == __uuidof(IAccessible)) {
        *ppvObject = static_cast<IAccessible*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE TextSurfaceAccessible::AddRef() {
    return m_refCount.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE TextSurfaceAccessible::Release() {
    const ULONG newCount = m_refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (newCount == 0) {
        delete this;
    }
    return newCount;
}

// --- IDispatch --- (delegation, except Invoke()'s accName interception below)

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::GetTypeInfoCount(UINT* pctinfo) {
    return m_inner->GetTypeInfoCount(pctinfo);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo) {
    return m_inner->GetTypeInfo(iTInfo, lcid, ppTInfo);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::GetIDsOfNames(REFIID riid, LPOLESTR* rgszNames, UINT cNames,
                                                                LCID lcid, DISPID* rgDispId) {
    return m_inner->GetIDsOfNames(riid, rgszNames, cNames, lcid, rgDispId);
}

// IAccessible is IDispatch-derived, so a caller may read accName via
// DISPID-based dynamic dispatch (this method) instead of the strongly-typed
// get_accName() vtable slot above - both are valid, documented ways to call
// an Automation interface like IAccessible. get_accName() alone does not
// cover that path: a caller going through Invoke() would otherwise silently
// reach m_inner's (unmodified, empty) name instead of this class's
// override. DISPID_ACC_NAME (oleacc.h) is the only DISPID this class needs
// to special-case - everything else keeps delegating to m_inner.
HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags,
                                                         DISPPARAMS* pDispParams, VARIANT* pVarResult,
                                                         EXCEPINFO* pExcepInfo, UINT* puArgErr) {
    if (dispIdMember == DISPID_ACC_NAME && (wFlags & DISPATCH_PROPERTYGET) != 0 && pVarResult != nullptr) {
        VARIANT varChild;
        ::VariantInit(&varChild);
        // VARIANT is a Win32/OLE Automation tagged union - see get_accName()'s
        // own NOLINT comment below for why direct .vt/.lVal/.bstrVal access
        // is unavoidable here.
        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
        if (pDispParams != nullptr && pDispParams->cArgs >= 1) {
            varChild = pDispParams->rgvarg[0];
        } else {
            varChild.vt   = VT_I4;
            varChild.lVal = CHILDID_SELF;
        }
        BSTR          name = nullptr;
        const HRESULT hr   = get_accName(varChild, &name);
        if (SUCCEEDED(hr)) {
            ::VariantInit(pVarResult);
            pVarResult->vt      = VT_BSTR;
            pVarResult->bstrVal = name;
        }
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)
        return hr;
    }
    return m_inner->Invoke(dispIdMember, riid, lcid, wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

// --- IAccessible --- (delegation, except get_accName())

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accParent(IDispatch** ppdispParent) {
    return m_inner->get_accParent(ppdispParent);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accChildCount(long* pcountChildren) {
    return m_inner->get_accChildCount(pcountChildren);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accChild(VARIANT varChild, IDispatch** ppdispChild) {
    return m_inner->get_accChild(varChild, ppdispChild);
}

// The one property this whole class exists to override - CHILDID_SELF is
// "the object itself" (the only id meaningful here; m_inner has no real
// children since it wraps a plain leaf window, so anything else falls
// through to m_inner's own, unmodified answer).
HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accName(VARIANT varChild, BSTR* pszName) {
    if (pszName == nullptr) {
        return E_POINTER;
    }
    // VARIANT is a Win32/OLE Automation tagged union - .vt/.lVal access is
    // its mandated ABI, not something this code can route through a
    // std::variant instead.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
        *pszName = ::SysAllocString(m_currentLineText.c_str());
        return (*pszName != nullptr) ? S_OK : E_OUTOFMEMORY;
    }
    return m_inner->get_accName(varChild, pszName);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accValue(VARIANT varChild, BSTR* pszValue) {
    return m_inner->get_accValue(varChild, pszValue);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accDescription(VARIANT varChild, BSTR* pszDescription) {
    return m_inner->get_accDescription(varChild, pszDescription);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accRole(VARIANT varChild, VARIANT* pvarRole) {
    return m_inner->get_accRole(varChild, pvarRole);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accState(VARIANT varChild, VARIANT* pvarState) {
    return m_inner->get_accState(varChild, pvarState);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accHelp(VARIANT varChild, BSTR* pszHelp) {
    return m_inner->get_accHelp(varChild, pszHelp);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accHelpTopic(BSTR* pszHelpFile, VARIANT varChild,
                                                                   long* pidTopic) {
    return m_inner->get_accHelpTopic(pszHelpFile, varChild, pidTopic);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accKeyboardShortcut(VARIANT              varChild,
                                                                          BSTR* pszKeyboardShortcut) {
    return m_inner->get_accKeyboardShortcut(varChild, pszKeyboardShortcut);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accFocus(VARIANT* pvarChild) {
    return m_inner->get_accFocus(pvarChild);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accSelection(VARIANT* pvarChildren) {
    return m_inner->get_accSelection(pvarChildren);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::get_accDefaultAction(VARIANT varChild, BSTR* pszDefaultAction) {
    return m_inner->get_accDefaultAction(varChild, pszDefaultAction);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::accSelect(long flagsSelect, VARIANT varChild) {
    return m_inner->accSelect(flagsSelect, varChild);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::accLocation(long* pxLeft, long* pyTop, long* pcxWidth,
                                                              long* pcyHeight, VARIANT varChild) {
    return m_inner->accLocation(pxLeft, pyTop, pcxWidth, pcyHeight, varChild);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::accNavigate(long navDir, VARIANT varStart,
                                                              VARIANT* pvarEndUpAt) {
    return m_inner->accNavigate(navDir, varStart, pvarEndUpAt);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::accHitTest(long xLeft, long yTop, VARIANT* pvarChild) {
    return m_inner->accHitTest(xLeft, yTop, pvarChild);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::accDoDefaultAction(VARIANT varChild) {
    return m_inner->accDoDefaultAction(varChild);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::put_accName(VARIANT varChild, BSTR szName) {
    return m_inner->put_accName(varChild, szName);
}

HRESULT STDMETHODCALLTYPE TextSurfaceAccessible::put_accValue(VARIANT varChild, BSTR szValue) {
    return m_inner->put_accValue(varChild, szValue);
}

}  // namespace neomifes::ui
