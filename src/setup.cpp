#include "text.hpp"
#include "profile.hpp"
#include "setup_state.hpp"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <array>
#include <set>
#include <stdexcept>
#include <cstring>
#include <cstdint>
#include "build_identity.hpp"
#include "runtime.hpp"
#include "tsf.hpp"
#include <exception>
#include <optional>

namespace {
using Path=std::filesystem::path;
struct Entry { Path path; const unsigned char* data; size_t length; };
std::vector<Entry> payload() {
    HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(101),RT_RCDATA);
    if(!resource)throw std::runtime_error("Installer payload is missing");
    size_t size=SizeofResource(nullptr,resource),offset=0;
    auto data=static_cast<const unsigned char*>(LockResource(LoadResource(nullptr,resource)));
    if(!data || size<8 || std::memcmp(data,"AKP1",4)!=0)throw std::runtime_error("Invalid installer payload");
    offset=4;
    auto number=[&](size_t bytes)->uint64_t {
        if(offset+bytes>size)throw std::runtime_error("Truncated installer payload");
        uint64_t value=0; for(size_t i=0;i<bytes;++i)value|=static_cast<uint64_t>(data[offset++])<<(i*8); return value;
    };
    auto count=number(4); if(count>20000)throw std::runtime_error("Invalid installer file count");
    std::vector<Entry> entries; std::set<Path> names;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 is unavailable");
    try {
        for(uint64_t i=0;i<count;++i) {
            auto nameSize=number(4); if(nameSize==0 || nameSize>4096 || offset+nameSize>size)throw std::runtime_error("Invalid installer filename");
            auto name=ak::wide(std::string_view(reinterpret_cast<const char*>(data+offset),static_cast<size_t>(nameSize))); offset+=static_cast<size_t>(nameSize);
            Path path(name);
            if(path.is_absolute() || path.has_root_path() || name.find(L':')!=std::wstring::npos || name.find(L'\\')!=std::wstring::npos)throw std::runtime_error("Unsafe installer filename");
            for(const auto& part:path)if(part==L".." || part==L".")throw std::runtime_error("Unsafe installer path");
            if(!names.insert(path).second)throw std::runtime_error("Duplicate installer filename");
            auto length=number(8);
            if(length>300*1024*1024 || offset+32>size || length>size-offset-32)throw std::runtime_error("Invalid installer file size");
            const unsigned char* expected=data+offset; offset+=32;
            std::array<unsigned char,32> hash{};
            if(BCryptHash(algorithm,nullptr,0,const_cast<PUCHAR>(data+offset),static_cast<ULONG>(length),hash.data(),32)<0 || std::memcmp(hash.data(),expected,32)!=0)throw std::runtime_error("Installer payload checksum failed");
            entries.push_back({path,data+offset,static_cast<size_t>(length)}); offset+=static_cast<size_t>(length);
        }
        if(offset!=size)throw std::runtime_error("Unexpected installer data");
    } catch(...) { BCryptCloseAlgorithmProvider(algorithm,0); throw; }
    BCryptCloseAlgorithmProvider(algorithm,0);
    return entries;
}
Path known_folder(REFKNOWNFOLDERID id) {
    PWSTR value=nullptr; if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&value)))throw std::runtime_error("Windows folder is unavailable");
    Path path(value);CoTaskMemFree(value);return path;
}
void validate_target(const Path& root,const Path& relative) {
    auto target=(root/relative).lexically_normal();
    auto rel=target.lexically_relative(root.lexically_normal());
    if(rel.empty() || rel.is_absolute() || *rel.begin()==L"..")throw std::runtime_error("Unsafe installation target");
    Path current=root;
    if(GetFileAttributesW(current.c_str())!=INVALID_FILE_ATTRIBUTES && (GetFileAttributesW(current.c_str())&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Installation folder cannot be a link");
    for(const auto& part:relative) {
        current/=part;DWORD attributes=GetFileAttributesW(current.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Installation paths cannot contain links");
    }
}
void registry_string(HKEY root,const wchar_t* key,const wchar_t* name,const std::wstring& value) {
    HKEY handle=nullptr;LSTATUS status=RegCreateKeyExW(root,key,0,nullptr,0,KEY_SET_VALUE,nullptr,&handle,nullptr);
    if(status!=ERROR_SUCCESS)throw std::runtime_error("Cannot update installation settings");
    status=RegSetValueExW(handle,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));RegCloseKey(handle);
    if(status!=ERROR_SUCCESS)throw std::runtime_error("Cannot write installation settings");
}
bool register_dll(const Path& dll,bool install,bool x86) {
    wchar_t windows[32768]{};GetWindowsDirectoryW(windows,32768);
    Path exe=Path(windows)/(x86?L"SysWOW64":L"System32")/L"regsvr32.exe";
    std::wstring args=L"\""+exe.wstring()+L"\" /s "+(install?L"":L"/u ")+L"\""+dll.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),args.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return false;
    DWORD waited=WaitForSingleObject(process.hProcess,60000),code=1;
    if(waited==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&code);
    else { TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,5000); }
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return waited==WAIT_OBJECT_0 && code==0;
}
void shortcut(const Path& root,bool install) {
    Path path=known_folder(FOLDERID_CommonPrograms)/L"AutoKeyboard.lnk";
    if(!install){std::filesystem::remove(path);return;}
    IShellLinkW* link=nullptr;
    if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&link))))throw std::runtime_error("Cannot create Start menu shortcut");
    link->SetPath((root/L"AutoKeyboard.exe").c_str());link->SetWorkingDirectory(root.c_str());
    IPersistFile* file=nullptr;HRESULT hr=link->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&file));
    if(SUCCEEDED(hr)){hr=file->Save(path.c_str(),TRUE);file->Release();}link->Release();
    if(FAILED(hr))throw std::runtime_error("Cannot save Start menu shortcut");
}
constexpr wchar_t UninstallKey[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AutoKeyboard";
struct RegistryValue {std::wstring name;DWORD type=REG_NONE;std::optional<std::vector<BYTE>> bytes;};
std::vector<RegistryValue> save_install_record(){
    std::vector<RegistryValue> values;
    for(const auto* name:{L"DisplayName",L"UninstallString",L"InstallLocation",L"DisplayVersion"}){
        RegistryValue value;value.name=name;DWORD size=0;
        auto status=RegGetValueW(HKEY_LOCAL_MACHINE,UninstallKey,name,RRF_RT_ANY,&value.type,nullptr,&size);
        if(status==ERROR_SUCCESS){if(size>1048576)throw std::runtime_error("Invalid prior installation record");value.bytes.emplace(size);status=RegGetValueW(HKEY_LOCAL_MACHINE,UninstallKey,name,RRF_RT_ANY,&value.type,value.bytes->data(),&size);}
        if(status!=ERROR_SUCCESS && status!=ERROR_FILE_NOT_FOUND && status!=ERROR_PATH_NOT_FOUND)throw std::runtime_error("Cannot preserve prior installation record");
        values.push_back(std::move(value));
    }return values;
}
bool restore_install_record(const std::vector<RegistryValue>& values){
    HKEY key=nullptr;if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,UninstallKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    bool restored=true;for(const auto& value:values){auto status=value.bytes?RegSetValueExW(key,value.name.c_str(),0,value.type,value.bytes->data(),static_cast<DWORD>(value.bytes->size())):RegDeleteValueW(key,value.name.c_str());if(status!=ERROR_SUCCESS && status!=ERROR_FILE_NOT_FOUND)restored=false;}RegCloseKey(key);return restored;
}
void clear_missing_registration(){
    // Only this product's fixed CLSID and profile are removed, in both views.
    ak::ComPtr<ITfInputProcessorProfiles> profiles;
    if(SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfiles,reinterpret_cast<void**>(profiles.put()))))profiles->Unregister(ak::ServiceId);
    wchar_t id[40]{};StringFromGUID2(ak::ServiceId,id,40);
    for(REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}){HKEY classes=nullptr;auto status=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"Software\\Classes\\CLSID",0,KEY_ALL_ACCESS|view,&classes);if(status==ERROR_SUCCESS){status=RegDeleteTreeW(classes,id);RegCloseKey(classes);}if(status!=ERROR_SUCCESS && status!=ERROR_FILE_NOT_FOUND && status!=ERROR_PATH_NOT_FOUND)throw std::runtime_error("Windows could not clean a missing keyboard registration");}
}
std::wstring registry_value(const wchar_t* key,const wchar_t* name,DWORD type) {
    DWORD bytes=0;LSTATUS status=RegGetValueW(HKEY_LOCAL_MACHINE,key,name,type,nullptr,nullptr,&bytes);
    if(status==ERROR_FILE_NOT_FOUND || status==ERROR_PATH_NOT_FOUND)return {};
    if(status!=ERROR_SUCCESS || bytes>1024*1024)throw std::runtime_error("Cannot read installation state");
    std::wstring value(bytes/sizeof(wchar_t)+2,L'\0');
    status=RegGetValueW(HKEY_LOCAL_MACHINE,key,name,type,nullptr,value.data(),&bytes);
    if(status!=ERROR_SUCCESS)throw std::runtime_error("Cannot read installation state");
    value.resize(bytes/sizeof(wchar_t));
    if(type==RRF_RT_REG_SZ)while(!value.empty()&&value.back()==L'\0')value.pop_back();
    return value;
}
bool verify_files(const Path& root,const std::vector<Entry>& entries) {
    std::array<char,65536> buffer{};
    for(const auto& entry:entries) {
        validate_target(root,entry.path);
        std::ifstream input(root/entry.path,std::ios::binary|std::ios::ate);
        if(!input || input.tellg()!=static_cast<std::streamoff>(entry.length))return false;
        input.seekg(0);
        for(size_t offset=0;offset<entry.length;) {
            size_t count=std::min(buffer.size(),entry.length-offset);
            input.read(buffer.data(),static_cast<std::streamsize>(count));
            if(!input || std::memcmp(buffer.data(),entry.data+offset,count)!=0)return false;
            offset+=count;
        }
    }
    return true;
}
bool remove_files(const Path& root,const std::vector<Entry>& entries) {
    bool pending=false;
    std::set<Path,std::greater<Path>> directories;
    for(const auto& entry:entries) {
        validate_target(root,entry.path);
        auto path=root/entry.path;
        if(std::filesystem::exists(path) && !DeleteFileW(path.c_str())) {
            if(!MoveFileExW(path.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT))throw std::runtime_error("Close apps using AutoKeyboard, then uninstall again");
            pending=true;
        }
        auto parent=entry.path.parent_path();while(!parent.empty()){directories.insert(parent);parent=parent.parent_path();}
    }
    auto self=root/L"AutoKeyboardSetup.exe";
    if(std::filesystem::exists(self) && !DeleteFileW(self.c_str())){MoveFileExW(self.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT);pending=true;}
    for(const auto& directory:directories){std::error_code error;std::filesystem::remove(root/directory,error);if(error && pending)MoveFileExW((root/directory).c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT);}
    std::error_code error;std::filesystem::remove(root,error);
    if(error && pending)MoveFileExW(root.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT);
    return pending;
}
bool admin() { SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY;PSID group=nullptr;BOOL member=FALSE;
    if(AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&group)){CheckTokenMembership(nullptr,group,&member);FreeSid(group);}return member!=FALSE;
}
int run(const std::vector<std::wstring>& args) {
    auto entries=payload();
    if(args.size()>1 && args[1]==L"--verify-payload")return 0;
    if(args.size()>1 && args[1]==L"--verify-installed") {
        Path root=args.size()>2?std::filesystem::absolute(args[2]):known_folder(FOLDERID_ProgramFiles)/L"AutoKeyboard";
        return verify_files(root,entries)?0:1;
    }
    if(args.size()>2 && args[1]==L"--extract") {
        Path root=std::filesystem::absolute(args[2]);
        if(std::filesystem::exists(root))throw std::runtime_error("Extraction folder must not exist");
        for(const auto& entry:entries){validate_target(root,entry.path);auto path=root/entry.path;std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(entry.data),entry.length);if(!out)throw std::runtime_error("Payload extraction failed");}
        return 0;
    }
    bool uninstall=args.size()>1 && args[1]==L"--uninstall";
    if(!admin()) {
        wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
        SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS;info.lpVerb=L"runas";info.lpFile=self;info.lpParameters=uninstall?L"--uninstall":L"--install";info.nShow=SW_SHOWNORMAL;
        if(!ShellExecuteExW(&info))return 1;
        if(!info.hProcess)return 1;
        DWORD code=1;WaitForSingleObject(info.hProcess,INFINITE);GetExitCodeProcess(info.hProcess,&code);CloseHandle(info.hProcess);
        if(code==0){
            // This process retains the original desktop user's token. Never
            // write that user's startup/profile state in the elevated child.
            auto preflight=ak::desktop_preflight();
            if(preflight.ready){
                if(uninstall)ak::configure_current_user(false);
                else {
                    auto exe=known_folder(FOLDERID_ProgramFiles)/L"AutoKeyboard/AutoKeyboardControl.exe";
                    std::wstring command=L"\""+exe.wstring()+L"\" --configure-user";
                    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
                    if(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){
                        DWORD waited=WaitForSingleObject(process.hProcess,30000);if(waited!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,5000);}DWORD configured=1;GetExitCodeProcess(process.hProcess,&configured);CloseHandle(process.hThread);CloseHandle(process.hProcess);
                        if(configured)MessageBoxW(nullptr,L"Machine installation succeeded. Open Just Type normally to finish your user's configuration.",L"Just Type (اكتب بلا هم)",MB_OK|MB_ICONINFORMATION);
                        auto appExe=known_folder(FOLDERID_ProgramFiles)/L"AutoKeyboard/AutoKeyboard.exe";
                        ShellExecuteW(nullptr,L"open",appExe.c_str(),nullptr,appExe.parent_path().c_str(),SW_SHOWNORMAL);
                    }
                }
            }
        }return static_cast<int>(code);
    }
    Path root=known_folder(FOLDERID_ProgramFiles)/L"AutoKeyboard";
    if(uninstall) {
        if(!ak::setup::registered_installation(root,registry_value(UninstallKey,L"DisplayName",RRF_RT_REG_SZ),registry_value(UninstallKey,L"InstallLocation",RRF_RT_REG_SZ)))throw std::runtime_error("AutoKeyboard is not registered at the expected location");
        if(HWND tray=FindWindowW(L"AutoKeyboard.Settings.1",nullptr))PostMessageW(tray,WM_COMMAND,204,0);
        if(std::filesystem::exists(root/L"AutoKeyboardIME.dll") && !register_dll(root/L"AutoKeyboardIME.dll",false,false))throw std::runtime_error("Windows could not unregister the keyboard. Close applications using it, then try again.");
        if(std::filesystem::exists(root/L"x86/AutoKeyboardIME.dll") && !register_dll(root/L"x86/AutoKeyboardIME.dll",false,true))throw std::runtime_error("Windows could not unregister the 32-bit keyboard");
        clear_missing_registration();
        RegDeleteTreeW(HKEY_LOCAL_MACHINE,UninstallKey);
        shortcut(root,false);
        bool pending=remove_files(root,entries);
        MessageBoxW(nullptr,pending?L"AutoKeyboard was uninstalled. Restart Windows to remove files still in use. Your personal word bank was kept.":L"AutoKeyboard was uninstalled. Your personal word bank was kept.",L"AutoKeyboard",MB_OK|MB_ICONINFORMATION);
        return 0;
    }
    auto pending=registry_value(L"SYSTEM\\CurrentControlSet\\Control\\Session Manager",L"PendingFileRenameOperations",RRF_RT_REG_MULTI_SZ);
    if(ak::setup::pending_change(root,pending))throw std::runtime_error("An earlier uninstall or update has pending file changes. Save your work and restart Windows before installing AutoKeyboard. Your personal word bank will be kept.");
    bool repair=(std::filesystem::exists(root/L"AutoKeyboardIME.dll") && std::filesystem::exists(root/L"AutoKeyboard.exe")) ||
        ak::setup::registered_installation(root,registry_value(UninstallKey,L"DisplayName",RRF_RT_REG_SZ),registry_value(UninstallKey,L"InstallLocation",RRF_RT_REG_SZ));
    if(std::filesystem::exists(root) && ((GetFileAttributesW(root.c_str())&FILE_ATTRIBUTE_REPARSE_POINT) || !std::filesystem::is_directory(root) || (!repair && !std::filesystem::is_empty(root))))throw std::runtime_error("The installation folder contains unrecognized files; choose a clean AutoKeyboard installation.");
    if(HWND tray=FindWindowW(L"AutoKeyboard.Settings.1",nullptr)) {
        DWORD pid=0;GetWindowThreadProcessId(tray,&pid);
        HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);
        PostMessageW(tray,WM_COMMAND,204,0);
        if(process){DWORD wait=WaitForSingleObject(process,10000);CloseHandle(process);if(wait!=WAIT_OBJECT_0)throw std::runtime_error("Close AutoKeyboard from its tray menu, then run setup again.");}
    }
    Path backup=root/(L".rollback-"+std::to_wstring(GetCurrentProcessId()));
    validate_target(root,backup.lexically_relative(root));
    if(std::filesystem::exists(backup))throw std::runtime_error("An installation rollback folder already exists");
    std::vector<Path> installed;
    std::vector<Path> moved;
    auto replace=[&](const Path& relative,const unsigned char* data,size_t length) {
        validate_target(root,relative);auto path=root/relative;
        std::filesystem::create_directories(path.parent_path());
        auto pending=Path(path.wstring()+L".new-"+std::to_wstring(GetCurrentProcessId()));
        validate_target(root,pending.lexically_relative(root));
        if(std::filesystem::exists(pending))throw std::runtime_error("An installation staging file already exists");
        {std::ofstream out(pending,std::ios::binary);out.write(reinterpret_cast<const char*>(data),static_cast<std::streamsize>(length));if(!out){out.close();DeleteFileW(pending.c_str());throw std::runtime_error("Cannot stage installation files");}}
        if(std::filesystem::exists(path)) {
            auto saved=backup/relative;validate_target(backup,relative);std::filesystem::create_directories(saved.parent_path());
            if(!MoveFileExW(path.c_str(),saved.c_str(),0)){DeleteFileW(pending.c_str());throw std::runtime_error("Close editors using AutoKeyboard and run setup again");}
            moved.push_back(relative);
        }
        if(!MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)){DeleteFileW(pending.c_str());throw std::runtime_error("Cannot replace an installation file");}
        installed.push_back(relative);
    };
    bool registered=false;
    auto oldRecord=save_install_record();
    auto linkPath=known_folder(FOLDERID_CommonPrograms)/L"AutoKeyboard.lnk";
    std::optional<std::vector<char>> oldLink;
    if(std::filesystem::exists(linkPath)){
        std::ifstream input(linkPath,std::ios::binary|std::ios::ate);auto size=input.tellg();
        if(size<0 || size>1048576)throw std::runtime_error("Cannot preserve existing Start menu shortcut");
        oldLink.emplace(static_cast<size_t>(size));input.seekg(0);input.read(oldLink->data(),size);if(!input)throw std::runtime_error("Cannot preserve Start menu shortcut");
    }
    try {
        for(const auto& entry:entries) {
            replace(entry.path,entry.data,entry.length);
        }
        wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
        std::ifstream selfInput(Path(self),std::ios::binary|std::ios::ate);auto selfSize=selfInput.tellg();
        if(selfSize<=0)throw std::runtime_error("Cannot read the setup program");
        std::vector<unsigned char> selfBytes(static_cast<size_t>(selfSize));selfInput.seekg(0);selfInput.read(reinterpret_cast<char*>(selfBytes.data()),static_cast<std::streamsize>(selfBytes.size()));
        if(!selfInput)throw std::runtime_error("Cannot read the setup program");
        replace(L"AutoKeyboardSetup.exe",selfBytes.data(),selfBytes.size());
        if(!verify_files(root,entries))throw std::runtime_error("Installation files failed verification. No completed installation was recorded.");
        if(!register_dll(root/L"AutoKeyboardIME.dll",true,false))throw std::runtime_error("Windows could not register the keyboard. No installation was retained.");
        registered=true;
        if(std::filesystem::exists(root/L"x86/AutoKeyboardIME.dll") && !register_dll(root/L"x86/AutoKeyboardIME.dll",true,true))throw std::runtime_error("Windows could not register the 32-bit keyboard");
        registry_string(HKEY_LOCAL_MACHINE,UninstallKey,L"DisplayName",L"AutoKeyboard - Arabic / English");
        registry_string(HKEY_LOCAL_MACHINE,UninstallKey,L"UninstallString",L"\""+(root/L"AutoKeyboardSetup.exe").wstring()+L"\" --uninstall");
        registry_string(HKEY_LOCAL_MACHINE,UninstallKey,L"InstallLocation",root.wstring());
        shortcut(root,true);
        registry_string(HKEY_LOCAL_MACHINE,UninstallKey,L"DisplayVersion",ak::wide(AK_VERSION));
    } catch(...) {
        auto original=std::current_exception();bool restored=true;
        if(registered){register_dll(root/L"AutoKeyboardIME.dll",false,false);if(std::filesystem::exists(root/L"x86/AutoKeyboardIME.dll"))register_dll(root/L"x86/AutoKeyboardIME.dll",false,true);}
        for(auto it=installed.rbegin();it!=installed.rend();++it){validate_target(root,*it);if(!DeleteFileW((root/(*it)).c_str()) && GetLastError()!=ERROR_FILE_NOT_FOUND)restored=false;}
        for(auto it=moved.rbegin();it!=moved.rend();++it){validate_target(root,*it);if(!MoveFileExW((backup/(*it)).c_str(),(root/(*it)).c_str(),MOVEFILE_REPLACE_EXISTING))restored=false;}
        if(repair){register_dll(root/L"AutoKeyboardIME.dll",true,false);if(std::filesystem::exists(root/L"x86/AutoKeyboardIME.dll"))register_dll(root/L"x86/AutoKeyboardIME.dll",true,true);}
        else {RegDeleteTreeW(HKEY_LOCAL_MACHINE,UninstallKey);try{remove_files(root,entries);}catch(...){}}
        if(!restore_install_record(oldRecord))restored=false;
        if(oldLink){std::ofstream out(linkPath,std::ios::binary|std::ios::trunc);out.write(oldLink->data(),oldLink->size());if(!out)restored=false;}
        else{std::error_code error;std::filesystem::remove(linkPath,error);if(error)restored=false;}
        if(!restored)throw std::runtime_error("Installation failed and rollback was incomplete. Keep the .rollback folder, close editors, and run the repair installer before restarting.");
        std::rethrow_exception(original);
    }
    if(repair){try{remove_files(backup,entries);}catch(...){}}
    std::wstring completed=L"Just Type (اكتب بلا هم) "+ak::wide(AK_VERSION)+L" installation is complete! Your word bank and settings were kept.\n\n";
    completed+=repair?L"Save your work and restart Windows before testing this update. Explorer, Start search and other open applications can keep the previous keyboard version active until a restart.\n\n":L"Close and reopen your editors.\n\n";
    completed+=L"Universal Global Hook is active automatically across all apps — just type in Arabic or English (for example hgsghl followed by Space) and Just Type will convert and switch your keyboard layout.";
    MessageBoxW(nullptr,completed.c_str(),L"Just Type (اكتب بلا هم)",MB_OK|MB_ICONINFORMATION);
    // Start the tray from the Start menu in the user's normal desktop session;
    // do not launch an elevated keyboard-settings process from this installer.
    return 0;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int count=0;LPWSTR* values=CommandLineToArgvW(GetCommandLineW(),&count);std::vector<std::wstring> args;
    for(int i=0;i<count;++i)args.emplace_back(values[i]);LocalFree(values);
    int result=1;
    try{result=run(args);}catch(const std::exception& e){
        bool unattended=args.size()>1 && (args[1]==L"--verify-payload" || args[1]==L"--verify-installed" || args[1]==L"--extract");
        if(!unattended)MessageBoxW(nullptr,ak::wide(e.what()).c_str(),L"AutoKeyboard setup",MB_OK|MB_ICONERROR);
    }
    if(SUCCEEDED(com))CoUninitialize();return result;
}
