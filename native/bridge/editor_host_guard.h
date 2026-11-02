// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <vector>

// Delphi modal forms can disable every top-level window on the DAW's UI thread.
// This guard only counters a disable whose nearest non-system caller is this
// instance's exact native module. A DAW-owned modal, including one entered from
// a native nested message loop, is left alone. No process-wide window enabling.
class EditorHostGuard {
public:
    ~EditorHostGuard(){ detach(); }
    void attach(HWND host, HWND view, HWND native, HWND proxy, HMODULE nativeModule) {
        detach(); this->view=view; this->native=native; this->proxy=proxy; this->nativeModule=nativeModule;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&EditorHostGuard::windowProc), &bridgeModule);
        DWORD n=GetSystemDirectoryW(systemDirectory.data(),DWORD(systemDirectory.size()));
        systemLength=n<systemDirectory.size()?n:0;
        HWND top=GetAncestor(host,GA_ROOT);hostRoot=top;
        for(int depth=0;top&&depth<32;++depth,top=GetWindow(top,GW_OWNER)) {
            DWORD pid=0;DWORD thread=GetWindowThreadProcessId(top,&pid);
            if(pid!=GetCurrentProcessId()||thread!=GetCurrentThreadId()||top==view||top==native||top==proxy)continue;
            if(std::any_of(hosts.begin(),hosts.end(),[top](const Host&x){return x.window==top;}))break;
            hosts.push_back({top,IsWindowEnabled(top)!=FALSE});
            if(!SetWindowSubclass(top,windowProc,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this)))hosts.pop_back();
        }
        refresh();
    }
    void detach() {
        for(const auto& host:hosts)if(IsWindow(host.window))RemoveWindowSubclass(host.window,windowProc,reinterpret_cast<UINT_PTR>(this));
        hosts.clear();
        if(viewSuspended&&viewWasEnabled&&IsWindow(view))EnableWindow(view,TRUE);
        viewSuspended=false;viewWasEnabled=false;view=native=proxy=hostRoot=nullptr;nativeModule=bridgeModule=nullptr;
    }
    bool ownsPopup(HWND window) const {
        if(!window||window==view||window==native||window==proxy||IsChild(view,window))return false;
        DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid!=GetCurrentProcessId())return false;
        HWND cursor=window;
        for(int depth=0;cursor&&depth<32;++depth) {
            if(cursor==view||cursor==native)return true;
            // Native Application ownership is shared across instances of one DLL.
            // Only the instance that held keyboard focus claims proxy-owned popups.
            if(cursor==proxy)return lastFocusInside;
            HWND owner=GetWindow(cursor,GW_OWNER);
            if(!owner){HWND parent=GetParent(cursor);if(parent==cursor)break;owner=parent;}
            if(owner==cursor)break;cursor=owner;
        }
        return false;
    }
    void refresh() {
        HWND focus=GetFocus();if(focus)lastFocusInside=focus==view||focus==native||IsChild(view,focus)||ownsPopup(GetAncestor(focus,GA_ROOT));
        if(viewSuspended&&GetTickCount()-suspensionTime>250&&!hasVisiblePopup()) {
            if(viewWasEnabled&&IsWindow(view))EnableWindow(view,TRUE);
            viewSuspended=false;viewWasEnabled=false;
        }
    }
private:
    struct Host {HWND window;bool enabled;};
    std::vector<Host> hosts;
    HWND view=nullptr,native=nullptr,proxy=nullptr,hostRoot=nullptr;
    HMODULE nativeModule=nullptr,bridgeModule=nullptr;
    std::array<wchar_t,32768> systemDirectory{};size_t systemLength=0;
    bool restoring=false,lastFocusInside=false,viewSuspended=false,viewWasEnabled=false;
    DWORD suspensionTime=0;
    bool hasVisiblePopup() const {
        struct Search{const EditorHostGuard*self;bool found=false;} search{this};
        EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM value)->BOOL {
            auto&s=*reinterpret_cast<Search*>(value);if(IsWindowVisible(hwnd)&&s.self->ownsPopup(hwnd)){s.found=true;return FALSE;}return TRUE;
        },reinterpret_cast<LPARAM>(&search));return search.found;
    }
    bool instanceFocused() const {
        HWND focus=GetFocus();if(focus&&(focus==view||focus==native||IsChild(view,focus)))return true;
        HWND active=GetActiveWindow();return ownsPopup(active)||(active==hostRoot&&lastFocusInside);
    }
    bool nativeDisableOrigin() const {
        if(!nativeModule||!bridgeModule||!systemLength)return false;
        void* frames[40]{};USHORT count=CaptureStackBackTrace(0,40,frames,nullptr);
        for(USHORT i=0;i<count;++i){
            HMODULE module=nullptr;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(frames[i]),&module))return false;
            if(module==bridgeModule)continue;
            if(module==nativeModule)return true;
            wchar_t path[32768]{};DWORD n=GetModuleFileNameW(module,path,32768);
            if(n>systemLength&&_wcsnicmp(path,systemDirectory.data(),systemLength)==0&&(path[systemLength]==L'\\'||path[systemLength]==L'/'))continue;
            // This is the immediate application caller (for example Ableton).
            // Do not mistake a deeper native ShowModal/message-loop frame for it.
            return false;
        }
        return false;
    }
    static LRESULT CALLBACK windowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR ref) {
        auto&self=*reinterpret_cast<EditorHostGuard*>(ref);
        auto it=std::find_if(self.hosts.begin(),self.hosts.end(),[hwnd](const Host&h){return h.window==hwnd;});
        if(message==WM_ENABLE&&it!=self.hosts.end()) {
            bool previous=it->enabled;it->enabled=wp!=0;
            if(!wp&&previous&&!self.restoring&&self.instanceFocused()&&self.nativeDisableOrigin()) {
                self.restoring=true;EnableWindow(hwnd,TRUE);self.restoring=false;
                if(!self.viewSuspended&&IsWindow(self.view)) {
                    self.viewWasEnabled=IsWindowEnabled(self.view)!=FALSE;self.viewSuspended=true;self.suspensionTime=GetTickCount();
                    if(self.viewWasEnabled)EnableWindow(self.view,FALSE);
                }
                // The nested EnableWindow(TRUE) already notified the host.
                // Do not deliver the superseded FALSE notification afterwards.
                return 0;
            }
        }
        if(message==WM_NCDESTROY){if(it!=self.hosts.end())self.hosts.erase(it);RemoveWindowSubclass(hwnd,windowProc,id);}
        return DefSubclassProc(hwnd,message,wp,lp);
    }
};
