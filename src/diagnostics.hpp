#pragma once
#include "runtime.hpp"
#include <chrono>
#include <fstream>
#include <optional>
#include <stdexcept>

namespace ak {
inline int64_t diagnostic_time(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
inline std::filesystem::path temp_diagnostic_folder(){return std::filesystem::temp_directory_path()/L"AutoKeyboard";}
struct DiagnosticConfig {
    std::filesystem::path file,marker;
    int64_t expires=0;
    std::optional<bool> message_bridge;
    bool active()const{
        return !file.empty() && (!expires || (diagnostic_time()<=expires && std::filesystem::exists(marker)));
    }
};
inline DiagnosticConfig diagnostic_config(){
    wchar_t path[32768]{};DWORD count=GetEnvironmentVariableW(L"AUTOKEYBOARD_TRACE",path,32768);
    if(count && count<32768 && std::filesystem::path(path).is_absolute())return {std::filesystem::path(path),{},0,{}};
    wchar_t customDir[32768]{};DWORD customLen=GetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",customDir,32768);
    auto folder=data_folder();auto marker=folder/L"diagnostics.enabled";
    std::error_code error;auto size=std::filesystem::file_size(marker,error);
    if((error || size>128) && !customLen){folder=temp_diagnostic_folder();marker=folder/L"diagnostics.enabled";error.clear();size=std::filesystem::file_size(marker,error);}
    if(error || size>128)return {};
    int64_t expires=0;int bridge=0;std::ifstream input(marker);input>>expires>>bridge;
    auto now=diagnostic_time();if(!input || expires<now || expires>now+600 || (bridge!=0 && bridge!=1))return {};
    DiagnosticConfig config{folder/L"notepad-diagnostic.log",marker,expires,{}};
    wchar_t executable[32768]{};
    if(GetModuleFileNameW(nullptr,executable,32768) && _wcsicmp(std::filesystem::path(executable).filename().c_str(),L"Notepad.exe")==0 && bridge!=0)config.message_bridge=true;
    return config;
}
inline void enable_diagnostics(const std::filesystem::path& folder,bool bridge){
    std::filesystem::create_directories(folder);
    auto expires=diagnostic_time()+600;
    std::ofstream marker(folder/L"diagnostics.enabled",std::ios::trunc);
    marker<<expires<<' '<<bridge<<'\n';marker.close();
    if(!marker)throw std::runtime_error("Cannot enable the diagnostic capture");
    wchar_t customDir[32768]{};
    if(!GetEnvironmentVariableW(L"AUTOKEYBOARD_DATA_DIR",customDir,32768)){
        std::error_code ec;auto tmp=temp_diagnostic_folder();std::filesystem::create_directories(tmp,ec);
        std::ofstream tmpMarker(tmp/L"diagnostics.enabled",std::ios::trunc);
        tmpMarker<<expires<<' '<<bridge<<'\n';
    }
}
}
