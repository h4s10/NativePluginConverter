// SPDX-License-Identifier: MIT
#pragma once
#include "config.h"
#include <shlobj.h>

// DAWs often hide initialize() errors. Keep one bounded local report per plugin;
// no dialogs during scanning, no credentials, registry contents or native state.
namespace StartupDiagnostics {
inline std::string utf8(const std::wstring& value) {
    if(value.empty())return {};
    int size=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    if(size<=0)return {};
    std::string result(size,'\0');
    WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),result.data(),size,nullptr,nullptr);
    return result;
}
inline std::string field(std::string value) {
    if(value.size()>4096)value.resize(4096);
    for(auto& c:value)if(c=='\r'||c=='\n'||c=='\0')c=' ';
    return value;
}
inline std::wstring writeAt(const std::wstring& directory,const BridgeConfig& config,const std::string& reason) noexcept {
    const DWORD previousError=GetLastError();
    struct RestoreError {DWORD value;~RestoreError(){SetLastError(value);}} restore{previousError};
    try {
        if(directory.empty())return {};
        int created=SHCreateDirectoryExW(nullptr,directory.c_str(),nullptr);
        if(created!=ERROR_SUCCESS&&created!=ERROR_ALREADY_EXISTS&&created!=ERROR_FILE_EXISTS)return {};
        static constexpr wchar_t digits[]=L"0123456789abcdef";
        std::wstring id;for(unsigned char b:config.uid){id+=digits[b>>4];id+=digits[b&15];}
        auto file=directory+L"\\bridge-"+id+L".log";
        std::string text="NativePluginConverter: plugin initialization failed\r\n";
        SYSTEMTIME now{};GetSystemTime(&now);char timestamp[64]{};
        std::snprintf(timestamp,sizeof(timestamp),"UTC: %04u-%02u-%02u %02u:%02u:%02u\r\n",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);
        text+=timestamp;
        text+="Plugin: "+field(config.nameUtf8)+"\r\n";
        text+="Reason: "+field(reason)+"\r\n";
        text+="Configuration: "+field(utf8(config.iniPath))+"\r\n";
        text+="Module: "+field(utf8(config.installed?config.dllRelative:config.dllPath))+"\r\n";
        text+="Run the launch check in NativePluginConverter for the selected FL Studio installation.\r\n";
        HANDLE handle=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(handle==INVALID_HANDLE_VALUE)return {};
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(handle,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){CloseHandle(handle);return {};}
        DWORD written=0;bool ok=WriteFile(handle,text.data(),DWORD(text.size()),&written,nullptr)&&written==text.size();
        if(ok)ok=SetEndOfFile(handle)!=FALSE;CloseHandle(handle);
        return ok?file:std::wstring();
    } catch(...) {return {};}
}
inline void record(const BridgeConfig& config,const std::string& reason) noexcept {
    try {
        wchar_t local[MAX_PATH]{};
        if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA|CSIDL_FLAG_CREATE,nullptr,SHGFP_TYPE_CURRENT,local)))
            writeAt(std::wstring(local)+L"\\NativePluginConverter\\Logs",config,reason);
    } catch(...) {}
}
}
