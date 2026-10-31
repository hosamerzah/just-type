#include "profile.hpp"
#include "tsf.hpp"
#include "build_identity.hpp"
#include <richedit.h>
#include <fstream>

namespace {
HWND editor=nullptr,label=nullptr;
bool bridge=false;
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_CREATE){
        CreateWindowW(L"STATIC",L"Select Automatic Arabic / English with Win + Space. Click the editor and physically type hgsghl, Space, hello, Space. Then click Verify. This checks this host only.",WS_CHILD|WS_VISIBLE,16,14,740,55,window,nullptr,nullptr,nullptr);
        editor=CreateWindowExW(WS_EX_CLIENTEDGE,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|WS_VSCROLL,16,78,740,160,window,reinterpret_cast<HMENU>(100),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Verify typed result",WS_CHILD|WS_VISIBLE|WS_TABSTOP,16,250,180,32,window,reinterpret_cast<HMENU>(101),nullptr,nullptr);
        label=CreateWindowW(L"STATIC",L"Awaiting physical typing. Nothing is saved until Verify.",WS_CHILD|WS_VISIBLE,16,295,740,85,window,nullptr,nullptr,nullptr);
        SetTimer(window,1,500,nullptr);return 0;
    }
    if(message==WM_TIMER){
        BOOL thread=FALSE;ak::ComPtr<ITfThreadMgr> manager;
        if(SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_ITfThreadMgr,reinterpret_cast<void**>(manager.put()))))manager->IsThreadFocus(&thread);
        std::wstring title=L"AutoKeyboard "+ak::wide(AK_VERSION)+L" | "+std::to_wstring(sizeof(void*)*8)+L" bit | TSF thread focus="+std::to_wstring(thread);
        SetWindowTextW(window,title.c_str());return 0;
    }
    if(message==WM_COMMAND && LOWORD(w)==101){
        std::wstring text(GetWindowTextLengthW(editor)+1,L'\0');int count=GetWindowTextW(editor,text.data(),static_cast<int>(text.size()));text.resize(count);
        bool passed=text==L"السلام hello ";
        auto path=ak::data_folder()/(sizeof(void*)==8?L"physical-host-x64.txt":L"physical-host-x86.txt");std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);out<<"Scope: user-entered typing in AutoKeyboard RichEdit diagnostic host only\nMessage bridge: "<<(bridge?"experimental opt-in":"off; normal TSF")<<"\nVersion: "<<AK_VERSION<<"\nSource: "<<AK_SOURCE_ID<<"\nResult: "<<(passed?"PASS":"FAIL")<<"\n"<<ak::utf8(ak::keyboard_diagnostics());
        SetWindowTextW(label,(std::wstring(passed?L"PASS: expected mixed-language text.\r\n":L"FAIL: text does not match the test phrase.\r\n")+L"Metadata report: "+path.wstring()+L"\r\nNo typed text was saved.").c_str());return 0;
    }
    if(message==WM_DESTROY){KillTimer(window,1);PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int){
    bridge=command && std::wstring(command)==L"--bridge";
    SetEnvironmentVariableW(L"AUTOKEYBOARD_MESSAGE_BRIDGE",bridge?L"1":nullptr);
    HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(init))return 1;
    auto preflight=ak::desktop_preflight();if(!preflight.ready){MessageBoxW(nullptr,preflight.report.c_str(),L"AutoKeyboard test blocked",MB_OK);CoUninitialize();return 3;}
    HMODULE rich=LoadLibraryExW(L"msftedit.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!rich){CoUninitialize();return 1;}
    int result=1;
    {
        ak::ComPtr<ITfThreadMgr> manager;TfClientId client=TF_CLIENTID_NULL;
        if(SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_ITfThreadMgr,reinterpret_cast<void**>(manager.put()))) && SUCCEEDED(manager->Activate(&client))){
            ak::ComPtr<ITfKeystrokeMgr> keys;manager->QueryInterface(IID_ITfKeystrokeMgr,reinterpret_cast<void**>(keys.put()));
            WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=window_proc;cls.lpszClassName=L"AutoKeyboard.PhysicalDiagnostic";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
            HWND window=CreateWindowW(cls.lpszClassName,L"AutoKeyboard physical typing test",WS_OVERLAPPEDWINDOW,100,100,800,450,nullptr,nullptr,instance,nullptr);
            if(window){ShowWindow(window,SW_SHOW);SetFocus(editor);MSG message{};
                while(GetMessageW(&message,nullptr,0,0)>0){BOOL interested=FALSE,eaten=FALSE;
                    if(keys && (message.message==WM_KEYDOWN || message.message==WM_KEYUP)){
                        bool down=message.message==WM_KEYDOWN;
                        HRESULT hr=down?keys->TestKeyDown(message.wParam,message.lParam,&interested):keys->TestKeyUp(message.wParam,message.lParam,&interested);
                        if(hr==S_OK && interested){if(down)keys->KeyDown(message.wParam,message.lParam,&eaten);else keys->KeyUp(message.wParam,message.lParam,&eaten);}
                    }
                    if(!eaten){TranslateMessage(&message);DispatchMessageW(&message);}
                }result=0;
            }manager->Deactivate();
        }
    }FreeLibrary(rich);CoUninitialize();return result;
}
