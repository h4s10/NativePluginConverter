// SPDX-License-Identifier: MIT
// Real configured VST3 module integration. Run in a separate process per bundle.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>
#include "../bridge/engine.h"
#include "../bridge/config.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
using namespace Steinberg;
using namespace Steinberg::Vst;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);fflush(stderr);ExitProcess(10);} }while(0)
#define UNKNOWN_IMPL tresult PLUGIN_API queryInterface(const TUID,void**p)override{if(p)*p=nullptr;return kNoInterface;} uint32 PLUGIN_API addRef()override{return 1;} uint32 PLUGIN_API release()override{return 1;}
struct Stream:IBStream {
 UNKNOWN_IMPL
 std::vector<uint8_t>b;size_t pos=0;
 tresult PLUGIN_API read(void*d,int32 n,int32*done)override{if(n<0||pos>b.size())return kInvalidArgument;n=std::min(n,int32(b.size()-pos));if(n)memcpy(d,b.data()+pos,n);pos+=n;if(done)*done=n;return kResultOk;}
 tresult PLUGIN_API write(void*s,int32 n,int32*done)override{if(n<0)return kInvalidArgument;b.resize(pos+n);if(n)memcpy(b.data()+pos,s,n);pos+=n;if(done)*done=n;return kResultOk;}
 tresult PLUGIN_API seek(int64 p,int32 mode,int64*done)override{int64 next=p+(mode==1?int64(pos):mode==2?int64(b.size()):0);if(next<0||next>int64(b.size()))return kInvalidArgument;pos=size_t(next);if(done)*done=next;return kResultOk;}
 tresult PLUGIN_API tell(int64*p)override{if(!p)return kInvalidArgument;*p=pos;return kResultOk;}
};
struct Events:IEventList {
 UNKNOWN_IMPL
 std::vector<Event> events;
 int32 PLUGIN_API getEventCount()override{return int32(events.size());}
 tresult PLUGIN_API getEvent(int32 i,Event&e)override{if(i<0||i>=int32(events.size()))return kInvalidArgument;e=events[i];return kResultOk;}
 tresult PLUGIN_API addEvent(Event&e)override{events.push_back(e);return kResultOk;}
};
struct Frame:IPlugFrame {
 UNKNOWN_IMPL
 HWND hwnd=nullptr;
 tresult PLUGIN_API resizeView(IPlugView*v,ViewRect*r)override{RECT rc{0,0,r->getWidth(),r->getHeight()};AdjustWindowRect(&rc,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(hwnd,nullptr,0,0,rc.right-rc.left,rc.bottom-rc.top,SWP_NOMOVE|SWP_NOZORDER);return v->onSize(r);}
};
LONG CALLBACK fail(EXCEPTION_POINTERS*x){
 DWORD code=x->ExceptionRecord->ExceptionCode;
 if(code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_ILLEGAL_INSTRUCTION||code==EXCEPTION_STACK_OVERFLOW){
  HMODULE m=nullptr;auto address=x->ExceptionRecord->ExceptionAddress;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(wchar_t*)address,&m);
  fprintf(stderr,"NATIVE EXCEPTION code=%lx address=%p module=%p rva=%llx\n",code,address,m,(unsigned long long)((uintptr_t)address-(uintptr_t)m));fflush(stderr);TerminateProcess(GetCurrentProcess(),99);
 }
 return EXCEPTION_CONTINUE_SEARCH;
}
static int noteBase=60;
struct Instance {
 IComponent*c=nullptr;IAudioProcessor*a=nullptr;IEditController*e=nullptr;bool instrument=false;
 void create(IPluginFactory*f,const TUID cid,const BridgeConfig&config,Stream*state,double rate,int block){
  instrument=config.instrument;
  CHECK(f->createInstance(cid,IComponent_iid,(void**)&c)==kResultOk);CHECK(c->initialize(nullptr)==kResultOk);
  CHECK(c->queryInterface(IAudioProcessor_iid,(void**)&a)==kResultOk);CHECK(c->queryInterface(IEditController_iid,(void**)&e)==kResultOk);
  CHECK(e->getParameterCount()>=0&&e->getParameterCount()<=65536);
  CHECK(c->getBusCount(kAudio,kOutput)==1);CHECK(c->getBusCount(kAudio,kInput)==(instrument?0:1));
  CHECK(c->getBusCount(kEvent,kInput)==1);
  BusInfo info{};CHECK(c->getBusInfo(kAudio,kOutput,0,info)==kResultOk&&info.channelCount==2);
  if(!instrument)CHECK(c->getBusInfo(kAudio,kInput,0,info)==kResultOk&&info.channelCount==2);
  SpeakerArrangement stereo=SpeakerArr::kStereo;
  CHECK(a->setBusArrangements(instrument?nullptr:&stereo,instrument?0:1,&stereo,1)==kResultTrue);
  if(state){state->pos=0;CHECK(c->setState(state)==kResultOk);}
  ProcessSetup setup{};setup.sampleRate=rate;setup.maxSamplesPerBlock=block;setup.symbolicSampleSize=kSample32;setup.processMode=kRealtime;
  CHECK(a->setupProcessing(setup)==kResultOk);CHECK(c->activateBus(kAudio,kOutput,0,true)==kResultOk);
  if(!instrument)CHECK(c->activateBus(kAudio,kInput,0,true)==kResultOk);
  CHECK(c->activateBus(kEvent,kInput,0,true)==kResultOk);CHECK(c->setActive(true)==kResultOk);CHECK(a->setProcessing(true)==kResultOk);
 }
 double render(double rate,int block,int count,bool notes,bool alias=false){
  std::vector<float> l(block),r(block),il(block),ir(block);float*channels[]{l.data(),r.data()};float*inputs[]{alias?l.data():il.data(),alias?r.data():ir.data()};
  AudioBusBuffers out{},in{};out.numChannels=in.numChannels=2;out.channelBuffers32=channels;in.channelBuffers32=inputs;
  ProcessContext context{};context.sampleRate=rate;context.tempo=120;context.state=ProcessContext::kTempoValid|ProcessContext::kProjectTimeMusicValid|ProcessContext::kPlaying;
  Events events;if(notes){for(int j=0;j<3;j++){Event on{};on.type=Event::kNoteOnEvent;on.sampleOffset=std::min(7+j*11,block-1);on.noteOn.pitch=noteBase+j*4;on.noteOn.noteId=j;on.noteOn.velocity=.7f;events.events.push_back(on);}}
  ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numOutputs=1;data.outputs=&out;data.processContext=&context;
  if(!instrument){data.numInputs=1;data.inputs=&in;}
  double energy=0;for(int n=0;n<count;n++){
   data.numSamples=n%2?block:std::max(1,block-3);data.inputEvents=n==0?&events:nullptr;
   for(int i=0;i<data.numSamples;i++){auto t=(context.projectTimeSamples+i)/rate;inputs[0][i]=float(.15*sin(t*440*6.283185307179586));inputs[1][i]=float(.1*sin(t*660*6.283185307179586));}
   in.silenceFlags=0;CHECK(a->process(data)==kResultOk);
   for(int i=0;i<data.numSamples;i++){CHECK(std::isfinite(l[i])&&std::isfinite(r[i]));CHECK(fabs(l[i])<32&&fabs(r[i])<32);energy+=l[i]*l[i]+r[i]*r[i];}
   context.projectTimeSamples+=data.numSamples;context.projectTimeMusic+=data.numSamples*120/(60*rate);
  }
  return energy;
 }
 void close(){CHECK(a->setProcessing(false)==kResultOk);CHECK(c->setActive(false)==kResultOk);a->release();e->release();CHECK(c->terminate()==kResultOk);c->release();c=nullptr;}
};
void pump(unsigned duration){DWORD start=GetTickCount();while(GetTickCount()-start<duration){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(10);}}
int wmain(int argc,wchar_t**argv){
 if(argc<2){fprintf(stderr,"Usage: NativeVST3Test.exe PATH-TO-INNER-VST3 [--sample WAV] [--gui] [--allow-silent] [--note-base MIDI] [--peer OTHER-INNER-VST3]\n");return 2;}
 bool gui=false,allowSilent=false;const wchar_t*sample=nullptr;const wchar_t*peerPath=nullptr;
 for(int i=2;i<argc;i++){if(!wcscmp(argv[i],L"--gui"))gui=true;else if(!wcscmp(argv[i],L"--allow-silent"))allowSilent=true;else if(!wcscmp(argv[i],L"--note-base")&&i+1<argc){noteBase=_wtoi(argv[++i]);if(noteBase<0||noteBase>119)return 2;}else if(!wcscmp(argv[i],L"--peer")&&i+1<argc)peerPath=argv[++i];else if(!wcscmp(argv[i],L"--sample")&&i+1<argc)sample=argv[++i];else return 2;}
 HANDLE testJob=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};jobInfo.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;CHECK(testJob&&SetInformationJobObject(testJob,JobObjectExtendedLimitInformation,&jobInfo,sizeof jobInfo)&&AssignProcessToJobObject(testJob,GetCurrentProcess()));
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,fail);CHECK(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)));
 std::wstring ini=argv[1];ini.resize(ini.find_last_of(L"/\\")+1);ini+=L"bridge.ini";
 auto config=BridgeConfig::load(ini.c_str());CHECK(config.valid);
 Stream initial;Stream* initialPtr=nullptr;
 if(sample){
  Engine engine;CHECK(engine.openConfigured(ini.c_str()));CHECK(engine.canLoadFile());CHECK(engine.loadFile(sample));std::vector<uint8_t>payload;CHECK(engine.save(payload));
  uint32_t h[3]{0x31424746,2,uint32_t(payload.size())};initial.b.resize(28+payload.size());memcpy(initial.b.data(),h,12);memcpy(initial.b.data()+12,config.uid.data(),16);memcpy(initial.b.data()+28,payload.data(),payload.size());engine.close();initialPtr=&initial;
 }
 HMODULE module=LoadLibraryW(argv[1]);CHECK(module);
 auto init=(bool(*)())GetProcAddress(module,"InitDll");auto exit=(bool(*)())GetProcAddress(module,"ExitDll");auto get=(IPluginFactory*(PLUGIN_API*)())GetProcAddress(module,"GetPluginFactory");
 CHECK(init&&exit&&get&&init());auto*f=get();CHECK(f&&f->countClasses()==1);PClassInfo ci{};CHECK(f->getClassInfo(0,&ci)==kResultOk);CHECK(!memcmp(ci.cid,config.uid.data(),16));
 IPluginFactory2*f2=nullptr;CHECK(f->queryInterface(IPluginFactory2_iid,(void**)&f2)==kResultOk);PClassInfo2 ci2{};CHECK(f2->getClassInfo2(0,&ci2)==kResultOk);
 CHECK(std::string(ci2.subCategories)==(config.instrument?"Instrument|Synth":"Fx"));f2->release();
 fprintf(stderr,"FACTORY name=%s kind=%s\n",ci.name,config.instrument?"instrument":"effect");fflush(stderr);
 Instance one,two;one.create(f,ci.cid,config,initialPtr,44100,512);two.create(f,ci.cid,config,initialPtr,48000,127);
 double e1=one.render(44100,512,50,true);double e2=two.render(48000,127,100,true,true);CHECK(allowSilent||(e1>0.001&&e2>0.001));
 fprintf(stderr,"PASS two native VST3 instances: energy44k=%g energy48k=%g params=%d latency=%u\n",e1,e2,one.e->getParameterCount(),one.a->getLatencySamples());fflush(stderr);
 Stream saved;CHECK(one.c->getState(&saved)==kResultOk);CHECK(saved.b.size()>=28);
 double before=one.e->getParameterCount()?one.e->getParamNormalized(0):0;if(one.e->getParameterCount())one.e->setParamNormalized(0,.9);
 saved.pos=0;CHECK(one.c->setState(&saved)==kResultOk);if(one.e->getParameterCount())CHECK(fabs(one.e->getParamNormalized(0)-before)<1e-6);
 auto restored=one.render(44100,512,10,true);CHECK(allowSilent||restored>0);
 saved.pos=0;saved.b[12]^=1;CHECK(one.c->setState(&saved)==kResultFalse);saved.b[12]^=1;
 fprintf(stderr,"PASS state restore and wrong-UID rejection (%zu bytes)\n",saved.b.size());fflush(stderr);
 if(peerPath){
  std::wstring peerIni=peerPath;peerIni.resize(peerIni.find_last_of(L"/\\")+1);peerIni+=L"bridge.ini";
  auto peerConfig=BridgeConfig::load(peerIni.c_str());CHECK(peerConfig.valid);CHECK(memcmp(config.uid.data(),peerConfig.uid.data(),16));
  HMODULE peerModule=LoadLibraryW(peerPath);CHECK(peerModule);
  auto peerInit=(bool(*)())GetProcAddress(peerModule,"InitDll");auto peerExit=(bool(*)())GetProcAddress(peerModule,"ExitDll");auto peerGet=(IPluginFactory*(PLUGIN_API*)())GetProcAddress(peerModule,"GetPluginFactory");CHECK(peerInit&&peerExit&&peerGet&&peerInit());
  auto*pf=peerGet();CHECK(pf&&pf->countClasses()==1);PClassInfo pci{};CHECK(pf->getClassInfo(0,&pci)==kResultOk);CHECK(!memcmp(pci.cid,peerConfig.uid.data(),16));
  Instance peer;peer.create(pf,pci.cid,peerConfig,nullptr,48000,256);double peerEnergy=peer.render(48000,256,50,true);CHECK(allowSilent||peerEnergy>0);
  CHECK(allowSilent||one.render(44100,512,10,true)>0);CHECK(allowSilent||peer.render(48000,256,10,false)>0);
  peer.close();pf->release();CHECK(peerExit());CHECK(FreeLibrary(peerModule));CHECK(allowSilent||one.render(44100,512,10,true)>0);
  fprintf(stderr,"PASS different plugin peer %s: energy=%g, independent UID and surviving teardown\n",pci.name,peerEnergy);fflush(stderr);
 }
 two.close();auto surviving=one.render(44100,512,10,true);CHECK(allowSilent||surviving>0);fprintf(stderr,"PASS surviving instance after peer teardown\n");fflush(stderr);
 if(gui){
  WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"FLNativeVST3IntegrationHost";CHECK(RegisterClassW(&wc));
  HWND hwnd=CreateWindowW(wc.lpszClassName,L"FL native VST3 integration test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,900,650,nullptr,nullptr,wc.hInstance,nullptr);CHECK(hwnd);Frame frame;frame.hwnd=hwnd;
  for(int pass=0;pass<2;pass++){
   auto*v=one.e->createView(ViewType::kEditor);CHECK(v);CHECK(v->setFrame(&frame)==kResultOk);CHECK(v->attached(hwnd,kPlatformTypeHWND)==kResultOk);
   ViewRect rect{};CHECK(v->getSize(&rect)==kResultOk);CHECK(rect.getWidth()>0&&rect.getHeight()>0);CHECK(frame.resizeView(v,&rect)==kResultOk);
   fprintf(stderr,"EDITOR %d: %dx%d\n",pass,rect.getWidth(),rect.getHeight());fflush(stderr);pump(400);CHECK(IsWindow(hwnd));CHECK(v->removed()==kResultOk);CHECK(v->setFrame(nullptr)==kResultOk);v->release();
  }
  DestroyWindow(hwnd);fprintf(stderr,"PASS native editor attach/message pump/detach/reopen\n");fflush(stderr);
 }
 one.close();f->release();CHECK(exit());CHECK(FreeLibrary(module));CoUninitialize();fprintf(stderr,"PASS full configured VST3 integration%s\n",allowSilent?" (silence permitted)":"");return 0;
}
