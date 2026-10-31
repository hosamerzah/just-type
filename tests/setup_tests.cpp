#include "setup_state.hpp"
#include <iostream>
#include <stdexcept>
using namespace ak::setup;
int main() {
    int failed=0,passed=0;
    std::filesystem::path root=L"C:\\Program Files\\AutoKeyboard";
    auto block=[](std::initializer_list<std::wstring> items){std::wstring raw;for(const auto& item:items){raw+=item;raw.push_back(L'\0');}raw.push_back(L'\0');return raw;};
    auto test=[&](const char* name,bool value){std::cout<<(value?"PASS ":"FAIL ")<<name<<'\n';if(value)++passed;else ++failed;};
    test("registered partial installation recognized",registered_installation(root,L"AutoKeyboard - Arabic / English",L"c:\\program files\\autokeyboard\\"));
    test("unrelated registration rejected",!registered_installation(root,L"Other program",root.wstring()));
    test("wrong installation path rejected",!registered_installation(root,L"AutoKeyboard - Arabic / English",L"C:\\Other"));
    test("pending live DLL deletion blocks installation",pending_change(root,block({L"\\??\\C:\\Program Files\\AutoKeyboard\\AutoKeyboardIME.dll",L""})));
    test("pending destination replacement blocks installation",pending_change(root,block({L"\\??\\C:\\other.tmp",L"!\\??\\C:\\Program Files\\AutoKeyboard\\AutoKeyboard.exe"})));
    test("later deletion after empty destination found",pending_change(root,block({L"C:\\unrelated",L"",L"C:\\Program Files\\AutoKeyboard\\pdfium.dll",L""})));
    test("case and slash variants recognized",pending_change(root,block({L"c:/program files/autokeyboard/pdfium.dll",L""})));
    test("retired rollback files do not block repair",!pending_change(root,block({L"C:\\Program Files\\AutoKeyboard\\.rollback-123\\AutoKeyboardIME.dll",L""})));
    test("sibling directory not matched",!pending_change(root,block({L"C:\\Program Files\\AutoKeyboardOther\\app.exe",L""})));
    test("pending installation root deletion blocks install",pending_change(root,block({root.wstring(),L""})));
    std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
