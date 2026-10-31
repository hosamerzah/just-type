#pragma once
#include "store.hpp"
#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace ak {
inline constexpr wchar_t AppName[] = L"AutoKeyboard";
inline constexpr wchar_t TrayClass[] = L"AutoKeyboard.Settings.1";
inline constexpr UINT TrayLockMessage = WM_APP + 24;
inline constexpr GUID ServiceId = {0x806b192a,0x0d35,0x43e5,{0xa4,0x7c,0x7a,0x94,0x0e,0x38,0xb5,0x65}};
inline constexpr GUID ProfileId = {0x7ca8776c,0x8b94,0x4cff,{0xb7,0x5e,0xc7,0xa7,0xd8,0x97,0x89,0x5a}};
std::filesystem::path module_folder();
std::filesystem::path packs_folder();
std::filesystem::path data_folder();
const std::wstring& tray_class();
bool app_excluded(std::wstring_view exclusions, std::wstring_view executable);
void apply(Engine& engine, const Settings& settings);
struct SharedState { LONG pid, language, mode, locked, thread; DWORD tick; };
class Status {
    HANDLE mapping_ = nullptr;
    HANDLE mutex_ = nullptr;
    SharedState* state_ = nullptr;
public:
    Status();
    ~Status();
    void publish(const Engine& engine);
    void clear();
    SharedState read() const;
};
// Worker owns no TSF/COM pointers. Only immutable bank snapshots cross threads.
class SnapshotLoader {
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    std::atomic_bool stop_{false};
    std::atomic_bool attempted_{false};
    std::shared_ptr<const BankSnapshot> snapshot_;
public:
    ~SnapshotLoader();
    void start(const std::filesystem::path& bank);
    void stop();
    std::shared_ptr<const BankSnapshot> latest(bool initial=false);
    bool pending()const noexcept{return !attempted_.load();}
};
}
