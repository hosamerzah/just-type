#include "profile.hpp"
#include "runtime.hpp"
#include <tlhelp32.h>
#include <fstream>
#include <iostream>
#include "diagnostics.hpp"
namespace {
int trace_notepad(bool bridge) {
    // Notepad can reuse an existing process, which would not inherit tracing.
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return 1;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    bool running=false;
    if(Process32FirstW(snapshot,&entry))do {
        if(_wcsicmp(entry.szExeFile,L"Notepad.exe")==0)running=true;
    }while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    if(running) {
        std::wcerr<<L"Close all Notepad windows first, then run this launcher again.\n";
#ifdef AK_TRACE_NOTEPAD_DEFAULT
        MessageBoxW(nullptr,L"Save your work and close all Notepad windows, then open this launcher again.",L"AutoKeyboard diagnostic",MB_OK|MB_ICONINFORMATION);
#endif
        return 3;
    }
    auto path=ak::data_folder()/L"notepad-diagnostic.log";
    std::filesystem::create_directories(path.parent_path());
    {std::ofstream out(path,std::ios::trunc);out<<"AutoKeyboard diagnostic: process events and error codes only; no keys or text.\nMessage bridge: "<<(bridge?"experimental opt-in":"off; normal TSF")<<'\n';}
    ak::enable_diagnostics(path.parent_path(),bridge);
    if(bridge)SetEnvironmentVariableW(L"AUTOKEYBOARD_MESSAGE_BRIDGE",L"1");
    wchar_t system[32768]{};
    if(!GetSystemDirectoryW(system,32768))return 1;
    auto notepad=std::filesystem::path(system)/L"notepad.exe";
    std::wstring command=L"\""+notepad.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    BOOL launched=CreateProcessW(notepad.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process);
    DWORD error=launched?ERROR_SUCCESS:GetLastError();
    SetEnvironmentVariableW(L"AUTOKEYBOARD_TRACE",nullptr);
    if(bridge)SetEnvironmentVariableW(L"AUTOKEYBOARD_MESSAGE_BRIDGE",nullptr);
    if(!launched){std::error_code ignored;std::filesystem::remove(path.parent_path()/L"diagnostics.enabled",ignored);std::wcerr<<L"Notepad launch failed: "<<error<<L'\n';return 1;}
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    std::wcout<<L"Select Automatic Arabic / English in Notepad. Type hgsghl followed by Space.\nMetadata capture expires after ten minutes; newly activated hosts may contribute events.\nDiagnostic: "<<path.wstring()<<L'\n';
    return 0;
}
}
int wmain(int argc,wchar_t** argv) {
    HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(init))return 1;
    int result=0;
    {
        std::wstring command=argc>1?argv[1]:
#ifdef AK_TRACE_NOTEPAD_DEFAULT
            L"--trace-notepad";
#else
            L"--status";
#endif
        if(command==L"--preflight") {auto state=ak::desktop_preflight();std::wcout<<state.report;CoUninitialize();return state.ready?0:3;}
        if(command==L"--stop-trace"){std::error_code error;std::filesystem::remove(ak::data_folder()/L"diagnostics.enabled",error);std::filesystem::remove(ak::temp_diagnostic_folder()/L"diagnostics.enabled",error);CoUninitialize();return 0;}
        if(command==L"--trace") {
            try {
                auto path=ak::data_folder()/L"notepad-diagnostic.log";
                std::filesystem::create_directories(path.parent_path());
                {std::ofstream out(path,std::ios::trunc);out<<"AutoKeyboard diagnostic: process events and error codes only; no keys or text.\nMessage bridge: on (default)\n";}
                ak::enable_diagnostics(path.parent_path(),true);
                std::wcout<<L"Diagnostic capture enabled for 10 minutes.\nLog: "<<path.wstring()<<L'\n';
                CoUninitialize();return 0;
            } catch(const std::exception& error){std::cerr<<error.what()<<'\n';CoUninitialize();return 1;}
        }
        if(command==L"--configure-user" || command==L"--remove-user") {
            auto state=ak::desktop_preflight();if(!state.ready){std::wcout<<state.report;CoUninitialize();return 3;}
            auto hr=ak::configure_current_user(command==L"--configure-user");std::wcout<<L"User configuration: "<<ak::hresult_text(hr)<<L'\n';CoUninitialize();return SUCCEEDED(hr)?0:1;
        }
        if(command==L"--trace-notepad") {
            auto state=ak::desktop_preflight();if(!state.ready){std::wcout<<state.report;CoUninitialize();return 3;}
            try{result=trace_notepad(argc>2 && std::wstring(argv[2])==L"--bridge");}
            catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
            CoUninitialize();return result;
        }
        if(command==L"--enable" || command==L"--disable") {
            auto hr=ak::enable_keyboard(command==L"--enable");
            std::wcout<<L"Windows input-method list update: "<<ak::hresult_text(hr)<<L'\n';result=hr==S_OK?0:1;
        } else if(command==L"--activate" || command==L"--probe") {
            auto hr=ak::activate_keyboard(command==L"--activate");
            std::wcout<<L"Profile activation: "<<ak::hresult_text(hr)<<L'\n';result=hr==S_OK?0:1;
        } else if(command!=L"--status")result=2;
        std::wcout<<ak::keyboard_diagnostics();
    }
    CoUninitialize();return result;
}
