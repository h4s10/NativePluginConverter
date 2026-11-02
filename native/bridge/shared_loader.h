// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <cstring>
#ifdef _MSC_VER
#include <intrin.h>
#endif

// Some native readers use a relative ffmpeg path that assumes FL's working
// directory. Redirect that exact import at the calling native module only;
// never change the DAW's directory or DLL search path. Files on disk are intact.
class SharedLoader {
    struct Patch { ULONGLONG* slot; ULONGLONG original; };
    struct Entry { size_t users=0; std::wstring root; std::vector<Patch> patches; };
    static std::map<HMODULE,Entry>& entries(){static std::map<HMODULE,Entry> v;return v;}
    static std::mutex& mutex(){static std::mutex m;return m;}
    HMODULE module=nullptr;
    static HMODULE WINAPI load(LPCWSTR path) {
        if(path && _wcsicmp(path,L"ffmpeg\\x64\\ilvideo_x64.dll")==0){
            HMODULE caller=nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(
#ifdef _MSC_VER
                _ReturnAddress()
#else
                __builtin_return_address(0)
#endif
                ),&caller);
            std::wstring root;
            {std::lock_guard<std::mutex> lock(mutex());auto it=entries().find(caller);if(it!=entries().end())root=it->second.root;}
            if(!root.empty())return LoadLibraryExW((root+L"\\Shared\\ffmpeg\\x64\\ilvideo_x64.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        }
        return LoadLibraryW(path);
    }
public:
    ~SharedLoader(){detach();}
    void attach(HMODULE m,const std::wstring& root){
        if(!m||root.empty())return;detach();std::lock_guard<std::mutex> lock(mutex());
        auto& e=entries()[m];if(e.users++){module=m;return;}e.root=root;module=m;
        auto* b=reinterpret_cast<unsigned char*>(m);auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(b);
        auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(b+dos->e_lfanew);
        auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!dir.VirtualAddress)return;
        for(auto* d=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(b+dir.VirtualAddress);d->Name;++d){
            if(!d->OriginalFirstThunk)continue;
            auto* o=reinterpret_cast<IMAGE_THUNK_DATA64*>(b+d->OriginalFirstThunk);
            auto* i=reinterpret_cast<IMAGE_THUNK_DATA64*>(b+d->FirstThunk);
            for(;o->u1.AddressOfData;++o,++i){
                if(IMAGE_SNAP_BY_ORDINAL64(o->u1.Ordinal))continue;
                auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(b+o->u1.AddressOfData)->Name;
                if(std::strcmp(name,"LoadLibraryW"))continue;
                // Do not replace a host or another tool's existing interception.
                if(i->u1.Function!=reinterpret_cast<ULONGLONG>(&LoadLibraryW))continue;
                e.patches.push_back({&i->u1.Function,i->u1.Function});DWORD old=0;
                if(VirtualProtect(&i->u1.Function,sizeof(i->u1.Function),PAGE_READWRITE,&old)){
                    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&i->u1.Function),reinterpret_cast<void*>(&load));
                    DWORD ignored;VirtualProtect(&i->u1.Function,sizeof(i->u1.Function),old,&ignored);
                }else e.patches.pop_back();
            }
        }
    }
    void detach(){
        if(!module)return;std::lock_guard<std::mutex> lock(mutex());auto it=entries().find(module);
        if(it!=entries().end()&&!--it->second.users){for(auto p:it->second.patches){
            if(*p.slot!=reinterpret_cast<ULONGLONG>(&load))continue;DWORD old=0;
            if(VirtualProtect(p.slot,sizeof(*p.slot),PAGE_READWRITE,&old)){
                InterlockedExchangePointer(reinterpret_cast<void* volatile*>(p.slot),reinterpret_cast<void*>(p.original));
                DWORD ignored;VirtualProtect(p.slot,sizeof(*p.slot),old,&ignored);
            }
        }entries().erase(it);}module=nullptr;
    }
};
