// SPDX-License-Identifier: MIT
// Diagnostic only: exercise the original FL Studio VSTi through its public ABI.
// No activation response, license token, native flag patch or vendor file edit.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <objbase.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
struct AEffect;
using Word=intptr_t;
using Dispatcher=Word(__cdecl*)(AEffect*,int,int,Word,void*,float);
using Process=void(__cdecl*)(AEffect*,float**,float**,int);
struct AEffect{int magic;Dispatcher dispatcher;Process process;void(__cdecl*setParam)(AEffect*,int,float);float(__cdecl*getParam)(AEffect*,int);int numPrograms,numParams,numInputs,numOutputs,flags;Word reserved1,reserved2;int initialDelay,realQualities,offQualities;float ioRatio;void*object,*user;int uniqueID,version;Process replacing;void*doubleReplacing;char future[56];};
static_assert(offsetof(AEffect,numInputs)==0x30);
struct Time{double samplePos=0,sampleRate=44100,nanoSeconds=0,ppqPos=0,tempo=120,barStartPos=0,cycleStartPos=0,cycleEndPos=0;int num=4,den=4,smpteOffset=0,smpteRate=0,samplesToNextClock=0,flags=512|1024|2048;};
static Time clockInfo;
static std::string directory;
static Word __cdecl host(AEffect*,int op,int,Word,void*ptr,float){switch(op){case 1:return 2400;case 7:return(Word)&clockInfo;case 16:return 44100;case 17:return 512;case 23:return 1;case 32:case 33:if(ptr)strcpy((char*)ptr,"NativePluginConverter Host Check");return 1;case 37:return 1;case 41:return(Word)directory.c_str();default:return 0;}}
static std::string utf8(const std::wstring&s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;}
static std::wstring quoted(const std::wstring&s){std::wstring r=L"\"";size_t slash=0;for(wchar_t c:s){if(c==L'\\'){++slash;continue;}r.append(slash*(c==L'"'?2:1),L'\\');slash=0;if(c==L'"')r+=L'\\';r+=c;}r.append(slash*2,L'\\');r+=L'"';return r;}
static void pump(){DWORD begin=GetTickCount();while(GetTickCount()-begin<500){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}}
static int worker(const wchar_t*root){
 setvbuf(stdout,nullptr,_IONBF,0);setvbuf(stderr,nullptr,_IONBF,0);
 if(!wcscmp(root,L"--self-test")){puts("PASS child process and inherited report output");return 17;}
 if(!wcscmp(root,L"--timeout-test")){puts("TIMEOUT fixture started");Sleep(10000);return 99;}
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com)){printf("COM failed: %lx\n",com);return 2;}
 std::wstring base=root;directory=utf8(base);printf("Selected installation: %s\n",directory.c_str());
 std::vector<HMODULE>deps;
 for(auto name:{L"Blend2D.dll",L"dsp_ippv2_x64.dll",L"freetype_x64.dll",L"QuickFontCache_x64.dll",L"ILTools_x64.dll"}){
  printf("PHASE preload %s\n",utf8(name).c_str());auto h=LoadLibraryExW((base+L"\\Shared\\"+name).c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!h){printf("Windows error: %lu\n",GetLastError());return 3;}deps.push_back(h);
 }
 // The original VSTi resolves its engine by basename or installed path. Preload
 // that exact engine from the explicitly selected installation, without registry edits.
 puts("PHASE load FL engine");auto engine=LoadLibraryExW((base+L"\\FLEngine_x64.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!engine){printf("Windows error: %lu\n",GetLastError());return 4;}
 puts("PHASE load FL Studio VSTi");auto module=LoadLibraryExW((base+L"\\System\\Plugin\\VSTi\\x64\\FL Studio VSTi.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!module){printf("Windows error: %lu\n",GetLastError());return 5;}
 auto create=(AEffect*(__cdecl*)(Dispatcher))GetProcAddress(module,"VSTPluginMain");if(!create)create=(AEffect*(__cdecl*)(Dispatcher))GetProcAddress(module,"main");if(!create){puts("No public VST entry point");return 6;}
 puts("PHASE create host instance");auto effect=create(host);if(!effect||effect->magic!=0x56737450||!effect->dispatcher){puts("Invalid VST instance");return 7;}
 printf("VST inputs=%d outputs=%d parameters=%d flags=%x\n",effect->numInputs,effect->numOutputs,effect->numParams,effect->flags);
 puts("PHASE open");effect->dispatcher(effect,0,0,0,nullptr,0);effect->dispatcher(effect,10,0,0,nullptr,44100);effect->dispatcher(effect,11,0,512,nullptr,0);pump();
 puts("PHASE get full FL Studio state");void*data=nullptr;auto size=effect->dispatcher(effect,23,0,0,&data,0);printf("STATE byte_count=%lld\n",(long long)size);bool stateOk=false;
 if(size>0&&size<=256*1024*1024&&data){std::vector<unsigned char>copy((unsigned char*)data,(unsigned char*)data+size);puts("PHASE restore full FL Studio state");auto result=effect->dispatcher(effect,24,0,size,copy.data(),0);printf("STATE restore_return=%lld\n",(long long)result);pump();stateOk=true;}
 puts("PHASE close");effect->dispatcher(effect,1,0,0,nullptr,0);FreeLibrary(module);FreeLibrary(engine);for(auto it=deps.rbegin();it!=deps.rend();++it)FreeLibrary(*it);CoUninitialize();
 printf("COMPLETED state_roundtrip_called=%d\n",stateOk);puts("This checks the original host API, not Edison restoration or license entitlement. State contents are not written to this report.");return stateOk?0:8;
}
struct Result{DWORD code=MAXDWORD;bool timeout=false;bool launched=false;};
static Result run(const std::wstring&exe,const std::wstring&root,HANDLE log,DWORD timeout){
 Result r;auto args=quoted(exe)+L" --worker "+quoted(root);STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=si.hStdError=log;
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);si.hStdInput=input;
 HANDLE job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
 if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&info,sizeof(info))){if(job)CloseHandle(job);CloseHandle(input);return r;}
 PROCESS_INFORMATION pi{};
 if(CreateProcessW(exe.c_str(),args.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&si,&pi)){
  if(AssignProcessToJobObject(job,pi.hProcess)){r.launched=true;ResumeThread(pi.hThread);if(WaitForSingleObject(pi.hProcess,timeout)==WAIT_TIMEOUT){r.timeout=true;TerminateJobObject(job,124);WaitForSingleObject(pi.hProcess,5000);}GetExitCodeProcess(pi.hProcess,&r.code);}else TerminateProcess(pi.hProcess,125);
  CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
 }
 CloseHandle(input);CloseHandle(job);return r;
}
static void write(HANDLE f,const std::string&s){DWORD written=0;WriteFile(f,s.data(),DWORD(s.size()),&written,nullptr);FlushFileBuffers(f);}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
 if(argc==3&&!wcscmp(argv[1],L"--worker")){int result=worker(argv[2]);LocalFree(argv);return result;}
 bool selftest=argc==2&&!wcscmp(argv[1],L"--self-test");LocalFree(argv);
 wchar_t exe[32768]{};if(!GetModuleFileNameW(nullptr,exe,32768))return 1;std::wstring app=exe;auto folder=app.substr(0,app.find_last_of(L"\\/"));
 wchar_t chosen[32768]{};
 if(!selftest){OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.lpstrFile=chosen;ofn.nMaxFile=32768;ofn.lpstrFilter=L"FL Studio (FL64.exe)\0FL64.exe\0\0";ofn.lpstrTitle=L"Выберите FL64.exe из активированной установки FL Studio";ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(!GetOpenFileNameW(&ofn))return 0;}
 std::wstring root=chosen;if(!root.empty())root.resize(root.find_last_of(L"\\/"));
 auto report=folder+L"\\FLHostCheck-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".txt";
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE log=CreateFileW(report.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(log==INVALID_HANDLE_VALUE){if(!selftest)MessageBoxW(nullptr,L"Не удалось записать отчёт. Переместите программу в папку Загрузки и повторите.",L"FL Host Check",MB_ICONERROR);return 2;}
 write(log,"NativePluginConverter: original FL Studio host diagnostic\r\nNo license keys, registry values or project contents are recorded.\r\n");
 auto result=run(app,selftest?L"--self-test":root,log,60000);
 write(log,"\r\nRESULT launched="+std::to_string(result.launched)+" timeout="+std::to_string(result.timeout)+" exit="+std::to_string(result.code)+"\r\n");
 bool passed=result.launched&&!result.timeout&&result.code==17;
 if(selftest&&passed){auto timeout=run(app,L"--timeout-test",log,500);passed=timeout.launched&&timeout.timeout&&timeout.code==124;write(log,passed?"PASS timeout kills only the diagnostic child\r\n":"FAIL timeout fixture\r\n");}
 CloseHandle(log);
 if(selftest)return passed?0:1;
 auto message=std::wstring(result.timeout?L"Проверка остановлена по времени. Этап отказа сохранён в отчёте.":L"Проверка завершена. Отчёт сохранён:")+L"\n\n"+report+L"\n\nПришлите этот текстовый файл. Это диагностика, а не исправленная версия Edison.";
 MessageBoxW(nullptr,message.c_str(),L"FL Host Check",MB_OK|MB_ICONINFORMATION);return result.code==0?0:1;
}
