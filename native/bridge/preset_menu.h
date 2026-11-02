// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <map>
#include <string>

// Factory subdirectories become nested menus; leaf IDs retain Engine indices.
class PresetMenu {
public:
    PresetMenu():root(CreatePopupMenu()){if(root)folders[L""]=root;}
    ~PresetMenu(){if(root)DestroyMenu(root);}
    PresetMenu(const PresetMenu&)=delete;
    PresetMenu& operator=(const PresetMenu&)=delete;
    HMENU handle()const{return root;}
    bool add(unsigned id,const std::wstring& label,bool checked=false){
        if(!root||!id||label.empty())return false;
        HMENU parent=root;std::wstring key;size_t start=0;
        while(start<label.size()){
            auto slash=label.find(L'/',start);auto part=label.substr(start,slash==std::wstring::npos?std::wstring::npos:slash-start);
            if(slash==std::wstring::npos)return !part.empty()&&AppendMenuW(parent,MF_STRING|(checked?MF_CHECKED:0),id,caption(part).c_str());
            start=slash+1;if(part.empty())continue;
            key+=part+L"/";auto found=folders.find(key);
            if(found==folders.end()){
                HMENU child=CreatePopupMenu();if(!child)return false;
                if(!AppendMenuW(parent,MF_POPUP,reinterpret_cast<UINT_PTR>(child),caption(part).c_str())){DestroyMenu(child);return false;}
                folders.emplace(key,child);parent=child;
            }else parent=found->second;
        }
        return false;
    }
private:
    HMENU root=nullptr;std::map<std::wstring,HMENU>folders;
    static std::wstring caption(const std::wstring& label){std::wstring text;for(auto ch:label){text+=ch;if(ch==L'&')text+=ch;}return text;}
};
