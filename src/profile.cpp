#include "profile.hpp"
#include "runtime.hpp"
#include "tsf.hpp"
#include <sstream>
#include <iomanip>
#include <sddl.h>
#include "build_identity.hpp"

namespace ak {
namespace {
std::wstring process_sid(HANDLE process) {
    HANDLE token=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&token))return L"unknown";
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<BYTE> data(size);
    bool ok=GetTokenInformation(token,TokenUser,data.data(),size,&size)!=FALSE;CloseHandle(token);
    LPWSTR sid=nullptr;if(!ok || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid))return L"unknown";
    std::wstring result(sid);LocalFree(sid);return result;
}
}
DesktopPreflight desktop_preflight() {
    DesktopPreflight result;std::wostringstream report;
    auto current=process_sid(GetCurrentProcess());DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);
    DWORD shellPid=0;GetWindowThreadProcessId(GetShellWindow(),&shellPid);
    HANDLE shell=shellPid?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,shellPid):nullptr;
    auto owner=shell?process_sid(shell):L"unknown";if(shell)CloseHandle(shell);
    HDESK desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);wchar_t name[256]{};DWORD needed=0;
    if(desktop){GetUserObjectInformationW(desktop,UOI_NAME,name,sizeof(name),&needed);CloseDesktop(desktop);}
    result.ready=session!=0 && current!=L"unknown" && owner==current && _wcsicmp(name,L"Default")==0;
    report<<L"Version: "<<wide(AK_VERSION)<<L"\r\nSource: "<<wide(AK_SOURCE_ID)<<L"\r\nArchitecture: "<<sizeof(void*)*8
          <<L" bit\r\nSession: "<<session<<L"\r\nTest account SID: "<<current<<L"\r\nDesktop owner SID: "<<owner
          <<L"\r\nInput desktop: "<<name<<L"\r\nEnvironment: "<<(result.ready?L"ready":L"BLOCKED - run this tool normally from the intended user's desktop")<<L"\r\n";
    result.report=report.str();return result;
}
std::wstring hresult_text(HRESULT result) {
    std::wostringstream text; text<<L"0x"<<std::hex<<std::uppercase<<std::setw(8)<<std::setfill(L'0')<<static_cast<unsigned long>(result);
    return text.str();
}
HRESULT enable_keyboard(bool enabled) {
    // Registration and inclusion in the user's input-method switcher are two
    // distinct Windows operations. Never replace the user's other keyboards.
    HMODULE input=LoadLibraryExW(L"input.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!input)return HRESULT_FROM_WIN32(GetLastError());
    using Install=BOOL(WINAPI*)(LPCWSTR,DWORD);
    auto install=reinterpret_cast<Install>(GetProcAddress(input,"InstallLayoutOrTip"));
    HRESULT result=E_NOTIMPL;
    if(install) {
        wchar_t clsid[40]{},profile[40]{};
        StringFromGUID2(ServiceId,clsid,40);StringFromGUID2(ProfileId,profile,40);
        std::wstring list=L"0x0409:"+std::wstring(clsid)+profile;
        SetLastError(ERROR_SUCCESS);
        BOOL success=install(list.c_str(),enabled?0:1); // ILOT_UNINSTALL = 1.
        result=success?S_OK:GetLastError()?HRESULT_FROM_WIN32(GetLastError()):E_FAIL;
    }
    FreeLibrary(input);return result;
}
HRESULT configure_current_user(bool install) {
    HRESULT hr=S_OK;
    // The diagnostic controller also ships under x86/, beside the 32-bit DLL.
    // Shared packs identify the main installation root in that arrangement.
    auto app=packs_folder().parent_path()/L"AutoKeyboard.exe";
    std::error_code error;
    if(install && !std::filesystem::is_regular_file(app,error))return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if(install){
        ComPtr<ITfInputProcessorProfiles> profiles;
        hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfiles,reinterpret_cast<void**>(profiles.put()));
        if(SUCCEEDED(hr))hr=profiles->EnableLanguageProfile(ServiceId,0x0409,ProfileId,TRUE);
        if(SUCCEEDED(hr))hr=enable_keyboard(true);
        if(FAILED(hr))return hr;
    }else enable_keyboard(false);
    HKEY run=nullptr;
    auto status=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&run,nullptr);
    if(status!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(status);
    if(install){auto value=L"\""+app.wstring()+L"\" --tray";
        status=RegSetValueExW(run,L"AutoKeyboard",0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));
    }else{status=RegDeleteValueW(run,L"AutoKeyboard");if(status==ERROR_FILE_NOT_FOUND)status=ERROR_SUCCESS;}
    RegCloseKey(run);return HRESULT_FROM_WIN32(status);
}
HRESULT activate_keyboard(bool session) {
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    HRESULT hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfileMgr,reinterpret_cast<void**>(profiles.put()));
    if(FAILED(hr))return hr;
    // No ENABLEPROFILE flag here: tests can activate an existing profile without
    // writing persistent user preferences. The explicit enable action does that.
    return profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x0409,ServiceId,ProfileId,nullptr,
        session?TF_IPPMF_FORSESSION:TF_IPPMF_FORPROCESS);
}
std::wstring keyboard_diagnostics() {
    std::wostringstream report;
    report<<L"AutoKeyboard Windows activation diagnostics\r\n";
    report<<desktop_preflight().report;
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    HRESULT hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfileMgr,reinterpret_cast<void**>(profiles.put()));
    report<<L"Profile manager: "<<hresult_text(hr)<<L"\r\n";
    if(SUCCEEDED(hr)) {
        TF_INPUTPROCESSORPROFILE profile{};
        hr=profiles->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x0409,ServiceId,ProfileId,nullptr,&profile);
        report<<L"Registered profile: "<<hresult_text(hr)<<L"; flags="<<std::hex<<profile.dwFlags<<L"\r\n";
        TF_INPUTPROCESSORPROFILE active{};
        hr=profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&active);
        report<<L"Current process uses AutoKeyboard: "<<(SUCCEEDED(hr)&&IsEqualCLSID(active.clsid,ServiceId)?L"yes":L"no")<<L"\r\n";
    }
    ComPtr<ITfTextInputProcessorEx> service;
    hr=CoCreateInstance(ServiceId,nullptr,CLSCTX_INPROC_SERVER,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(service.put()));
    report<<L"Installed input DLL COM load: "<<hresult_text(hr)<<L"\r\n";
    if(HMODULE module=GetModuleHandleW(L"AutoKeyboardIME.dll")) {
        wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);
        report<<L"Loaded DLL: "<<path<<L"\r\n";
        auto identity=reinterpret_cast<const char*(__cdecl*)()>(GetProcAddress(module,"AutoKeyboardBuildIdentity"));
        report<<L"Loaded DLL build: "<<(identity?wide(identity()):L"older build without identity export")<<L"\r\n";
    }
    return report.str();
}
}
