#pragma once
#include <windows.h>
#include <msctf.h>
// LLVM-MinGW does not yet declare the Ex interface. This declaration follows
// Microsoft's published SDK ABI; MSVC uses the SDK's own declaration.
// https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/msctf.h
#ifndef __ITfTextInputProcessorEx_INTERFACE_DEFINED__
#define __ITfTextInputProcessorEx_INTERFACE_DEFINED__
inline constexpr GUID IID_ITfTextInputProcessorEx = {0x6e4e2102,0xf9cd,0x433d,{0xb4,0x96,0x30,0x3c,0xe0,0x3a,0x65,0x07}};
MIDL_INTERFACE("6e4e2102-f9cd-433d-b496-303ce03a6507")
ITfTextInputProcessorEx : public ITfTextInputProcessor {
    virtual HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr*, TfClientId, DWORD) = 0;
};
#endif
#ifndef TF_CLIENTID_NULL
#define TF_CLIENTID_NULL ((TfClientId)0)
#endif
#ifndef TF_IPPMF_FORPROCESS
#define TF_IPPMF_FORPROCESS 0x10000000
#define TF_IPPMF_FORSESSION 0x20000000
#define TF_IPPMF_ENABLEPROFILE 0x00000001
#endif
