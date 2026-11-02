// SPDX-License-Identifier: MIT
#include "../bridge/engine.h"
#include "../bridge/config.h"
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <cstring>
static std::atomic<int> dialogs{0},exceptionsSeen{0};
static LONG WINAPI exceptions(EXCEPTION_POINTERS*x){if(x->ExceptionRecord->ExceptionCode==0xeedfade&&x->ExceptionRecord->NumberParameters>1){exceptionsSeen++;uintptr_t obj[12]{};SIZE_T read=0;if(ReadProcessMemory(GetCurrentProcess(),(void*)x->ExceptionRecord->ExceptionInformation[1],obj,sizeof obj,&read))for(unsigned j=0;j<12;j++){if(obj[j]<65536)continue;int len=0;if(!ReadProcessMemory(GetCurrentProcess(),(void*)(obj[j]-4),&len,4,&read)||len<=0||len>2048)continue;std::vector<wchar_t>text(len+1);if(ReadProcessMemory(GetCurrentProcess(),(void*)obj[j],text.data(),len*2,&read)){int n=WideCharToMultiByte(CP_UTF8,0,text.data(),len,nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,text.data(),len,s.data(),n,nullptr,nullptr);fprintf(stderr,"DELPHI offset%u: %s\n",j*8,s.c_str());}}fflush(stderr);}return EXCEPTION_CONTINUE_SEARCH;}
static DWORD WINAPI watchdog(void*){for(int i=0;i<350;i++){Sleep(100);EnumWindows([](HWND w,LPARAM)->BOOL{DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid!=GetCurrentProcessId()||!IsWindowVisible(w))return TRUE;wchar_t cls[100]{},title[500]{};GetClassNameW(w,cls,100);GetWindowTextW(w,title,500);if(wcscmp(cls,L"#32770"))return TRUE;dialogs++;fprintf(stderr,"DIALOG seen\n");EnumChildWindows(w,[](HWND c,LPARAM)->BOOL{char t[4096]{};GetWindowTextA(c,t,sizeof t);if(*t)fprintf(stderr,"  %s\n",t);return TRUE;},0);fflush(stderr);PostMessageW(w,WM_COMMAND,IDOK,0);return TRUE;},0);}TerminateProcess(GetCurrentProcess(),124);return 0;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 10;}}while(0)
int wmain(int argc,wchar_t**argv){if(argc<2)return 2;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,exceptions);CloseHandle(CreateThread(nullptr,0,watchdog,nullptr,0,nullptr));CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Engine e;CHECK(e.openConfigured(argv[1]));e.configure(44100,512);int index=-1;for(int i=0;i<e.presetCount();i++){char name[2048]{};e.presetName(i,name,sizeof name);if(std::string(name)==(argc>2?BridgeConfig::trim(std::string(argv[2],argv[2]+wcslen(argv[2]))):"Pad/Chorus ensemble"))index=i;}CHECK(index>=0);fprintf(stderr,"Preset index %d\n",index);std::vector<unsigned char>before,after;CHECK(e.save(before));int exBefore=exceptionsSeen.load();CHECK(e.loadPreset(index));CHECK(e.save(after));std::vector<float>l(512),r(512);double energy=0;e.noteOn(1,60,0,.8f);for(int i=0;i<100;i++){e.render(l.data(),r.data(),512);for(int j=0;j<512;j++){CHECK(std::isfinite(l[j])&&std::isfinite(r[j]));energy+=fabs(l[j])+fabs(r[j]);}}CHECK(energy>0.001);e.noteOff(1,60,0,0);CHECK(before!=after);CHECK(e.restore(after.data(),after.size()));e.close();printf("RESULT dialogs=%d preset_exceptions=%d energy=%.9g state=%zu\n",dialogs.load(),exceptionsSeen.load()-exBefore,energy,after.size());CoUninitialize();return dialogs||exceptionsSeen.load()!=exBefore?3:0;}
