// SPDX-License-Identifier: MIT
// Real Win32 controls with a mock synth; native audio/voice tests are separate.
#define main adapter_baseline_main
#include "generic_adapter_test.cpp"
#undef main
static void capture(HWND h){
    RECT r{};GetClientRect(h,&r);HDC source=GetDC(h),dc=CreateCompatibleDC(source);HBITMAP bitmap=CreateCompatibleBitmap(source,r.right,r.bottom);auto old=SelectObject(dc,bitmap);RedrawWindow(GetParent(h),nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    for(int i=0;i<20;i++){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(10);}
    BitBlt(dc,0,0,r.right,r.bottom,source,0,0,SRCCOPY);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    std::vector<uint8_t>pixels(size_t(r.right)*r.bottom*4);SelectObject(dc,old);GetDIBits(dc,bitmap,0,r.bottom,pixels.data(),&info,DIB_RGB_COLORS);
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+DWORD(pixels.size());FILE*f=fopen("build/channel-panel.bmp","wb");if(f){fwrite(&header,1,sizeof(header),f);fwrite(&info.bmiHeader,1,sizeof(BITMAPINFOHEADER),f);fwrite(pixels.data(),1,pixels.size(),f);fclose(f);}DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(h,source);
}
int main(){
    moduleHandle=GetModuleHandleW(nullptr);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    auto ini=configurationPath();FILE*cfg=_wfopen(ini.c_str(),L"wb");CHECK(cfg);
    fputs("[plugin]\nname=UI test\nkind=instrument\ndll=FL/native.dll\nroot=FL\nuid=123456789ABCDEF0123456789ABCDEF0\n[dependencies]\ncount=0\n",cfg);fclose(cfg);
    auto*factory=GetPluginFactory();PClassInfo ci{};CHECK(factory->getClassInfo(0,&ci)==kResultOk);
    IComponent*c=nullptr;CHECK(factory->createInstance(ci.cid,IComponent_iid,reinterpret_cast<void**>(&c))==kResultOk);CHECK(c->initialize(nullptr)==kResultOk);
    IEditController*controller=nullptr;CHECK(c->queryInterface(IEditController_iid,reinterpret_cast<void**>(&controller))==kResultOk);
    auto*view=controller->createView(ViewType::kEditor);CHECK(view);
    HWND host=CreateWindowW(L"STATIC",L"Channel test host",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,600,500,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(host);
    CHECK(view->attached(host,kPlatformTypeHWND)==kResultOk);HWND root=GetWindow(host,GW_CHILD),toggle=GetDlgItem(root,channelChoose);CHECK(toggle);
    CHECK(!(GetWindowLongPtrW(toggle,GWL_STYLE)&WS_TABSTOP));
    SetFocus(host);auto before=GetFocus();CHECK(before==host);
    SendMessageW(toggle,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));SendMessageW(toggle,WM_LBUTTONUP,0,MAKELPARAM(10,10));
    CHECK(GetFocus()==before);HWND panel=FindWindowExW(root,nullptr,L"FLBridgeChannelPanel",nullptr);CHECK(panel&&IsWindowVisible(panel));
    capture(root);
    SendMessageW(toggle,WM_KEYDOWN,VK_SPACE,0);SendMessageW(toggle,WM_KEYUP,VK_SPACE,0);CHECK(IsWindowVisible(panel));
    SetDlgItemInt(panel,0x710,6,TRUE);SendDlgItemMessageW(panel,0x711,BM_SETCHECK,BST_CHECKED,0);SendDlgItemMessageW(panel,0x712,BM_SETCHECK,BST_CHECKED,0);
    SetDlgItemInt(panel,0x713,330,TRUE);SetDlgItemInt(panel,0x714,48,TRUE);SetDlgItemInt(panel,0x715,-2,TRUE);SetDlgItemInt(panel,0x716,25,TRUE);SetDlgItemInt(panel,0x717,70,TRUE);SetDlgItemInt(panel,0x718,-30,TRUE);
    SendDlgItemMessageW(panel,0x71a,BM_CLICK,0,0);
    auto*plugin=static_cast<Plugin*>(c);auto settings=plugin->engine.channelSettings();CHECK(settings.maxPolyphony==6&&settings.mono&&settings.portamento&&settings.slideMs==330&&settings.rootNote==48&&settings.transpose==-2&&settings.fineCents==25&&settings.gain==0.7f&&settings.pan==-0.3f);
    SetDlgItemTextW(panel,0x710,L"999");SendDlgItemMessageW(panel,0x71a,BM_CLICK,0,0);CHECK(plugin->engine.channelSettings().maxPolyphony==6);
    Stream state;CHECK(c->getState(&state)==kResultOk);SendDlgItemMessageW(panel,0x719,BM_CLICK,0,0);CHECK(plugin->engine.channelSettings().maxPolyphony==256);
    state.pos=0;CHECK(c->setState(&state)==kResultOk);SendMessageW(root,WM_TIMER,1,0);CHECK(GetDlgItemInt(panel,0x710,nullptr,TRUE)==6);
    SendMessageW(toggle,BM_CLICK,0,0);CHECK(!IsWindowVisible(panel));ViewRect size{};CHECK(view->getSize(&size)==kResultOk);CHECK(size.getWidth()==400&&size.getHeight()==328);
    CHECK(view->removed()==kResultOk);CHECK(view->attached(host,kPlatformTypeHWND)==kResultOk);root=GetWindow(host,GW_CHILD);SendDlgItemMessageW(root,channelChoose,BM_CLICK,0,0);panel=FindWindowExW(root,nullptr,L"FLBridgeChannelPanel",nullptr);CHECK(panel&&GetDlgItemInt(panel,0x710,nullptr,TRUE)==6);
    CHECK(view->removed()==kResultOk);view->release();DestroyWindow(host);controller->release();CHECK(c->terminate()==kResultOk);c->release();factory->release();
    puts("PASS Channel visibility, Apply/Reset, bounds, v4 state, restore/reopen, native dimensions, toolbar mouse focus and Space suppression");return 0;
}
