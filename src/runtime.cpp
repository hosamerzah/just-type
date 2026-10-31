#include "runtime.hpp"
#include <shlobj.h>
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace ak {
std::filesystem::path module_folder() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&module_folder), &module);
    std::wstring path(32768, 0);
    DWORD n = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) throw std::runtime_error("Cannot find installation folder");
    path.resize(n);
    return std::filesystem::path(path).parent_path();
}
std::filesystem::path data_folder() {
    wchar_t overridePath[32768]{};
    DWORD overrideLength = GetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR", overridePath, 32768);
    if (overrideLength > 0 && overrideLength < 32768 && std::filesystem::path(overridePath).is_absolute()) return std::filesystem::path(overridePath);
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) throw std::runtime_error("Cannot find local application data");
    std::filesystem::path result = std::filesystem::path(path) / AppName;
    CoTaskMemFree(path);
    return result;
}
std::filesystem::path packs_folder() {
    auto folder=module_folder();
    if(std::filesystem::exists(folder/L"packs"))return folder/L"packs";
    // Dual-architecture installers place the 32-bit input DLL in x86/.
    if(folder.filename()==L"x86" && std::filesystem::exists(folder.parent_path()/L"packs"))return folder.parent_path()/L"packs";
    return folder/L"packs";
}
const std::wstring& tray_class() {
    static const std::wstring name=[] {
        wchar_t directory[32768]{};
        DWORD n=GetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",directory,32768);
        if(!n || n>=32768)return std::wstring(TrayClass);
        uint64_t hash=14695981039346656037ull;
        for(wchar_t ch:std::wstring_view(directory,n)){hash^=static_cast<uint16_t>(ch);hash*=1099511628211ull;}
        return std::wstring(TrayClass)+L"."+std::to_wstring(hash);
    }();
    return name;
}
bool app_excluded(std::wstring_view exclusions, std::wstring_view executable) {
    std::wistringstream input{std::wstring(exclusions)};
    std::wstring name;
    while (std::getline(input, name, L';')) {
        auto begin = name.find_first_not_of(L" \r\n\t"), end = name.find_last_not_of(L" \r\n\t");
        if (begin == std::wstring::npos) continue;
        name = name.substr(begin, end - begin + 1);
        if (CompareStringOrdinal(name.c_str(), static_cast<int>(name.size()), executable.data(), static_cast<int>(executable.size()), TRUE) == CSTR_EQUAL) return true;
    }
    return false;
}
void apply(Engine& engine, const Settings& settings) {
    engine.mode = settings.mode;
    engine.locked = settings.locked;
    engine.bank_minimum = settings.bank_minimum;
    engine.switch_minimum = settings.switch_minimum;
}
Status::Status() {
    wchar_t user[256]{}; DWORD size = 256;
    if (!GetUserNameW(user, &size)) return;
    std::wstring name = L"Local\\AutoKeyboard.Status.v2." + std::wstring(user);
    wchar_t directory[32768]{};DWORD count=GetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",directory,32768);
    if(count && count<32768) {
        uint64_t hash=14695981039346656037ull;
        for(wchar_t ch:std::wstring_view(directory,count)){hash^=static_cast<uint16_t>(ch);hash*=1099511628211ull;}
        name+=L"."+std::to_wstring(hash);
    }
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedState), name.c_str());
    if (mapping_) state_ = static_cast<SharedState*>(MapViewOfFile(mapping_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedState)));
    mutex_=CreateMutexW(nullptr,FALSE,(name+L".Lock").c_str());
}
Status::~Status() { if (state_) UnmapViewOfFile(state_); if (mapping_) CloseHandle(mapping_);if(mutex_)CloseHandle(mutex_); }
namespace {
bool status_lock(HANDLE mutex){if(!mutex)return false;DWORD result=WaitForSingleObject(mutex,0);return result==WAIT_OBJECT_0 || result==WAIT_ABANDONED;}
}
void Status::publish(const Engine& engine) {
    if (!state_ || !status_lock(mutex_)) return;
    LONG index = 0;
    for (size_t i = 0; i < engine.packs.size(); ++i) if (engine.packs[i].id == engine.current) index = static_cast<LONG>(i);
    *state_={static_cast<LONG>(GetCurrentProcessId()),index,engine.mode==Mode::Automatic?1:0,engine.locked?1:0,static_cast<LONG>(GetCurrentThreadId()),GetTickCount()};
    ReleaseMutex(mutex_);
}
void Status::clear() {
    if(!state_ || !status_lock(mutex_))return;
    if(state_->thread==static_cast<LONG>(GetCurrentThreadId()) && state_->pid==static_cast<LONG>(GetCurrentProcessId()))state_->pid=0;
    ReleaseMutex(mutex_);
}
SharedState Status::read() const {
    if (!state_ || !status_lock(mutex_)) return {};
    SharedState result=*state_;ReleaseMutex(mutex_);
    return result;
}
SnapshotLoader::~SnapshotLoader(){stop();}
void SnapshotLoader::start(const std::filesystem::path& bank) {
    stop(); stop_=false;
    {std::lock_guard lock(mutex_);snapshot_.reset();attempted_=false;}
    worker_=std::thread([this,bank] {
        int64_t revision=-1;
        while(!stop_) {
            try {
                Store db(bank,true); db.cancellation(&stop_);
                if(db.revision()!=revision) {
                    auto next=std::make_shared<BankSnapshot>(db.snapshot());
                    revision=next->revision;
                    std::lock_guard lock(mutex_);snapshot_=std::move(next);
                }
            } catch(...) { /* retain last complete snapshot; retry without blocking typing */ }
            std::unique_lock lock(mutex_);attempted_=true;changed_.notify_all();
            changed_.wait_for(lock,std::chrono::milliseconds(1500),[this]{return stop_.load();});
        }
    });
}
void SnapshotLoader::stop(){stop_=true;changed_.notify_all();if(worker_.joinable())worker_.join();}
std::shared_ptr<const BankSnapshot> SnapshotLoader::latest(bool initial) {
    std::unique_lock lock(mutex_);
    if(initial)changed_.wait_for(lock,std::chrono::milliseconds(500),[this]{return attempted_.load();});
    return snapshot_;
}
}
