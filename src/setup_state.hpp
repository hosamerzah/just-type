#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <algorithm>

namespace ak::setup {
inline std::wstring normalized_path(std::wstring value) {
    if(!value.empty()&&value.front()==L'!')value.erase(0,1);
    if(value.starts_with(L"\\??\\")||value.starts_with(L"\\\\?\\"))value.erase(0,4);
    std::replace(value.begin(),value.end(),L'/',L'\\');
    value=std::filesystem::path(value).lexically_normal().wstring();
    while(value.size()>3&&value.back()==L'\\')value.pop_back();
    return value;
}
inline bool same_path(std::wstring a,std::wstring b) {
    a=normalized_path(std::move(a));b=normalized_path(std::move(b));
    return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;
}
inline bool registered_installation(const std::filesystem::path& root,std::wstring_view name,std::wstring_view location) {
    return name==L"AutoKeyboard - Arabic / English" && !location.empty() && same_path(root.wstring(),std::wstring(location));
}
inline bool pending_change(const std::filesystem::path& root,std::wstring_view operations) {
    auto base=normalized_path(root.wstring());
    // Scan the bounded raw MULTI_SZ, including entries after an empty deletion
    // destination. Inspect both sources and destinations; never change the queue.
    for(size_t offset=0;offset<operations.size();) {
        auto end=operations.find(L'\0',offset);if(end==std::wstring_view::npos)end=operations.size();
        auto item=normalized_path(std::wstring(operations.substr(offset,end-offset)));offset=end+1;
        if(item.size()<base.size() || CompareStringOrdinal(base.c_str(),static_cast<int>(base.size()),item.c_str(),static_cast<int>(base.size()),TRUE)!=CSTR_EQUAL)continue;
        if(item.size()==base.size())return true;
        if(item[base.size()]!=L'\\')continue;
        auto child=std::wstring_view(item).substr(base.size()+1);
        // Retired copies use unique paths; their deletion cannot remove a newly
        // installed live file at root. All other pending changes require reboot.
        if(child.starts_with(L".rollback-"))continue;
        return true;
    }
    return false;
}
}
