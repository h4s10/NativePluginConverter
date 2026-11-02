// SPDX-License-Identifier: MIT
#pragma once
#include "engine.h"
#include <windows.h>
#include <commctrl.h>
#include <cwchar>

// Host-owned channel controls. The original FL Channel Settings form lives in
// FLEngine, not in a synth DLL; this panel implements the supported bridge subset.
class ChannelPanel {
public:
    static constexpr int minWidth=460, minHeight=332;
    ~ChannelPanel(){if(window)DestroyWindow(window);if(font)DeleteObject(font);if(brush)DeleteObject(brush);}
    HWND handle()const{return window;}
    bool create(HWND parent,Engine* value,void* ctx,void(*changed)(void*)){
        engine=value;context=ctx;notify=changed;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_UPDOWN_CLASS};InitCommonControlsEx(&controls);
        HINSTANCE instance=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent,GWLP_HINSTANCE));
        WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=proc;wc.lpszClassName=L"FLBridgeChannelPanel";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        brush=CreateSolidBrush(RGB(32,40,43));font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        window=CreateWindowExW(0,wc.lpszClassName,L"",WS_CHILD|WS_CLIPCHILDREN,0,28,minWidth,minHeight,parent,nullptr,instance,this);
        if(!window)return false;
        auto control=[&](const wchar_t*cls,const wchar_t*text,DWORD style,int id,int x,int y,int w,int h){
            HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
            SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;
        };
        auto label=[&](const wchar_t*text,int x,int y,int w=130){return control(L"STATIC",text,SS_LEFT,0,x,y,w,22);};
        auto number=[&](int id,int x,int y,int lo,int hi){
            HWND edit=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_RIGHT|ES_AUTOHSCROLL,id,x,y,76,24);
            SendMessageW(edit,EM_SETLIMITTEXT,6,0);
            HWND spin=control(UPDOWN_CLASSW,L"",UDS_ALIGNRIGHT|UDS_SETBUDDYINT|UDS_ARROWKEYS|UDS_NOTHOUSANDS,0,x+60,y,16,24);
            SendMessageW(spin,UDM_SETBUDDY,reinterpret_cast<WPARAM>(edit),0);SendMessageW(spin,UDM_SETRANGE32,lo,hi);
        };
        label(L"Channel settings",18,15,420);
        label(L"Max voices",18,51);number(Max,148,48,1,256);
        control(L"BUTTON",L"Mono",WS_TABSTOP|BS_AUTOCHECKBOX,Mono,264,48,164,24);
        label(L"Glide time (ms)",18,87);number(Slide,148,84,0,5000);
        control(L"BUTTON",L"Porta (Mono)",WS_TABSTOP|BS_AUTOCHECKBOX,Porta,264,84,174,24);
        label(L"Root note (MIDI)",18,123);number(Root,148,120,0,127);
        label(L"60 = C3 in Ableton / C5 in FL",238,123,212);
        label(L"Transpose (st)",18,159);number(Transpose,148,156,-48,48);
        label(L"Fine (cents)",250,159,100);number(Fine,358,156,-100,100);
        label(L"Volume (%)",18,195);number(Gain,148,192,0,200);
        label(L"Pan (-L / +R)",250,195,106);number(Pan,358,192,-100,100);
        label(L"Porta glides between overlapping notes with Mono enabled.",18,232,430);
        status=label(L"Saved with the DAW project. Press Apply to use changes.",18,254,430);
        control(L"BUTTON",L"Reset",WS_TABSTOP|BS_PUSHBUTTON,Reset,248,288,90,28);
        control(L"BUTTON",L"Apply",WS_TABSTOP|BS_DEFPUSHBUTTON,Apply,348,288,90,28);
        EnableWindow(GetDlgItem(window,Porta),engine->supportsPortamento());
        refresh(true);return true;
    }
    void refresh(bool force=false){
        if(!window||!engine)return;auto s=engine->channelSettings();
        if(!force&&equal(s,shown))return;shown=s;
        set(Max,s.maxPolyphony);set(Slide,s.slideMs);set(Root,s.rootNote);set(Transpose,s.transpose);set(Fine,s.fineCents);set(Gain,int(std::lround(s.gain*100)));set(Pan,int(std::lround(s.pan*100)));
        SendDlgItemMessageW(window,Mono,BM_SETCHECK,s.mono?BST_CHECKED:BST_UNCHECKED,0);
        SendDlgItemMessageW(window,Porta,BM_SETCHECK,s.portamento?BST_CHECKED:BST_UNCHECKED,0);
    }
private:
    enum {Max=0x710,Mono,Porta,Slide,Root,Transpose,Fine,Gain,Pan,Reset,Apply};
    HWND window=nullptr,status=nullptr;HFONT font=nullptr;HBRUSH brush=nullptr;Engine*engine=nullptr;void*context=nullptr;void(*notify)(void*)=nullptr;ChannelSettings shown{};
    static bool equal(const ChannelSettings&a,const ChannelSettings&b){return a.maxPolyphony==b.maxPolyphony&&a.mono==b.mono&&a.portamento==b.portamento&&a.slideMs==b.slideMs&&a.rootNote==b.rootNote&&a.transpose==b.transpose&&a.fineCents==b.fineCents&&a.gain==b.gain&&a.pan==b.pan;}
    void set(int id,int value){SetDlgItemInt(window,id,value,TRUE);}
    bool get(int id,int lo,int hi,int&out){wchar_t s[32]{};GetDlgItemTextW(window,id,s,32);wchar_t*end=nullptr;long n=wcstol(s,&end,10);if(!*s||end==s||*end||n<lo||n>hi)return false;out=int(n);return true;}
    void apply(){
        ChannelSettings s;int gain=100,pan=0;
        if(!get(Max,1,256,s.maxPolyphony)||!get(Slide,0,5000,s.slideMs)||!get(Root,0,127,s.rootNote)||!get(Transpose,-48,48,s.transpose)||!get(Fine,-100,100,s.fineCents)||!get(Gain,0,200,gain)||!get(Pan,-100,100,pan)){
            SetWindowTextW(status,L"Check values: voices 1-256, root 0-127, glide 0-5000 ms.");return;}
        s.gain=gain/100.f;s.pan=pan/100.f;s.mono=SendDlgItemMessageW(window,Mono,BM_GETCHECK,0,0)==BST_CHECKED;
        s.portamento=engine->supportsPortamento()&&SendDlgItemMessageW(window,Porta,BM_GETCHECK,0,0)==BST_CHECKED;
        engine->setChannelSettings(s);refresh(true);SetWindowTextW(status,L"Applied. These settings are saved with the DAW project.");if(notify)notify(context);
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto*self=reinterpret_cast<ChannelPanel*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<ChannelPanel*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self){
            if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(w),&r,self->brush);return 1;}
            if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT||m==WM_CTLCOLORBTN){auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,RGB(220,229,225));SetBkColor(dc,RGB(32,40,43));return reinterpret_cast<LRESULT>(self->brush);}
            if(m==WM_COMMAND&&HIWORD(w)==BN_CLICKED){
                if(LOWORD(w)==Apply){self->apply();return 0;}
                if(LOWORD(w)==Reset){self->engine->setChannelSettings(ChannelSettings{});self->refresh(true);SetWindowTextW(self->status,L"Default channel settings restored.");if(self->notify)self->notify(self->context);return 0;}
            }
            if(m==WM_NCDESTROY){self->window=nullptr;SetWindowLongPtrW(h,GWLP_USERDATA,0);}
        }
        return DefWindowProcW(h,m,w,l);
    }
};
