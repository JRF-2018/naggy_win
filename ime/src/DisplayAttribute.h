// DisplayAttribute.h - display attributes (underlines) of the composition.
#pragma once
#include "Globals.h"

namespace naggy_win {

class CDisplayAttributeInfo : public ITfDisplayAttributeInfo {
public:
    CDisplayAttributeInfo(const GUID& guid, const TF_DISPLAYATTRIBUTE& attr, const wchar_t* desc);
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP GetGUID(GUID* pguid) override;
    STDMETHODIMP GetDescription(BSTR* pbstr) override;
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override;
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) override;
    STDMETHODIMP Reset() override;

private:
    ~CDisplayAttributeInfo();
    LONG ref_ = 1;
    GUID guid_;
    TF_DISPLAYATTRIBUTE attr_, default_;
    const wchar_t* desc_;
};

class CEnumDisplayAttributeInfo : public IEnumTfDisplayAttributeInfo {
public:
    CEnumDisplayAttributeInfo();
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Skip(ULONG ulCount) override;

private:
    ~CEnumDisplayAttributeInfo();
    LONG ref_ = 1;
    ULONG index_ = 0;
};

// Creates the attribute info for guid (or nullptr).
ITfDisplayAttributeInfo* CreateDisplayAttributeInfo(const GUID& guid);

}  // namespace naggy_win
