// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <commctrl.h>

// Preset controls are mouse tools, not keyboard targets. In particular clicking
// Next must not steal the DAW's focus and turn Space into another preset change.
inline LRESULT CALLBACK toolbarButtonProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
    if(m==WM_LBUTTONDOWN||m==WM_LBUTTONDBLCLK){SetCapture(h);SendMessageW(h,BM_SETSTATE,TRUE,0);return 0;}
    if(m==WM_MOUSEMOVE&&GetCapture()==h){RECT r{};GetClientRect(h,&r);POINT p{short(LOWORD(l)),short(HIWORD(l))};SendMessageW(h,BM_SETSTATE,PtInRect(&r,p),0);return 0;}
    if(m==WM_LBUTTONUP){
        if(GetCapture()==h){ReleaseCapture();SendMessageW(h,BM_SETSTATE,FALSE,0);RECT r{};GetClientRect(h,&r);POINT p{short(LOWORD(l)),short(HIWORD(l))};
            if(PtInRect(&r,p))SendMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(h),BN_CLICKED),reinterpret_cast<LPARAM>(h));}
        return 0;
    }
    if(m==WM_CAPTURECHANGED){SendMessageW(h,BM_SETSTATE,FALSE,0);return 0;}
    if(m==WM_KEYDOWN||m==WM_KEYUP||m==WM_CHAR)return 0;
    if(m==WM_GETDLGCODE)return 0;
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,toolbarButtonProc,id);
    return DefSubclassProc(h,m,w,l);
}
inline void configureToolbarButton(HWND h){
    SetWindowLongPtrW(h,GWL_STYLE,GetWindowLongPtrW(h,GWL_STYLE)&~WS_TABSTOP);
    SetWindowSubclass(h,toolbarButtonProc,0x461,0);
}
