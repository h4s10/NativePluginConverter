// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <vector>

// Hooks only this editor subtree. Normal native right-click menus are preserved.
class EditorShortcuts {
public:
    using OpenMenu=void(*)(void*,POINT);
    ~EditorShortcuts(){detach();}
    void attach(HWND root,void* context,OpenMenu callback){detach();this->context=context;this->callback=callback;refresh(root);}
    void refresh(HWND root){if(!root||!callback)return;install(root);EnumChildWindows(root,[](HWND h,LPARAM p)->BOOL{reinterpret_cast<EditorShortcuts*>(p)->install(h);return TRUE;},reinterpret_cast<LPARAM>(this));}
    void detach(){for(HWND h:windows)if(IsWindow(h))RemoveWindowSubclass(h,proc,reinterpret_cast<UINT_PTR>(this));windows.clear();callback=nullptr;context=nullptr;}
private:
    std::vector<HWND>windows;void*context=nullptr;OpenMenu callback=nullptr;
    void install(HWND h){if(GetWindowThreadProcessId(h,nullptr)!=GetCurrentThreadId()||std::find(windows.begin(),windows.end(),h)!=windows.end())return;if(SetWindowSubclass(h,proc,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this)))windows.push_back(h);}
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref){
        auto&self=*reinterpret_cast<EditorShortcuts*>(ref);
        const bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(self.callback){
            if(m==WM_KEYDOWN&&w==VK_F4&&!shift&&!(GetKeyState(VK_CONTROL)&0x8000)&&!(GetKeyState(VK_MENU)&0x8000)){if(!(l&(1L<<30))){POINT p{12,12};ClientToScreen(h,&p);self.callback(self.context,p);}return 0;}
            if(m==WM_RBUTTONDOWN&&shift)return 0;
            if(m==WM_RBUTTONUP&&shift){POINT p{short(LOWORD(l)),short(HIWORD(l))};ClientToScreen(h,&p);self.callback(self.context,p);return 0;}
            if(m==WM_CONTEXTMENU&&shift){POINT p{short(LOWORD(l)),short(HIWORD(l))};if(p.x==-1&&p.y==-1){p={12,12};ClientToScreen(h,&p);}self.callback(self.context,p);return 0;}
        }
        if(m==WM_NCDESTROY){self.windows.erase(std::remove(self.windows.begin(),self.windows.end(),h),self.windows.end());RemoveWindowSubclass(h,proc,id);}
        return DefSubclassProc(h,m,w,l);
    }
};
