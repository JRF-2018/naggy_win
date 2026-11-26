// DisplayAttribute.cpp - display attributes (underlines) of the composition.
#include "DisplayAttribute.h"

namespace naggy_win {

namespace {
TF_DISPLAYATTRIBUTE InputAttr() {
    TF_DISPLAYATTRIBUTE a = {};
    a.crText.type = TF_CT_NONE;
    a.crBk.type = TF_CT_NONE;
    a.lsStyle = TF_LS_DOT;
    a.fBoldLine = FALSE;
    a.crLine.type = TF_CT_NONE;
    a.bAttr = TF_ATTR_INPUT;
    return a;
}
TF_DISPLAYATTRIBUTE ConvertedAttr() {
    TF_DISPLAYATTRIBUTE a = {};
    a.crText.type = TF_CT_NONE;
    a.crBk.type = TF_CT_NONE;
    a.lsStyle = TF_LS_SOLID;
    a.fBoldLine = TRUE;
    a.crLine.type = TF_CT_NONE;
    a.bAttr = TF_ATTR_TARGET_CONVERTED;
    return a;
}
}  // namespace

ITfDisplayAttributeInfo* CreateDisplayAttributeInfo(const GUID& guid) {
    if (IsEqualGUID(guid, GUID_NaggyDisplayAttributeInput))
        return new (std::nothrow) CDisplayAttributeInfo(guid, InputAttr(), L"Naggy Input");
    if (IsEqualGUID(guid, GUID_NaggyDisplayAttributeConverted))
        return new (std::nothrow) CDisplayAttributeInfo(guid, ConvertedAttr(), L"Naggy Converted");
    return nullptr;
}

CDisplayAttributeInfo::CDisplayAttributeInfo(const GUID& guid, const TF_DISPLAYATTRIBUTE& attr,
                                             const wchar_t* desc)
    : guid_(guid), attr_(attr), default_(attr), desc_(desc) {
    DllAddRef();
}
CDisplayAttributeInfo::~CDisplayAttributeInfo() { DllRelease(); }

STDMETHODIMP CDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
        *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) CDisplayAttributeInfo::AddRef() { return InterlockedIncrement(&ref_); }
STDMETHODIMP_(ULONG) CDisplayAttributeInfo::Release() {
    LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}
STDMETHODIMP CDisplayAttributeInfo::GetGUID(GUID* pguid) {
    if (!pguid) return E_INVALIDARG;
    *pguid = guid_;
    return S_OK;
}
STDMETHODIMP CDisplayAttributeInfo::GetDescription(BSTR* pbstr) {
    if (!pbstr) return E_INVALIDARG;
    *pbstr = SysAllocString(desc_);
    return *pbstr ? S_OK : E_OUTOFMEMORY;
}
STDMETHODIMP CDisplayAttributeInfo::GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) {
    if (!pda) return E_INVALIDARG;
    *pda = attr_;
    return S_OK;
}
STDMETHODIMP CDisplayAttributeInfo::SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) {
    if (!pda) return E_INVALIDARG;
    attr_ = *pda;
    return S_OK;
}
STDMETHODIMP CDisplayAttributeInfo::Reset() {
    attr_ = default_;
    return S_OK;
}

CEnumDisplayAttributeInfo::CEnumDisplayAttributeInfo() { DllAddRef(); }
CEnumDisplayAttributeInfo::~CEnumDisplayAttributeInfo() { DllRelease(); }

STDMETHODIMP CEnumDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo)) {
        *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) CEnumDisplayAttributeInfo::AddRef() { return InterlockedIncrement(&ref_); }
STDMETHODIMP_(ULONG) CEnumDisplayAttributeInfo::Release() {
    LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}
STDMETHODIMP CEnumDisplayAttributeInfo::Clone(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    CEnumDisplayAttributeInfo* e = new (std::nothrow) CEnumDisplayAttributeInfo();
    if (!e) return E_OUTOFMEMORY;
    e->index_ = index_;
    *ppEnum = e;
    return S_OK;
}
STDMETHODIMP CEnumDisplayAttributeInfo::Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo,
                                             ULONG* pcFetched) {
    static const GUID* kGuids[] = {&GUID_NaggyDisplayAttributeInput,
                                   &GUID_NaggyDisplayAttributeConverted};
    ULONG n = 0;
    while (n < ulCount && index_ < ARRAYSIZE(kGuids)) {
        rgInfo[n] = CreateDisplayAttributeInfo(*kGuids[index_]);
        if (!rgInfo[n]) break;
        ++n;
        ++index_;
    }
    if (pcFetched) *pcFetched = n;
    return n == ulCount ? S_OK : S_FALSE;
}
STDMETHODIMP CEnumDisplayAttributeInfo::Reset() {
    index_ = 0;
    return S_OK;
}
STDMETHODIMP CEnumDisplayAttributeInfo::Skip(ULONG ulCount) {
    index_ += ulCount;
    return index_ <= 2 ? S_OK : S_FALSE;
}

}  // namespace naggy_win
