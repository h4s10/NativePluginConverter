// SPDX-License-Identifier: MIT
#define wmain baseline_integration_main
#include "native_vst3_test.cpp"
#undef wmain
static DWORD WINAPI watchdog(void*){Sleep(45000);TerminateProcess(GetCurrentProcess(),124);return 0;}
int wmain(int argc,wchar_t**argv){
 if(argc<2)return 2;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,fail);CloseHandle(CreateThread(nullptr,0,watchdog,nullptr,0,nullptr));CHECK(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)));
 std::wstring ini=argv[1];ini.resize(ini.find_last_of(L"/\\")+1);ini+=L"bridge.ini";auto config=BridgeConfig::load(ini.c_str());CHECK(config.valid);
 HMODULE module=LoadLibraryW(argv[1]);CHECK(module);auto factory=((IPluginFactory*(*)())GetProcAddress(module,"GetPluginFactory"))();CHECK(factory);PClassInfo info{};CHECK(factory->getClassInfo(0,&info)==kResultOk);
 Instance instance;instance.create(factory,info.cid,config,nullptr,44100,512);
 HWND host=CreateWindowW(L"STATIC",L"Preset bridge verification",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1400,1000,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(host);Frame frame;frame.hwnd=host;
 Stream selectedState;std::wstring savedCaption;
 for(int pass=0;pass<2;pass++){
  auto*view=instance.e->createView(ViewType::kEditor);CHECK(view);CHECK(view->setFrame(&frame)==kResultOk);CHECK(view->attached(host,kPlatformTypeHWND)==kResultOk);pump(100);
  HWND root=GetWindow(host,GW_CHILD),bar=GetDlgItem(root,0x682),next=GetDlgItem(root,0x683),previous=GetDlgItem(root,0x681);CHECK(root&&bar&&next&&previous);
  HWND panel=FindWindowExW(root,nullptr,L"FLNativeVST3BridgeWindow",nullptr),native=GetWindow(panel,GW_CHILD);CHECK(panel&&native);
  RECT n{},r{},p{};GetClientRect(native,&n);GetClientRect(root,&r);GetClientRect(panel,&p);CHECK(r.right==n.right&&r.bottom==n.bottom+28&&p.right==n.right&&p.bottom==n.bottom);
  POINT pt{0,0};MapWindowPoints(panel,root,&pt,1);CHECK(pt.x==0&&pt.y==28);CHECK(!(GetWindowLongPtrW(root,GWL_STYLE)&(WS_BORDER|WS_DLGFRAME)));
  if(pass==0){
   const int count=argc>2?_wtoi(argv[2]):3;Stream initial;CHECK(instance.c->getState(&initial)==kResultOk);
   for(int i=0;i<count;i++){
    SendMessageW(next,BM_CLICK,0,0);pump(15);wchar_t caption[4096]{};GetWindowTextW(bar,caption,4096);CHECK(wcslen(caption)>0&&std::wstring(caption)!=L"Presets ▾");
    Stream s;CHECK(instance.c->getState(&s)==kResultOk);CHECK(s.b!=initial.b);double energy=instance.render(44100,512,2,config.instrument);CHECK(std::isfinite(energy));
    printf("PRESET %d %ls energy=%.8g state=%zu\n",i,caption,energy,s.b.size());fflush(stdout);
   }
   wchar_t caption[4096]{};GetWindowTextW(bar,caption,4096);savedCaption=caption;CHECK(instance.c->getState(&selectedState)==kResultOk);
   SendMessageW(previous,BM_CLICK,0,0);pump(20);selectedState.pos=0;CHECK(instance.c->setState(&selectedState)==kResultOk);pump(60);GetWindowTextW(bar,caption,4096);CHECK(savedCaption==caption);
   // Legacy v2 projects remain loadable (native chunk ends at 28+header[2]).
   Stream legacy=selectedState;uint32_t nstate=0;memcpy(&nstate,legacy.b.data()+8,4);legacy.b.resize(28+nstate);legacy.b[4]=2;legacy.pos=0;CHECK(instance.c->setState(&legacy)==kResultOk);
   selectedState.pos=0;CHECK(instance.c->setState(&selectedState)==kResultOk);
  }else{wchar_t caption[4096]{};GetWindowTextW(bar,caption,4096);CHECK(savedCaption==caption);}
  CHECK(view->removed()==kResultOk);view->setFrame(nullptr);view->release();
 }
 Instance restored;restored.create(factory,info.cid,config,&selectedState,48000,512);CHECK(std::isfinite(restored.render(48000,512,2,config.instrument)));restored.close();
 instance.close();factory->release();FreeLibrary(module);DestroyWindow(host);CoUninitialize();puts("PASS toolbar native dimensions, preset switching/audio, state v3 label, legacy v2, reopen, new instance restore");return 0;
}
