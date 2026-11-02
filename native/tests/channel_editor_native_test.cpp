// SPDX-License-Identifier: MIT
#define wmain baseline_native_main
#include "native_vst3_test.cpp"
#undef wmain
static DWORD WINAPI watchdog(void*){Sleep(45000);TerminateProcess(GetCurrentProcess(),124);return 0;}
int wmain(int argc,wchar_t**argv){
 if(argc<2)return 2;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,fail);CloseHandle(CreateThread(nullptr,0,watchdog,nullptr,0,nullptr));CHECK(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)));
 std::wstring ini=argv[1];ini.resize(ini.find_last_of(L"/\\")+1);ini+=L"bridge.ini";auto config=BridgeConfig::load(ini.c_str());CHECK(config.valid&&config.instrument);
 HMODULE dll=LoadLibraryW(argv[1]);CHECK(dll);auto*factory=((IPluginFactory*(*)())GetProcAddress(dll,"GetPluginFactory"))();CHECK(factory);PClassInfo ci{};CHECK(factory->getClassInfo(0,&ci)==kResultOk);
 Instance one;one.create(factory,ci.cid,config,nullptr,44100,512);HWND host=CreateWindowW(L"STATIC",L"Native Channel test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1400,1000,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(host);Frame frame;frame.hwnd=host;
 auto*view=one.e->createView(ViewType::kEditor);CHECK(view);CHECK(view->setFrame(&frame)==kResultOk);CHECK(view->attached(host,kPlatformTypeHWND)==kResultOk);pump(100);
 HWND root=GetWindow(host,GW_CHILD),button=GetDlgItem(root,0x684);CHECK(button);SendMessageW(button,BM_CLICK,0,0);pump(30);HWND settings=FindWindowExW(root,nullptr,L"FLBridgeChannelPanel",nullptr);CHECK(settings&&IsWindowVisible(settings));
 SetDlgItemInt(settings,0x710,5,TRUE);SetDlgItemInt(settings,0x715,12,TRUE);SetDlgItemInt(settings,0x717,70,TRUE);SendDlgItemMessageW(settings,0x71a,BM_CLICK,0,0);pump(30);
 double energy=one.render(44100,512,60,true);CHECK(std::isfinite(energy)&&energy>0.001);Stream state;CHECK(one.c->getState(&state)==kResultOk);CHECK(state.b[4]==4&&state.b.size()>40);uint32_t wire[10]{};memcpy(wire,state.b.data()+state.b.size()-40,40);CHECK(wire[0]==0x314e4843&&wire[1]==5&&wire[6]==12);float gain;memcpy(&gain,&wire[8],4);CHECK(gain==0.7f);
 SendDlgItemMessageW(settings,0x719,BM_CLICK,0,0);state.pos=0;CHECK(one.c->setState(&state)==kResultOk);pump(60);CHECK(GetDlgItemInt(settings,0x710,nullptr,TRUE)==5&&GetDlgItemInt(settings,0x715,nullptr,TRUE)==12);
 SendMessageW(button,BM_CLICK,0,0);CHECK(!IsWindowVisible(settings));CHECK(view->removed()==kResultOk);view->setFrame(nullptr);view->release();
 Instance restored;restored.create(factory,ci.cid,config,&state,48000,512);Stream again;CHECK(restored.c->getState(&again)==kResultOk);CHECK(again.b.size()>=40&&!memcmp(again.b.data()+again.b.size()-40,wire,40));CHECK(restored.render(48000,512,30,true)>0.001);restored.close();one.close();factory->release();FreeLibrary(dll);DestroyWindow(host);CoUninitialize();printf("PASS native Channel Apply/audio/state/reopen/48k new instance: energy=%g\n",energy);return 0;
}
