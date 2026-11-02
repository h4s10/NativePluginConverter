// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <cwctype>
#include <cstdio>

// Resolve existing files only. Never copy, register, modify or execute vendor code.
struct InstalledFL {
    static bool absolute(const std::wstring& p) {
        if(p.size()<3||!((p[0]>=L'A'&&p[0]<=L'Z')||(p[0]>=L'a'&&p[0]<=L'z'))||p[1]!=L':'||(p[2]!=L'\\'&&p[2]!=L'/'))return false;
        for(size_t i=2;i<p.size();++i)if(p[i]<32||p[i]==L':'||p[i]==L'"'||p[i]==L'|'||p[i]==L'?'||p[i]==L'*'||p[i]==L'<'||p[i]==L'>')return false;
        return true;
    }
    static std::wstring environment(const wchar_t* name) {
        wchar_t b[32768]{};DWORD n=GetEnvironmentVariableW(name,b,32768);return n&&n<32768?std::wstring(b,n):std::wstring();
    }
    static std::wstring canonical(const std::wstring& p) {
        HANDLE f=CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        if(f==INVALID_HANDLE_VALUE)return {};
        wchar_t b[32768]{};DWORD n=GetFinalPathNameByHandleW(f,b,32768,FILE_NAME_NORMALIZED);CloseHandle(f);
        if(!n||n>=32768)return {};
        std::wstring s(b,n);if(s.rfind(L"\\\\?\\",0)==0)s.erase(0,4);
        while(s.size()>3&&(s.back()==L'\\'||s.back()==L'/'))s.pop_back();
        return s;
    }
    static bool inside(const std::wstring& root,const std::wstring& file) {
        auto prefix=root;if(prefix.back()!=L'\\')prefix+=L'\\';
        return file.size()>prefix.size()&&_wcsnicmp(prefix.c_str(),file.c_str(),prefix.size())==0;
    }
    static bool match(const std::wstring& candidate,const std::wstring& dll,const std::vector<std::wstring>& deps,
                      std::wstring& root,std::wstring& native,std::vector<std::wstring>& paths) {
        if(!absolute(candidate))return false;
        DWORD attr=GetFileAttributesW(candidate.c_str());if(attr==INVALID_FILE_ATTRIBUTES||!(attr&FILE_ATTRIBUTE_DIRECTORY))return false;
        auto actual=canonical(candidate);if(actual.empty()||!absolute(actual))return false;
        auto file=[&](const std::wstring& rel)->std::wstring{
            auto p=canonical(actual+L"\\"+rel);if(p.empty()||!inside(actual,p))return {};
            DWORD a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY)?p:std::wstring();
        };
        auto main=file(dll);if(main.empty())return false;
        std::vector<std::wstring> resolved;for(auto& p:deps){auto f=file(p);if(f.empty())return false;resolved.push_back(f);}
        root=actual;native=main;paths=std::move(resolved);return true;
    }
    static std::wstring selection(bool& exists,bool& valid) {
        exists=false;valid=true;wchar_t local[MAX_PATH]{};
        if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local)))return {};
        auto file=std::wstring(local)+L"\\NativePluginConverter\\fl-studio-path.txt";
        if(GetFileAttributesW(file.c_str())==INVALID_FILE_ATTRIBUTES)return {};
        exists=true;FILE*f=_wfopen(file.c_str(),L"rb");if(!f){valid=false;return {};}
        char b[131073]{};size_t n=fread(b,1,sizeof(b),f);bool bad=ferror(f)!=0||n==sizeof(b);fclose(f);
        if(bad){valid=false;return {};}
        std::string text(b,n);if(text.compare(0,3,"\xEF\xBB\xBF")==0)text.erase(0,3);
        while(!text.empty()&&(text.back()=='\r'||text.back()=='\n'))text.pop_back();
        if(text.empty()||text.find('\0')!=std::string::npos){valid=false;return {};}
        int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
        if(count<=0){valid=false;return {};}
        std::wstring p(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),p.data(),count);
        valid=absolute(p);return p;
    }
    static bool choose(const std::vector<std::wstring>& candidates,const std::wstring& dll,const std::vector<std::wstring>& deps,
                       std::wstring& root,std::wstring& native,std::vector<std::wstring>& paths,std::string& error) {
        std::vector<std::wstring> roots;
        for(const auto& p:candidates){std::wstring r,n;std::vector<std::wstring>d;if(!match(p,dll,deps,r,n,d))continue;
            bool duplicate=false;for(auto& old:roots)if(!_wcsicmp(old.c_str(),r.c_str()))duplicate=true;
            if(duplicate)continue;roots.push_back(r);root=r;native=n;paths=std::move(d);
        }
        if(roots.size()==1)return true;
        root.clear();native.clear();paths.clear();
        error=roots.empty()?"No suitable FL Studio installation found. Select its folder in NativePluginConverter on this Windows computer, or set [installation] root in bridge.ini.":
            "Multiple FL Studio installations match. Select one in NativePluginConverter, or set [installation] root in bridge.ini.";
        return false;
    }
    static bool resolve(const std::wstring& explicitRoot,const std::wstring& dll,const std::vector<std::wstring>& deps,
                        std::wstring& root,std::wstring& native,std::vector<std::wstring>& paths,std::string& error) {
        auto p=explicitRoot;
        if(p.empty())p=environment(L"NPC_FL_STUDIO_ROOT");
        if(p.empty()){
            bool exists=false,valid=true;p=selection(exists,valid);
            if(exists&&!valid){error="Invalid saved FL Studio path. Select its installation in NativePluginConverter again.";return false;}
        }
        if(!p.empty()){
            if(match(p,dll,deps,root,native,paths))return true;
            error="Selected FL Studio installation is missing the plugin/dependencies, or a DLL resolves outside it. Re-select the folder in NativePluginConverter or update [installation] root in bridge.ini.";return false;
        }
        std::vector<std::wstring> candidates;
        for(auto var:{L"ProgramW6432",L"ProgramFiles",L"ProgramFiles(x86)"}){
            auto base=environment(var);if(!absolute(base))continue;base+=L"\\Image-Line\\";
            WIN32_FIND_DATAW entry{};HANDLE h=FindFirstFileW((base+L"FL Studio*").c_str(),&entry);if(h==INVALID_HANDLE_VALUE)continue;
            do{if(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)candidates.push_back(base+entry.cFileName);}while(candidates.size()<128&&FindNextFileW(h,&entry));FindClose(h);
        }
        return choose(candidates,dll,deps,root,native,paths,error);
    }
};
