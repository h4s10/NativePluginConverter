// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <cwctype>
#include "installed_fl.h"
#ifdef _MSC_VER
#include <intrin.h>
#define NPC_FONT_CALLER _ReturnAddress()
#else
#define NPC_FONT_CALLER __builtin_return_address(0)
#endif

// QuickFontCache can fall back to Windows\Fonts for FL's private icon fonts.
// Resolve only these missing, read-only requests against the selected installation.
// No system font registration, asset copying, DLL-file changes or CWD changes.
class InstalledFonts {
    struct Patch { ULONGLONG* slot; ULONGLONG original; ULONGLONG replacement; };
    struct Entry {
        HMODULE module=nullptr;
        size_t users=0;
        std::wstring root;
        std::map<std::wstring,std::wstring> fonts;
        std::vector<Patch> patches;
    };
    // POD shared between independently loaded generated VST3 modules. Every user
    // holds an OS reference to the module implementing these callbacks. The last
    // user clears this record under the named mutex before releasing that module.
    struct Shared {
        DWORD version;
        void* entry;
        bool (*acquire)(void*,const wchar_t*);
        int (*release)(void*);
    };
    HANDLE mapping=nullptr,gate=nullptr;
    Shared* shared=nullptr;
    HMODULE owner=nullptr,target=nullptr;
    static std::map<HMODULE,Entry*>& entries(){static std::map<HMODULE,Entry*> e;return e;}
    static std::mutex& mutex(){static std::mutex m;return m;}
    static std::wstring normalize(std::wstring p){for(auto& c:p){if(c==L'/')c=L'\\';c=std::towlower(c);}return p;}
    static std::wstring fallback(void* address,LPCWSTR path){
        if(!path)return {};std::wstring p=normalize(path);
        auto slash=p.find_last_of(L'\\');auto name=p.substr(slash==std::wstring::npos?0:slash+1);
        if(name!=L"ilglyphsex.ilfont"&&name!=L"ilcursors.ilfont"&&name!=L"illogos.ilfont")return {};
        auto dir=slash==std::wstring::npos?std::wstring():p.substr(0,slash+1);
        wchar_t windows[32768]{};DWORD n=GetWindowsDirectoryW(windows,32768);
        if(!dir.empty()&&dir!=L"shared\\artwork\\fonts\\"&&dir!=L"artwork\\fonts\\"&&
           (!n||n>=32768||dir!=normalize(std::wstring(windows,n)+L"\\Fonts\\")))return {};
        HMODULE caller=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(address),&caller))return {};
        std::lock_guard<std::mutex> lock(mutex());auto e=entries().find(caller);if(e==entries().end())return {};
        auto f=e->second->fonts.find(name);if(f==e->second->fonts.end())return {};
        // Recheck the final target: a moved/replaced symlink must not escape root.
        auto actual=InstalledFL::canonical(f->second);
        return !actual.empty()&&InstalledFL::inside(e->second->root,actual)?actual:std::wstring();
    }
    static DWORD WINAPI attributes(LPCWSTR path){
        DWORD result=GetFileAttributesW(path);if(result!=INVALID_FILE_ATTRIBUTES)return result;
        DWORD error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return result;
        try{auto p=fallback(NPC_FONT_CALLER,path);
            if(!p.empty()){auto found=GetFileAttributesW(p.c_str());if(found!=INVALID_FILE_ATTRIBUTES&&!(found&FILE_ATTRIBUTE_DIRECTORY))return found;}
        }catch(...){ /* Never propagate C++ exceptions into a vendor Win32 call. */ }
        SetLastError(error);return result;
    }
    static HANDLE WINAPI openFile(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE model){
        HANDLE result=CreateFileW(path,access,share,security,creation,flags,model);if(result!=INVALID_HANDLE_VALUE)return result;
        DWORD error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return result;
        constexpr DWORD read=GENERIC_READ|FILE_READ_DATA|FILE_READ_EA|FILE_READ_ATTRIBUTES|READ_CONTROL|SYNCHRONIZE;
        if(creation==OPEN_EXISTING&&!(access&~read)&&!(flags&FILE_FLAG_DELETE_ON_CLOSE)){
            try{auto p=fallback(NPC_FONT_CALLER,path);
                if(!p.empty()){result=CreateFileW(p.c_str(),access,share,security,creation,flags,model);if(result!=INVALID_HANDLE_VALUE)return result;}
            }catch(...){ /* Preserve the original Win32 failure. */ }
        }
        SetLastError(error);return INVALID_HANDLE_VALUE;
    }
    static bool restore(Entry* e){
        bool complete=true;
        for(auto p:e->patches){if(*p.slot!=p.replacement)continue;DWORD old=0;
            if(VirtualProtect(p.slot,sizeof(*p.slot),PAGE_READWRITE,&old)){
                InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(p.slot),reinterpret_cast<void*>(p.original),reinterpret_cast<void*>(p.replacement));
                DWORD ignored;VirtualProtect(p.slot,sizeof(*p.slot),old,&ignored);
            }else complete=false;
        }
        return complete;
    }
    static bool acquire(void* value,const wchar_t* root){auto e=static_cast<Entry*>(value);if(_wcsicmp(e->root.c_str(),root))return false;++e->users;return true;}
    static int release(void* value){auto e=static_cast<Entry*>(value);if(--e->users)return 0;
        std::lock_guard<std::mutex> lock(mutex());
        if(!restore(e)){e->users=1;return 2;} // retain the owner if an OS page-protection failure leaves a live hook
        entries().erase(e->module);delete e;return 1;
    }
    static Entry* create(HMODULE module,const std::wstring& root){
        auto owned=std::make_unique<Entry>();auto e=owned.get();e->module=module;e->root=root;
        for(auto name:{L"ILGlyphsEx.ilfont",L"ILCursors.ilfont",L"ILLogos.ilfont"})for(auto folder:{L"\\Shared\\Artwork\\Fonts\\",L"\\Artwork\\Fonts\\"}){
            auto file=InstalledFL::canonical(root+folder+name);if(file.empty()||!InstalledFL::inside(root,file))continue;
            auto a=GetFileAttributesW(file.c_str());if(a==INVALID_FILE_ATTRIBUTES||(a&FILE_ATTRIBUTE_DIRECTORY))continue;
            e->fonts.emplace(normalize(name),file);break;
        }
        if(e->fonts.empty()){return nullptr;}
        auto b=reinterpret_cast<unsigned char*>(module);auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(b);
        auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(b+dos->e_lfanew);
        auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!dir.VirtualAddress){return nullptr;}
        std::lock_guard<std::mutex> lock(mutex());entries()[module]=e;
        try{for(auto d=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(b+dir.VirtualAddress);d->Name;++d){
            if(!d->OriginalFirstThunk)continue;
            auto o=reinterpret_cast<IMAGE_THUNK_DATA64*>(b+d->OriginalFirstThunk),i=reinterpret_cast<IMAGE_THUNK_DATA64*>(b+d->FirstThunk);
            for(;o->u1.AddressOfData;++o,++i){
                if(IMAGE_SNAP_BY_ORDINAL64(o->u1.Ordinal))continue;
                auto name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(b+o->u1.AddressOfData)->Name;
                ULONGLONG original=0,replacement=0;
                if(!std::strcmp(name,"GetFileAttributesW")){original=reinterpret_cast<ULONGLONG>(&GetFileAttributesW);replacement=reinterpret_cast<ULONGLONG>(&attributes);}
                if(!std::strcmp(name,"CreateFileW")){original=reinterpret_cast<ULONGLONG>(&CreateFileW);replacement=reinterpret_cast<ULONGLONG>(&openFile);}
                if(!original||i->u1.Function!=original)continue;DWORD old=0;
                // Allocate bookkeeping BEFORE replacing an import, so allocation
                // failure can never leave an untracked callback in the vendor DLL.
                e->patches.push_back({&i->u1.Function,original,replacement});
                if(!VirtualProtect(&i->u1.Function,sizeof(i->u1.Function),PAGE_READWRITE,&old)){e->patches.pop_back();continue;}
                auto previous=InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&i->u1.Function),reinterpret_cast<void*>(replacement),reinterpret_cast<void*>(original));
                DWORD ignored;VirtualProtect(&i->u1.Function,sizeof(i->u1.Function),old,&ignored);
                if(previous!=reinterpret_cast<void*>(original))e->patches.pop_back();
            }
        }
        }catch(...){
            if(!restore(e)){e->users=1;return owned.release();}
            entries().erase(module);return nullptr;
        }
        if(e->patches.size()<2){
            if(!restore(e)){e->users=1;return owned.release();}
            entries().erase(module);return nullptr;
        }
        e->users=1;return owned.release();
    }
    void handles(){if(shared){UnmapViewOfFile(shared);shared=nullptr;}if(mapping){CloseHandle(mapping);mapping=nullptr;}if(gate){CloseHandle(gate);gate=nullptr;}}
public:
    InstalledFonts()=default;
    InstalledFonts(const InstalledFonts&)=delete;
    InstalledFonts& operator=(const InstalledFonts&)=delete;
    ~InstalledFonts(){detach();}
    bool attach(HMODULE module,const std::wstring& selectedRoot){
        detach();if(!module||!InstalledFL::absolute(selectedRoot))return false;
        std::wstring root;try{root=InstalledFL::canonical(selectedRoot);}catch(...){return false;}if(root.empty())return false;
        wchar_t name[128]{};swprintf(name,128,L"Local\\NPC.FontLookup.1.%lu.%p",GetCurrentProcessId(),module);
        wchar_t mutexName[144]{};swprintf(mutexName,144,L"%ls.mutex",name);
        gate=CreateMutexW(nullptr,FALSE,mutexName);if(!gate)return false;
        DWORD wait=WaitForSingleObject(gate,10000);if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){handles();return false;}
        struct GateLock{HANDLE handle;bool held=true;void unlock(){if(held){ReleaseMutex(handle);held=false;}}~GateLock(){unlock();}}guard{gate};
        bool ok=false;
        try{mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name);
        if(mapping)shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
        if(shared&&shared->version==1&&shared->entry&&shared->acquire&&shared->release){
            if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(shared->acquire),&owner))ok=shared->acquire(shared->entry,root.c_str());
        }else if(shared&&!shared->version){
            if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&acquire),&owner)){
                auto e=create(module,root);if(e){*shared={1,e,&acquire,&release};ok=true;}
            }
        }
        }catch(...){ok=false;}
        guard.unlock();
        if(!ok){handles();if(owner){FreeLibrary(owner);owner=nullptr;}}else target=module;
        return ok;
    }
    void detach(){
        if(shared&&owner){DWORD wait=WaitForSingleObject(gate,INFINITE);if(wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED){
            int result=shared->release(shared->entry);
            if(result==1)*shared={};
            ReleaseMutex(gate);
            if(result==2){
                // Exceptional OS failure: retain the shared registry and its
                // owner reference rather than leave an IAT callback dangling.
                HMODULE retained=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(target),&retained);
                mapping=nullptr;shared=nullptr;gate=nullptr;owner=nullptr;target=nullptr;return;
            }
        }else{
            HMODULE retained=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(target),&retained);
            mapping=nullptr;shared=nullptr;gate=nullptr;owner=nullptr;target=nullptr;return;
        }}
        handles();auto held=owner;owner=nullptr;target=nullptr;if(held)FreeLibrary(held);
    }
};
#undef NPC_FONT_CALLER
