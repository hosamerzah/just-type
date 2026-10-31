#include "runtime.hpp"
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <iostream>
#include <thread>
#include <chrono>

using namespace ak;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::wstring text(HWND window){std::wstring value(GetWindowTextLengthW(window)+1,0);int n=GetWindowTextW(window,value.data(),static_cast<int>(value.size()));value.resize(n);return value;}
struct WindowSearch { DWORD pid;HWND window=nullptr; };
BOOL CALLBACK find(HWND window,LPARAM argument){auto result=reinterpret_cast<WindowSearch*>(argument);DWORD pid=0;GetWindowThreadProcessId(window,&pid);wchar_t cls[128]{};GetClassNameW(window,cls,128);if(pid==result->pid && wcscmp(cls,tray_class().c_str())==0){result->window=window;return FALSE;}return TRUE;}
}
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    auto folder=std::filesystem::temp_directory_path()/(L"AutoKeyboard-ui-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    SetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",folder.c_str());
    PROCESS_INFORMATION process{};HWND window=nullptr;int result=1;
    try{
        check(!FindWindowW(tray_class().c_str(),nullptr),"An identical isolated settings test is already running");
        auto exe=module_folder()/L"AutoKeyboard.exe";std::wstring command=L"\""+exe.wstring()+L"\" --tray";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        check(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"Cannot start settings app");
        WindowSearch found{process.dwProcessId};
        for(int i=0;i<200 && !found.window;++i){EnumWindows(find,reinterpret_cast<LPARAM>(&found));std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        window=found.window;check(window!=nullptr,"Settings window did not initialize");
        for(int i=0;i<100 && !GetDlgItem(window,113);++i)std::this_thread::sleep_for(std::chrono::milliseconds(50));
        check(GetDlgItem(window,113)!=nullptr,"Preview control missing");
        check(GetDlgItem(window,117) && GetDlgItem(window,118),"Activation and diagnostics controls missing");
        check(SendMessageW(GetDlgItem(window,116),CB_GETCOUNT,0,0)==3,"PDF language selector missing installed packs");
        SendMessageW(window,WM_NULL,0,0);
        check(text(GetDlgItem(window,113)).find(L"السلام")!=std::wstring::npos,"Preview did not show Arabic conversion");
        std::cout<<"PASS settings app starts and previews a real dictionary conversion\n";
        SendMessageW(GetDlgItem(window,101),BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(window,WM_COMMAND,101,0);
        check(!IsWindowEnabled(GetDlgItem(window,102)),"Bank minimum did not disable");
        SendMessageW(GetDlgItem(window,100),CB_SETCURSEL,1,0);
        SendMessageW(window,WM_COMMAND,106,0);
        {Store db(folder/L"words.db",true);auto settings=db.settings();check(settings.bank_minimum==1 && settings.switch_minimum==4 && settings.mode==Mode::Automatic,"Settings controls did not save independent options");}
        std::cout<<"PASS optional bank filter and switching minimum persist independently\n";
        SendMessageW(GetDlgItem(window,112),CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(112,CBN_SELCHANGE),0);SendMessageW(window,WM_COMMAND,114,0);
        {Store db(folder/L"words.db",true);check(!db.vocabulary().preferences.empty(),"Preference button did not save");}
        std::cout<<"PASS explicit personal preference saves from the preview\n";
        PostMessageW(window,WM_COMMAND,204,0);check(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0,"Settings app did not exit");
        DWORD code=1;GetExitCodeProcess(process.hProcess,&code);check(code==0,"Settings app crashed");result=0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';if(window)PostMessageW(window,WM_COMMAND,204,0);if(process.hProcess)WaitForSingleObject(process.hProcess,5000);}
    if(process.hThread)CloseHandle(process.hThread);if(process.hProcess)CloseHandle(process.hProcess);
    SetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",nullptr);
    if(folder.parent_path()==std::filesystem::temp_directory_path())std::filesystem::remove_all(folder);
    CoUninitialize();return result;
}
