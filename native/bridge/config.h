// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <array>
#include <cwctype>
#include <cstdio>
#include "installed_fl.h"

// UTF-8 configuration produced by the converter. Loading metadata never executes DLLs.
struct BridgeConfig {
    bool valid=false, instrument=false, installed=false;
    std::string error, nameUtf8;
    std::wstring iniPath, baseDir, dllPath, rootPath, name;
    std::array<char,16> uid{};
    std::vector<std::wstring> dependencyPaths, dependencyRelatives;
    std::wstring dllRelative, installationRoot;
    bool resolveInstallation() {
        if(!valid)return false;
        if(!installed)return true; // Read older locally bundled configurations without changing IDs/states.
        return InstalledFL::resolve(installationRoot,dllRelative,dependencyRelatives,rootPath,dllPath,dependencyPaths,error);
    }
    static std::wstring wide(const std::string& s) {
        if(s.empty()) return {};
        int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
        if(!n)return {};
        std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n);return out;
    }
    static std::string trim(std::string s) {
        const auto first=s.find_first_not_of(" \t\r\n"),last=s.find_last_not_of(" \t\r\n");
        return first==std::string::npos?std::string():s.substr(first,last-first+1);
    }
    static bool relative(const std::wstring& text) {
        if(text.empty()||text.front()==L'/'||text.front()==L'\\'||text.find(L':')!=std::wstring::npos)return false;
        size_t start=0;
        while(start<text.size()) {
            auto end=text.find_first_of(L"/\\",start);if(end==std::wstring::npos)end=text.size();
            auto part=text.substr(start,end-start);
            if(part.empty()||part==L"."||part==L".."||part.back()==L'.'||part.back()==L' '||part.find_first_of(L"<>\"|?*")!=std::wstring::npos)return false;
            for(auto c:part)if(c<32)return false;
            start=end+1;
        }
        return true;
    }
    static BridgeConfig load(const wchar_t* input) {
        BridgeConfig c;
        auto fail=[&](const char* why){c.error=why;return c;};
        if(!input||!*input)return fail("Missing bridge.ini path");
        wchar_t full[32768]{};DWORD n=GetFullPathNameW(input,32768,full,nullptr);
        if(!n||n>=32768)return fail("Invalid bridge.ini path");
        c.iniPath.assign(full,n);const auto slash=c.iniPath.find_last_of(L"/\\");
        if(slash==std::wstring::npos)return fail("Invalid bridge.ini directory");
        c.baseDir=c.iniPath.substr(0,slash+1);
        FILE* f=_wfopen(c.iniPath.c_str(),L"rb");if(!f)return fail("Missing bridge.ini");
        std::string text;char buf[4096];size_t got=0;
        while((got=fread(buf,1,sizeof(buf),f))>0){text.append(buf,got);if(text.size()>1024*1024){fclose(f);return fail("bridge.ini is too large");}}
        bool ioError=ferror(f)!=0;fclose(f);if(ioError)return fail("Cannot read bridge.ini");
        if(text.find('\0')!=std::string::npos)return fail("Invalid bridge.ini encoding");
        if(text.compare(0,3,"\xEF\xBB\xBF")==0)text.erase(0,3);
        if(!text.empty()&&wide(text).empty())return fail("bridge.ini must be UTF-8");
        std::map<std::string,std::string> entries;std::string section;
        for(size_t start=0;start<text.size();) {
            auto end=text.find('\n',start);if(end==std::string::npos)end=text.size();
            auto line=trim(text.substr(start,end-start));start=end+1;
            if(line.empty()||line[0]==';'||line[0]=='#')continue;
            if(line[0]=='['&&line.back()==']'){section=trim(line.substr(1,line.size()-2));continue;}
            auto eq=line.find('=');if(eq==std::string::npos)return fail("Invalid bridge.ini entry");
            auto key=section+"."+trim(line.substr(0,eq));
            if(entries.count(key))return fail("Duplicate bridge.ini entry");
            entries[key]=trim(line.substr(eq+1));
        }
        c.nameUtf8=entries["plugin.name"];c.name=wide(c.nameUtf8);
        if(c.name.empty()||c.name.size()>255)return fail("Invalid plugin name");
        auto kind=entries["plugin.kind"];if(kind!="instrument"&&kind!="effect")return fail("Invalid plugin kind");
        c.instrument=kind=="instrument";
        auto uid=entries["plugin.uid"];if(uid.size()!=32)return fail("Invalid plugin UID");
        auto hex=[](char v)->int{if(v>='0'&&v<='9')return v-'0';if(v>='a'&&v<='f')return v-'a'+10;if(v>='A'&&v<='F')return v-'A'+10;return -1;};
        bool nonzero=false;
        for(int i=0;i<16;i++){int a=hex(uid[i*2]),b=hex(uid[i*2+1]);if(a<0||b<0)return fail("Invalid plugin UID");c.uid[i]=char(a*16+b);nonzero|=c.uid[i]!=0;}
        if(!nonzero)return fail("Zero plugin UID");
        auto dll=wide(entries["plugin.dll"]),root=wide(entries["plugin.root"]);
        auto mode=entries["plugin.source"];
        if(!mode.empty()&&mode!="installed")return fail("Unsupported plugin source mode");
        c.installed=mode=="installed";
        if(c.installed){
            if(!relative(dll))return fail("Invalid relative installed plugin path");
            auto normalized=dll;for(auto&ch:normalized){if(ch==L'\\')ch=L'/';ch=towlower(ch);}
            if(normalized.rfind(L"plugins/fruity/",0)!=0)return fail("Installed plugin must be inside Plugins/Fruity");
            c.dllRelative=dll;c.installationRoot=wide(entries["installation.root"]);
            if(!c.installationRoot.empty()&&!InstalledFL::absolute(c.installationRoot))return fail("Installation root must be an absolute Windows drive path");
        }else{
            if(!relative(dll)||!relative(root))return fail("Plugin paths must stay inside the bundle");
            c.dllPath=c.baseDir+dll;c.rootPath=c.baseDir+root;
        }
        const auto count=entries["dependencies.count"];int total=0;
        if(count.empty()||count.size()>4)return fail("Invalid dependency count");
        for(char v:count){if(v<'0'||v>'9')return fail("Invalid dependency count");total=total*10+v-'0';}
        if(total>512)return fail("Too many dependencies");
        for(int i=0;i<total;i++) {
            auto path=wide(entries["dependencies.dll"+std::to_string(i)]);
            if(!relative(path))return fail("Dependency paths must stay inside the bundle");
            if(c.installed)c.dependencyRelatives.push_back(path);else c.dependencyPaths.push_back(c.baseDir+path);
        }
        c.valid=true;return c;
    }
};
