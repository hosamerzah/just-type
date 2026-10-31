#include "tsf.hpp"
#include <string>
#include <array>
#include "build_identity.hpp"

namespace ak {
HINSTANCE instance = nullptr;
std::atomic<long> objects{0}, locks{0};
bool perUser = false;
class Factory final : public IClassFactory {
    std::atomic<ULONG> references_{1};
public:
    Factory() { ++objects; }
    ~Factory() { --objects; }
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override { auto n = --references_; if (!n) delete this; return n; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** out) override { if(!out)return E_POINTER;*out=nullptr; if (outer) return CLASS_E_NOAGGREGATION; return create_service(iid, out); }
    STDMETHODIMP LockServer(BOOL lock) override { if (lock) ++locks; else --locks; return S_OK; }
};
std::wstring guid_string(REFGUID guid) { wchar_t text[40]{}; StringFromGUID2(guid, text, 40); return text; }
HRESULT registry(bool install) {
    auto keyName = L"Software\\Classes\\CLSID\\" + guid_string(ServiceId);
    HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    if (!install) { RegDeleteTreeW(root, keyName.c_str()); return S_OK; }
    wchar_t path[32768]{}; DWORD count = GetModuleFileNameW(instance, path, 32768);
    if (!count || count == 32768) return E_FAIL;
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(root, (keyName + L"\\InprocServer32").c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    status = RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(path), (count + 1) * sizeof(wchar_t));
    const wchar_t apartment[] = L"Apartment";
    if (status == ERROR_SUCCESS) status = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(apartment), sizeof(apartment));
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(status);
}
HRESULT profiles(bool install) {
    ComPtr<ITfInputProcessorProfiles> profiles_;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(profiles_.put()));
    if (FAILED(hr)) return hr;
    if (!install) { profiles_->RemoveLanguageProfile(ServiceId, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), ProfileId); return profiles_->Unregister(ServiceId); }
    if (FAILED(hr = profiles_->Register(ServiceId))) return hr;
    wchar_t path[32768]{}; DWORD length = GetModuleFileNameW(instance, path, 32768);
    const wchar_t description[] = L"Automatic Arabic / English";
    hr = profiles_->AddLanguageProfile(ServiceId, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), ProfileId,
        description, static_cast<ULONG>(wcslen(description)), path, length, 0);
    if (SUCCEEDED(hr) && !perUser) profiles_->EnableLanguageProfileByDefault(ServiceId, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), ProfileId, TRUE);
    if (SUCCEEDED(hr)) hr = profiles_->EnableLanguageProfile(ServiceId, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), ProfileId, TRUE);
    return hr;
}
HRESULT categories(bool install) {
    ComPtr<ITfCategoryMgr> category;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(category.put()));
    if (FAILED(hr)) return hr;
    // Modern input-indicator and UI-less compatibility; no secure-mode claim.
    constexpr GUID traySupport={0x25504fb4,0x7bab,0x4bc1,{0x9c,0x69,0xcf,0x81,0x89,0x0f,0x0e,0xf5}};
    constexpr GUID immersiveSupport={0x13a016df,0x560b,0x46cd,{0x94,0x7a,0x4c,0x3a,0xf1,0xe0,0xe3,0x5d}};
    constexpr GUID uiLessSupport={0x49d2f9cf,0x1f5e,0x11d7,{0xa6,0xd3,0x00,0x06,0x5b,0x84,0x43,0x5c}};
    constexpr GUID comLessSupport={0x364215d9,0x75bc,0x11d7,{0xa6,0xef,0x00,0x06,0x5b,0x84,0x43,0x5c}};
    constexpr GUID inputModeCompartment={0xccf05dd8,0x4a87,0x11d7,{0xa6,0xe2,0x00,0x06,0x5b,0x84,0x43,0x5c}};
    constexpr GUID displayAttributeProvider={0x046b8c80,0x1647,0x40f7,{0x9b,0x21,0xb9,0x3b,0x81,0xaa,0xbc,0x1b}};
    for (const auto& id : {GUID_TFCAT_TIP_KEYBOARD,traySupport,immersiveSupport,uiLessSupport,comLessSupport,inputModeCompartment,displayAttributeProvider}) {
        if (install) hr = category->RegisterCategory(ServiceId, id, ServiceId);
        else hr = category->UnregisterCategory(ServiceId, id, ServiceId);
        if (FAILED(hr) && install) return hr;
    }
    return S_OK;
}
}
extern "C" BOOL WINAPI DllMain(HINSTANCE instance_, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { ak::instance = instance_; DisableThreadLibraryCalls(instance_); }
    return TRUE;
}
extern "C" const char* __cdecl AutoKeyboardBuildIdentity(){return AK_VERSION " " AK_SOURCE_ID;}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return ak::objects == 0 && ak::locks == 0 ? S_OK : S_FALSE; }
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (clsid != ak::ServiceId) return CLASS_E_CLASSNOTAVAILABLE;
    try { auto factory = new ak::Factory; auto hr = factory->QueryInterface(iid, out); factory->Release(); return hr; }
    catch (...) { return E_OUTOFMEMORY; }
}
extern "C" HRESULT __stdcall DllUnregisterServer() {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ak::categories(false); ak::profiles(false); auto hr = ak::registry(false);
    if (SUCCEEDED(init)) CoUninitialize();
    return hr;
}
extern "C" HRESULT __stdcall DllRegisterServer() {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT hr = ak::registry(true);
    if (SUCCEEDED(hr)) hr = ak::profiles(true);
    if (SUCCEEDED(hr)) hr = ak::categories(true);
    if (FAILED(hr)) { ak::categories(false); ak::profiles(false); ak::registry(false); }
    if (SUCCEEDED(init)) CoUninitialize();
    return hr;
}
extern "C" HRESULT __stdcall DllInstall(BOOL install, LPCWSTR command) {
    ak::perUser = command && _wcsicmp(command, L"user") == 0;
    return install ? DllRegisterServer() : DllUnregisterServer();
}
