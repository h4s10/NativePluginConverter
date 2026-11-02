// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <objbase.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include "engine.h"
#include "config.h"
// Child helpers must not outlive a timed-out compatibility check.
static HANDLE probeJob=nullptr;
static bool containChildren(){probeJob=CreateJobObjectW(nullptr,nullptr);if(!probeJob)return false;JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;if(!SetInformationJobObject(probeJob,JobObjectExtendedLimitInformation,&limit,sizeof limit)||!AssignProcessToJobObject(probeJob,GetCurrentProcess())){CloseHandle(probeJob);probeJob=nullptr;}return probeJob!=nullptr;}
static const char* stage="startup";
static DWORD WINAPI timeout(void*){Sleep(25000);fprintf(stderr,"TIMEOUT %s\n",stage);fflush(stderr);TerminateProcess(GetCurrentProcess(),124);return 0;}
static LONG WINAPI fault(EXCEPTION_POINTERS*p){DWORD c=p->ExceptionRecord->ExceptionCode;if(c==0xc0000005||c==0xc000001d||c==0xc0000094||c==0xc00000fd){fprintf(stderr,"CRASH %s code=%08lx address=%p\n",stage,c,p->ExceptionRecord->ExceptionAddress);HMODULE mod=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)p->ExceptionRecord->ExceptionAddress,&mod);wchar_t path[32768]{};GetModuleFileNameW(mod,path,32768);fprintf(stderr,"MODULE %ls RVA=%llx\n",path,(unsigned long long)((char*)p->ExceptionRecord->ExceptionAddress-(char*)mod));fflush(stderr);TerminateProcess(GetCurrentProcess(),99);}return EXCEPTION_CONTINUE_SEARCH;}
static std::string utf8(const wchar_t*s){int n=WideCharToMultiByte(CP_UTF8,0,s,-1,nullptr,0,nullptr,nullptr);std::string r(n? n:1,0);if(n)WideCharToMultiByte(CP_UTF8,0,s,-1,r.data(),n,nullptr,nullptr);r.pop_back();return r;}
static void quote(const std::string&s){putchar('"');for(unsigned char c:s){if(c=='"'||c=='\\')putchar('\\');if(c<32)printf("\\u%04x",c);else putchar(c);}putchar('"');}
static void phase(const char*s){stage=s;fprintf(stderr,"PHASE %s\n",s);fflush(stderr);}
int wmain(int argc,wchar_t**argv){if(argc<2)return 2;if(!containChildren()){fprintf(stderr,"Cannot contain native helper processes\n");return 7;}SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,fault);CloseHandle(CreateThread(nullptr,0,timeout,nullptr,0,nullptr));CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Engine e;phase("open");if(!e.openConfigured(argv[1])){fprintf(stderr,"ERROR %s\n",e.error());printf("{\"completed\":false,\"error\":");quote(e.error());printf("}\n");return 3;}printf("{\"flags\":%d,\"instrument\":%s,\"parameters\":%d",e.nativeFlags(),e.isInstrument()?"true":"false",e.parameterCount());fflush(stdout);
 phase("parameters");for(int i=0;i<e.parameterCount();i++){char name[256]{};e.parameterName(i,name,256);double value=e.getParameter(i);if(!std::isfinite(value))return 4;}
 phase("configure");e.configure(48000,512);e.setTransport(120,0,true);if(e.supportsSampleLoad()&&argc>2&&!e.loadSample(argv[2])){fprintf(stderr,"SAMPLE %s\n",e.error());return 5;}
 phase("render");std::vector<float> l(512),r(512),il(512),ir(512);if(e.isInstrument())e.noteOn(1,60,0,.8f);double energy=0,peak=0;for(int block=0;block<24;block++){for(int i=0;i<512;i++){il[i]=.1f*std::sin((block*512+i)*6.28318530718*440/48000);ir[i]=.07f*std::sin((block*512+i)*6.28318530718*660/48000);}e.render(l.data(),r.data(),512,il.data(),ir.data());for(int i=0;i<512;i++){energy+=l[i]*l[i]+r[i]*r[i];peak=std::max(peak,double(std::max(std::abs(l[i]),std::abs(r[i]))));}}e.allNotesOff();printf(",\"energy\":%.9g,\"peak\":%.9g",energy,peak);fflush(stdout);
 phase("state-save");std::vector<uint8_t> state;bool saved=e.save(state);printf(",\"state_bytes\":%llu",(unsigned long long)state.size());fflush(stdout);phase("state-restore");bool restored=saved&&e.restore(state.data(),state.size());printf(",\"state_restored\":%s",restored?"true":"false");fflush(stdout);
 phase("editor");HWND parent=CreateWindowExW(0,L"STATIC",L"FL Bridge compatibility probe",WS_OVERLAPPEDWINDOW,0,0,1400,1000,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);bool gui=e.showEditor(parent);int width=0,height=0;e.editorSize(width,height);for(int i=0;i<5;i++){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}e.idle();Sleep(10);}printf(",\"editor\":%s,\"width\":%d,\"height\":%d",gui?"true":"false",width,height);e.hideEditor();DestroyWindow(parent);
 phase("modules");printf(",\"modules\":[");HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());MODULEENTRY32W me{};me.dwSize=sizeof me;bool first=true;if(Module32FirstW(snap,&me))do{if(!first)putchar(',');quote(utf8(me.szExePath));first=false;}while(Module32NextW(snap,&me));CloseHandle(snap);printf("]");fflush(stdout);
 const bool usable=e.ready();if(!usable){printf(",\"error\":");quote(e.error());}
 phase("close");e.close();printf(",\"completed\":true}\n");fflush(stdout);CoUninitialize();return restored&&usable?0:6;}
