// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <commdlg.h>
#include <shlobj.h>
#include "engine.h"
#include "config.h"
#include "shared_loader.h"
#include "installed_fonts.h"
#include "preset.h"
#include "preset_paths.h"
#include "harmor_preset_paths.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <mutex>
#include <memory>
#include <bitset>
#include <type_traits>

namespace {
using Word = intptr_t;
// Delphi Application is shared by instances of one native DLL. Keep its owner
// HWND alive until the last instance closes (closing a peer must not invalidate it).
struct DialogOwner {HWND window=nullptr;size_t users=0;};
static std::mutex dialogOwnerMutex;
static std::map<std::wstring,DialogOwner> dialogOwners;
static HWND acquireDialogOwner(const std::wstring& key){std::lock_guard<std::mutex> guard(dialogOwnerMutex);auto& o=dialogOwners[key];
 if(!o.window)o.window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"FL Bridge dialog owner",WS_POPUP,0,0,0,0,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 if(o.window)++o.users;return o.window;}
static void releaseDialogOwner(const std::wstring& key){std::lock_guard<std::mutex> guard(dialogOwnerMutex);auto it=dialogOwners.find(key);if(it==dialogOwners.end())return;
 if(it->second.users&&!--it->second.users){if(IsWindow(it->second.window))DestroyWindow(it->second.window);dialogOwners.erase(it);}}

constexpr int RootMidiNote = 60; // C3 in Ableton: zero sample transposition.
constexpr int MidiMax = 1073741824; // Verified reciprocal at image VA 0x751238.
constexpr size_t MaxState = 256u*1024u*1024u;
#pragma pack(push, 1)
struct PlugInfo { int sdk; char* longName; char* shortName; int flags,params,poly,outControls,outVoices; int reserved[30]; };
struct SampleInfo { int size; void* data; int length,solidLength,loopStart,loopEnd; double rateConv; int regions; float beats,tempo; int channels,format; int reserved[13]; };
#pragma pack(pop)
static_assert(offsetof(SampleInfo,channels)==48);
struct SuppressEdits { int& n; explicit SuppressEdits(int&x):n(x){++n;} ~SuppressEdits(){--n;} };
struct Levels { float pan=0,vol=1,pitch=6000,cut=1,res=0; };
// Native initial.pitch is the absolute key; final.pitch is relative to the sample root.
struct VoiceParams { Levels initial,final; VoiceParams(){final.pitch=0;} };
static_assert(sizeof(VoiceParams)==40 && offsetof(VoiceParams,final)==20);
struct Voice { VoiceParams params; Word handle=-1; int id=-1,pitch=60,channel=0; float releaseVelocity=0; bool used=false,released=false,preview=false; uint64_t serial=0; double currentPitch=0,targetPitch=0,glideSamples=0; };
struct Sample { std::vector<int16_t> pcm; unsigned rate=44100; std::string name="sample.wav"; };
static int floatBits(float f) { int v; memcpy(&v,&f,4); return v; }
static std::string utf8(const wchar_t* s) { int n=WideCharToMultiByte(CP_UTF8,0,s,-1,nullptr,0,nullptr,nullptr); if(n<1)return {};std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s,-1,r.data(),n,nullptr,nullptr);r.pop_back();return r; }
static std::wstring wide(const char* s) {int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0);if(n<1)return {};std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,0,s,-1,r.data(),n);r.pop_back();return r;}
static unsigned u16(const uint8_t*p){return p[0]|unsigned(p[1])<<8;}
static unsigned u32(const uint8_t*p){return u16(p)|(u16(p+2)<<16);}
static bool checkX64Dll(const std::wstring& path,std::string& failure) {
 FILE* f=_wfopen(path.c_str(),L"rb");
 const auto pos=path.find_last_of(L"/\\");
 const std::string name=utf8(path.substr(pos==std::wstring::npos?0:pos+1).c_str());
 if(!f){failure="Missing or unreadable "+name+" next to the original DLL";return false;}
 uint8_t dos[64]{},pe[26]{};
 bool valid=fread(dos,1,sizeof dos,f)==sizeof dos&&dos[0]=='M'&&dos[1]=='Z';
 const unsigned offset=u32(dos+60);
 valid=valid&&offset>=64&&offset<=16*1024*1024&&fseek(f,long(offset),SEEK_SET)==0&&fread(pe,1,sizeof pe,f)==sizeof pe&&!memcmp(pe,"PE\0\0",4);
 fclose(f);
 if(!valid){failure="Invalid Windows DLL: "+name;return false;}
 if(u16(pe+4)!=IMAGE_FILE_MACHINE_AMD64||u16(pe+24)!=IMAGE_NT_OPTIONAL_HDR64_MAGIC){failure=name+" is not Windows x64 ("+(u16(pe+4)==IMAGE_FILE_MACHINE_I386?std::string("32-bit x86"):std::string("unsupported architecture"))+"). Replace it with the DLL from 64-bit FL Studio.";return false;}
 return true;
}
static bool readWave(const wchar_t* path,Sample& out) {
 FILE*f=_wfopen(path,L"rb");if(!f)return false;fseek(f,0,SEEK_END);long z=ftell(f);rewind(f);
 if(z<44||size_t(z)>MaxState){fclose(f);return false;}std::vector<uint8_t>b(z);bool ok=fread(b.data(),1,b.size(),f)==b.size();fclose(f);
 if(!ok||memcmp(b.data(),"RIFF",4)||memcmp(b.data()+8,"WAVE",4))return false;
 const uint8_t* fmt=nullptr;const uint8_t* data=nullptr;size_t fsz=0,dsz=0;
 for(size_t pos=12;pos+8<=b.size();){size_t n=u32(b.data()+pos+4);if(n>b.size()-pos-8)return false;
 if(!memcmp(b.data()+pos,"fmt ",4)){fmt=b.data()+pos+8;fsz=n;}if(!memcmp(b.data()+pos,"data",4)){data=b.data()+pos+8;dsz=n;}
 pos+=8+n+(n&1);}
 if(!fmt||fsz<16||!data)return false;unsigned tag=u16(fmt),ch=u16(fmt+2),rate=u32(fmt+4),align=u16(fmt+12),bits=u16(fmt+14);
 if(tag==65534&&fsz>=40)tag=u16(fmt+24);
 if(ch<1||ch>2||rate<8000||rate>384000||align!=ch*(bits/8))return false;
 if(!((tag==1&&(bits==8||bits==16||bits==24||bits==32))||(tag==3&&bits==32)))return false;
 size_t frames=dsz/align;if(frames==0||frames>MaxState/4)return false;out.pcm.resize(frames*2);out.rate=rate;out.name=utf8(path);
 for(size_t i=0;i<frames;i++)for(unsigned c=0;c<2;c++){
 const uint8_t*q=data+i*align+std::min(c,ch-1)*(bits/8);double x=0;
 if(tag==3){float v;memcpy(&v,q,4);x=std::isfinite(v)?v:0;}
 else if(bits==8)x=(int(*q)-128)/128.0;
 else if(bits==16)x=int16_t(u16(q))/32768.0;
 else if(bits==24){int v=int(q[0])|(int(q[1])<<8)|(int(q[2])<<16);if(v&0x800000)v|=int(0xff000000);x=v/8388608.0;}
 else x=int32_t(u32(q))/2147483648.0;
 out.pcm[i*2+c]=int16_t(std::clamp(x*32768.0,-32768.0,32767.0));}return true;
}
}

struct Engine::Impl {
 // This DLL requires host ABI version >= 24.2.99 (comparison at VA 0x73786f).
 struct Host { void** vt=nullptr; int version=24002099,flags=0; HWND app=nullptr; float* waves[10]{}; float* temp[4]{}; int reserved[30]{}; Impl* owner=nullptr; } host;
 Engine* facade=nullptr;
 std::recursive_mutex mutex;
 std::vector<HMODULE> dependencies; HMODULE module=nullptr;void* plug=nullptr;PlugInfo* info=nullptr;
 std::wstring lastAcceptedDrop; WIN32_FILE_ATTRIBUTE_DATA lastAcceptedDropInfo{};
 std::string failure,basePath,rootPath,sharedPath,exportPath;std::wstring dialogOwnerKey; HMODULE toolsModule=nullptr; void* realPathManager=nullptr;
 // Let the original plugin enforce its own mode. The bridge does not modify
 // native flags or infer activation from successful creation or state calls.
 bool closing=false;
 std::bitset<65536> automationDisabled;
 bool usable(){return plug&&info;}
 bool canAutomate(int index){return usable()&&index>=0&&index<65536&&index<info->params&&!automationDisabled.test(size_t(index));}
 SharedLoader sharedLoader; InstalledFonts fonts; BridgeConfig config; int latency=0; bool midiInput=false;
 std::array<void*,128> hostVt{};std::array<void*,32> pathVt{};void** pathPtr=nullptr;
 std::array<std::array<float,16384>,10> waves{};
 std::vector<float> scratch,input,temps,voiceScratch;
 std::array<Voice,256> voices{};
 ChannelSettings channel;
 struct HeldNote { int id=-1,pitch=60,channel=0;float velocity=1;uint64_t serial=0;bool preview=false; };
 std::array<HeldNote,256> held{};size_t heldCount=0;uint64_t nextSerial=0;
 double pitchFor(int note)const{return (note-channel.rootNote+channel.transpose)*100.0+channel.fineCents;}
 void writePitch(Voice&v){
  if(info->flags&(1<<21))v.params.final.pitch=float(v.currentPitch);
  else {int cents=int(std::lround(v.currentPitch));memcpy(&v.params.final.pitch,&cents,sizeof(cents));}
 }
 void stopVoice(Voice&v){Word h=v.handle;v.used=false;v.handle=-1;if(h!=-1)call<void>(11,h);}
 int voiceLimit()const{return channel.mono?1:channel.maxPolyphony;}
 int usedVoices()const{int n=0;for(const auto&v:voices)n+=v.used;return n;}
 Voice* victim(){Voice*best=nullptr;for(auto&v:voices)if(v.used&&(!best||(v.released&&!best->released)||(v.released==best->released&&v.serial<best->serial)))best=&v;return best;}
 void enforceLimit(int reserve=0){while(usedVoices()>voiceLimit()-reserve){auto*v=victim();if(!v)break;stopVoice(*v);}}
 Voice* activeMono(){for(auto&v:voices)if(v.used&&!v.released)return &v;return nullptr;}
 void retarget(Voice&v,const HeldNote&note,bool glide){
  v.id=note.id;v.pitch=note.pitch;v.channel=note.channel;v.serial=note.serial;v.preview=note.preview;v.params.final.vol=note.velocity;
  v.targetPitch=pitchFor(note.pitch);v.glideSamples=glide?rate*channel.slideMs/1000.0:0;
  if(v.glideSamples<=0)v.currentPitch=v.targetPitch;
  writePitch(v);
 }
 Voice* trigger(const HeldNote&note){
  enforceLimit(1);Voice*v=nullptr;for(auto&x:voices)if(!x.used){v=&x;break;}if(!v)return nullptr;
  *v=Voice{};v->used=true;v->id=note.id;v->pitch=note.pitch;v->channel=note.channel;v->serial=note.serial;v->preview=note.preview;
  v->params.initial.pitch=float(note.pitch*100);v->currentPitch=v->targetPitch=pitchFor(note.pitch);
  v->params.initial.vol=v->params.final.vol=std::clamp(note.velocity,0.f,1.f);
  if(!(info->flags&(1<<21))){int cents=note.pitch*100;memcpy(&v->params.initial.pitch,&cents,sizeof(cents));}
  writePitch(*v);v->handle=call<Word>(9,&v->params,(Word)v);if(v->handle==-1)v->used=false;return v->used?v:nullptr;
 }
 void advancePitch(int samples){for(auto&v:voices)if(v.used&&v.glideSamples>0){double step=std::min(double(samples),v.glideSamples);v.currentPitch+=(v.targetPitch-v.currentPitch)*step/v.glideSamples;v.glideSamples-=step;if(v.glideSamples<=0)v.currentPitch=v.targetPitch;writePitch(v);}}
 bool sliding()const{for(const auto&v:voices)if(v.used&&v.glideSamples>0)return true;return false;}

 std::unique_ptr<Sample> sample;
 struct Preset{std::wstring path;std::string name;};std::vector<Preset> presets;
 struct Range {int lo=0,hi=MidiMax;bool isFloat=false;};std::vector<Range> ranges;
 // Distructor exposes parameters for inactive module types too. Their conversion
 // is identity until that module is active, so startup ranges are not reliable.
 Range currentRange(int index){
  if(FST::sameName(config.nameUtf8,"Distructor")){
   auto&r=ranges[index];r.isFloat=(disp(26,index)&2)!=0;
   r.lo=param(index,0,32);r.hi=param(index,MidiMax,32);
  }
  return ranges[index];
 }
 double rate=44100,tempo=120,ppq=0,tickRemaining=0;int block=4096;bool playing=false,tempoInitialized=false;double notifiedRate=0;
 void (*editCallback)(void*,int,double)=nullptr; void* editContext=nullptr; int suppressEdits=0;
 HWND parent=nullptr;int width=800,height=420;
 Impl(){host.owner=this;host.vt=hostVt.data();pathPtr=pathVt.data();for(auto&x:hostVt)x=(void*)noop;for(auto&x:pathVt)x=(void*)noop;
 hostVt[0]=(void*)dispatch;hostVt[1]=(void*)paramChanged;hostVt[2]=(void*)hint;
 hostVt[3]=(void*)panOld;hostVt[4]=(void*)releaseVoice;hostVt[5]=(void*)killVoice;hostVt[6]=(void*)voiceEvent;
 hostVt[7]=(void*)lock;hostVt[8]=(void*)unlock;hostVt[11]=(void*)mixMono;hostVt[12]=(void*)mixStereo;
 hostVt[13]=(void*)loadSampleHost;hostVt[14]=(void*)sampleData;hostVt[15]=(void*)closeSample;
 hostVt[16]=(void*)timeInt;hostVt[17]=(void*)timeDouble;hostVt[18]=(void*)timeDouble;
 hostVt[23]=(void*)sampleInfo;hostVt[30]=(void*)sampleRegion;hostVt[31]=(void*)pan;
 hostVt[32]=(void*)lock;hostVt[33]=(void*)unlock;hostVt[34]=(void*)lock;hostVt[35]=(void*)unlock;
 hostVt[43]=(void*)lock;hostVt[44]=(void*)unlock;
 hostVt[36]=(void*)ioBuffer;hostVt[37]=(void*)outBuffer;hostVt[25]=(void*)mixBuffer;hostVt[21]=(void*)delayedMsg;
 for(int w=0;w<10;w++){host.waves[w]=waves[w].data();for(int i=0;i<16384;i++){double t=double(i)/16384;waves[w][i]=float(w==0?sin(t*6.283185307179586):w==1?1-4*abs(t-.5):w==2?(t<.5?1:-1):2*t-1);}}
 allocate(4096);}
 void allocate(int n){block=std::clamp(n,1,65536);scratch.resize(block*2);input.resize(block*2);voiceScratch.resize(block*2);temps.resize(block*8);for(int i=0;i<4;i++)host.temp[i]=temps.data()+i*block*2;}
 static Impl& self(Host*h){return *h->owner;}
 static Word __stdcall noop(void*,Word=0,Word=0,Word=0,Word=0){return 0;}
 template<class R,class... A> R call(int slot,A...a) {
  if(!closing&&!usable()){
   // TriggerVoice uses -1 for failure; zero can be a valid voice handle.
   if constexpr(std::is_same_v<R,Word>){if(slot==9)return Word(-1);}
   return R();
  }
  auto fn=(R(__stdcall*)(void*,A...))(*(void***)plug)[slot];
  if constexpr(std::is_void_v<R>){fn(plug,a...);if(!closing)usable();}
  else {R result=fn(plug,a...);if(!closing)usable();return result;}
 }
 Word disp(Word id,Word index=0,Word value=0){return call<Word>(1,id,index,value);}
 int param(int i,int v,int f){return call<int>(6,i,v,f);}
 static void __stdcall lock(Host*h,Word=0){self(h).mutex.lock();}
 static void __stdcall unlock(Host*h,Word=0){self(h).mutex.unlock();}
 static void __stdcall paramChanged(Host*h,Word,int index,int value){auto&s=self(h);std::lock_guard<std::recursive_mutex> guard(s.mutex);if(!s.editCallback||s.suppressEdits||!s.canAutomate(index)||size_t(index)>=s.ranges.size())return;auto r=s.currentRange(index);if(!s.canAutomate(index))return;if(r.isFloat){float f;memcpy(&f,&value,4);if(std::isfinite(f))s.editCallback(s.editContext,index,std::clamp(double(f),0.,1.));}else if(r.hi!=r.lo)s.editCallback(s.editContext,index,std::clamp((double(value)-r.lo)/(double(r.hi)-r.lo),0.,1.));}
 static void __stdcall hint(Host*,Word,const char*){}
 static void __stdcall pan(Host*,float*l,float*r,float p,float v){*l=v*std::min(1.f,1.f-p);*r=v*std::min(1.f,1.f+p);}
 static void __stdcall panOld(Host*h,float*l,float*r,int p,float v){pan(h,l,r,p/64.f,v);}
 static void __stdcall releaseVoice(Host*h,Word tag){auto&s=self(h);std::lock_guard<std::recursive_mutex>g(s.mutex);for(auto&v:s.voices)if((Word)&v==tag&&v.used){v.released=true;if(v.handle!=-1)s.call<void>(10,v.handle);break;}}
 static void __stdcall killVoice(Host*h,Word tag,int kill){auto&s=self(h);std::lock_guard<std::recursive_mutex>g(s.mutex);for(auto&v:s.voices)if((Word)&v==tag&&v.used){Word handle=v.handle;v.used=false;v.handle=-1;if(kill&&handle!=-1)s.call<void>(11,handle);break;}}
 static int __stdcall voiceEvent(Host*h,Word tag,Word event,Word,Word){auto&s=self(h);for(auto&v:s.voices)if((Word)&v==tag){switch(event){case 1:return -1;case 2:return v.channel;case 3:return floatBits(v.params.initial.vol);case 4:return floatBits(v.releaseVelocity);case 5:return floatBits(1.f);default:return 0;}}return 0;}
 static void __stdcall mixMono(Host*,float*src,float*dst,int n,float l,float r,float*lastL,float*lastR){if(!src||!dst||n<=0)return;float dl=(l-*lastL)/n,dr=(r-*lastR)/n;for(int i=0;i<n;i++){*lastL+=dl;*lastR+=dr;dst[2*i]+=src[i]*(*lastL);dst[2*i+1]+=src[i]*(*lastR);}*lastL=l;*lastR=r;}
 static void __stdcall mixStereo(Host*,float*src,float*dst,int n,float l,float r,float*lastL,float*lastR){if(!src||!dst||n<=0)return;float dl=(l-*lastL)/n,dr=(r-*lastR)/n;for(int i=0;i<n;i++){*lastL+=dl;*lastR+=dr;dst[2*i]+=src[2*i]*(*lastL);dst[2*i+1]+=src[2*i+1]*(*lastR);}*lastL=l;*lastR=r;}
 static bool __stdcall loadSampleHost(Host*h,Word*handle,char*filename,void*,int flags){auto&s=self(h);if(!s.sample||!handle)return false;*handle=(Word)s.sample.get();if(filename){strncpy(filename,s.sample->name.c_str(),255);filename[255]=0;}return true;}
 static void* __stdcall sampleData(Host*h,Word handle,int*length){auto&s=self(h);if(!s.sample||handle!=(Word)s.sample.get()){if(length)*length=0;return nullptr;}if(length)*length=int(s.sample->pcm.size()/2);return s.sample->pcm.data();}
 static void __stdcall closeSample(Host*,Word){} // Host owns lifetime; DLL borrows the sample.
 static void __stdcall sampleInfo(Host*h,Word handle,SampleInfo*out){auto&s=self(h);if(!out||out->size<56)return;int sz=out->size;SampleInfo v{};v.size=sz;v.loopStart=-1;v.loopEnd=-1;v.channels=2;v.format=0;v.rateConv=1;
 if(s.sample&&handle==(Word)s.sample.get()){v.data=s.sample->pcm.data();v.length=v.solidLength=int(s.sample->pcm.size()/2);v.rateConv=s.sample->rate/s.rate;}
 memcpy(out,&v,std::min(size_t(sz),sizeof v));}
 static void __stdcall sampleRegion(Host*,Word,int,void*out){if(out)memset(out,0,544);}
 static void __stdcall ioBuffer(Host*,Word,Word,void*out){if(out){void*zero=nullptr;memcpy(out,&zero,sizeof zero);}}
 // FPC routes its main output through GetOutBuffer(0), including a second call
 // with IO_Filled set. Preserve caller flags; buffers contain interleaved stereo.
 static void __stdcall outBuffer(Host*h,Word,Word index,void*out){if(out){void*buffer=index==0?self(h).scratch.data():nullptr;memcpy(out,&buffer,sizeof buffer);}}
 static void* __stdcall mixBuffer(Host*h,int index){return index==0?self(h).scratch.data():nullptr;}
 static void __stdcall delayedMsg(Host*h,Word,Word msg){auto&s=self(h);if(s.plug)s.call<void>(17,msg);}
 static int __stdcall timeInt(Host*h){return int(self(h).ppq*96);}
 static double __stdcall timeDouble(Host*h){return self(h).ppq*96;}
 static Word __stdcall dispatch(Host*h,Word tag,Word id,Word index,Word value){auto&s=self(h);
#ifdef GRANULIZER_TRACE
 fprintf(stderr,"host(%lld,%lld,%llx) host=%p vt=%p first=%p\n",(long long)id,(long long)index,(long long)value,h,h->vt,h->vt[0]);fflush(stderr);
#endif
 switch(id){
 // FHD_KillAutomation: inclusive parameter interval. This can arrive during
 // CreatePlugInstance, before PlugInfo or the parameter arrays exist.
 case 8:{std::lock_guard<std::recursive_mutex> guard(s.mutex);
  if(index>=0&&value>=index){for(Word i=index;i<=std::min<Word>(value,65535);++i)s.automationDisabled.set(size_t(i));}
  return 0;}
 // FHD_CheckProdCode has no supported implementation here. Keep the historical
 // unhandled-service response, without dereferencing Value or changing native
 // flags. Zero is not an activation result; the plugin keeps its own mode.
 case 26:return 0;
 // Native editor keyboards call these host services, rather than TriggerVoice.
// Sawer runtime: mouse-down => FHD_NoteOn20, key57, velocity99;
// mouse-up => FHD_NoteOff21, key57, value-1 (off ignores Value).
case 20:case 21:case 56:{
 const auto packed=static_cast<uintptr_t>(index);
 const int key=int(packed&0xffffu),color=int((packed>>16)&15u);
 if(!s.facade||key>127)return 0;
 if(id==21||(id==56&&value<=0))s.facade->previewNoteOff(key);
 else s.facade->previewNoteOn(key,color,float(std::clamp<Word>(value,0,128))/128.f);
 return 1;
}
 case 5:s.midiInput=value!=0;return 1;
 case 30:s.latency=int(std::clamp<Word>(value,0,10000000));return 1;
 case 2:if(s.plug){HWND w=*(HWND*)((char*)s.plug+24);RECT r{};if(w&&GetClientRect(w,&r)){s.width=r.right;s.height=r.bottom;}}return 0;
 case 12:{wchar_t file[32768]{};OPENFILENAMEW of{};of.lStructSize=sizeof(of);of.hwndOwner=s.parent;of.lpstrFilter=L"Wave audio (*.wav)\0*.wav\0\0";of.lpstrFile=file;of.nMaxFile=32768;of.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&of))s.load(file);return 0;}
 case 14:case 28:return value;
 case 24:return (Word)GetModuleHandleW(nullptr);
 case 29:return (Word)(index==1?s.exportPath:index==7?s.sharedPath:(s.rootPath.empty()?s.basePath:s.rootPath)).c_str();
 case 47:return (Word)s.exportPath.c_str();
 case 36:case 37:case 38:if(value){auto*t=(double*)value;t[0]=index==4?s.ppq*96:index==1?s.ppq*60000/s.tempo:s.ppq;t[1]=t[0];}return 1;
 case 39:return floatBits(1.f);
 case 46:return (Word)s.host.app;
 case 50:return 0; // No additional sidechain buses beyond the main stereo pair.
 case 71:return s.realPathManager?(Word)s.realPathManager:(Word)&s.pathPtr;
 case 84:return value==-1?0:0x303030;
 case 86:return 0;
 default:return 0;
 }}
 void killAll(){heldCount=0;if(!plug)return;for(auto&v:voices)if(v.used)stopVoice(v);}
 bool load(const wchar_t*path){std::lock_guard<std::recursive_mutex>g(mutex);if(!usable())return false;auto next=std::make_unique<Sample>();if(!readWave(path,*next)){failure="Use a mono/stereo PCM or float WAV file (8-192 kHz recommended).";return false;}
 SuppressEdits suppress(suppressEdits);killAll();disp(10,0,0);sample=std::move(next);disp(10,(Word)sample.get(),0);if(!usable())return false;failure.clear();return true;}
};

Engine::Engine():p(new Impl){p->facade=this;}
Engine::~Engine(){close();delete p;}
bool Engine::openConfigured(const wchar_t* ini){auto cfg=BridgeConfig::load(ini);if(!cfg.valid||!cfg.resolveInstallation()){p->failure=cfg.error;return false;}p->config=cfg;return open(cfg.dllPath.c_str());}
bool Engine::open(const wchar_t* dll){std::lock_guard<std::recursive_mutex>g(p->mutex);close();p->automationDisabled.reset();p->failure.clear();p->dialogOwnerKey=dll;p->host.app=acquireDialogOwner(p->dialogOwnerKey);p->basePath=utf8(dll);auto i=p->basePath.find_last_of("/\\");p->basePath=p->basePath.substr(0,i+1);p->rootPath=p->config.valid?utf8(p->config.rootPath.c_str()):p->basePath;if(!p->rootPath.empty()&&p->rootPath.back()!='\\')p->rootPath+='\\';p->sharedPath=p->rootPath+"Shared\\";
 // Exported files remain available after closing the plugin: DAWs may reference
 // them from a saved project. Each instance gets a unique writable directory.
 if(p->exportPath.empty()){wchar_t user[MAX_PATH]{};GUID id{};wchar_t guid[40]{};
  if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA|CSIDL_FLAG_CREATE,nullptr,SHGFP_TYPE_CURRENT,user))||FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,guid,40)){p->failure="Cannot locate a writable audio export folder";close();return false;}
  std::wstring folder=std::wstring(user)+L"\\FL Plugin Bridge\\Exports\\"+guid;
  int result=SHCreateDirectoryExW(nullptr,folder.c_str(),nullptr);if(result!=ERROR_SUCCESS&&result!=ERROR_ALREADY_EXISTS&&result!=ERROR_FILE_EXISTS){p->failure="Cannot create the audio export folder";close();return false;}
  p->exportPath=utf8(folder.c_str())+"\\";
 }
 if(!checkX64Dll(dll,p->failure))return false;
 for(const auto&path:p->config.dependencyPaths)if(!checkX64Dll(path,p->failure))return false;
 // Scoped per-module search: never change the DAW's process working directory.
 for(const auto&path:p->config.dependencyPaths){auto m=LoadLibraryExW(path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!m){p->failure="Cannot load dependency "+utf8(path.c_str())+", Windows error "+std::to_string(GetLastError());close();return false;}p->dependencies.push_back(m);
  auto name=path.substr(path.find_last_of(L"/\\")+1);
  if(!_wcsicmp(name.c_str(),L"QuickFontCache_x64.dll"))p->fonts.attach(m,p->config.rootPath);
 }

 std::wstring toolsPath=p->config.rootPath+L"/Shared/ILTools_x64.dll";
 p->toolsModule=LoadLibraryExW(toolsPath.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
 if(p->toolsModule){
  auto createPath=(void*(__stdcall*)(const char*,const char*,const char*,const char*,const char*))GetProcAddress(p->toolsModule,"CreatePluginPathManagerInstance");
  if(createPath)p->realPathManager=createPath(p->rootPath.c_str(),"","","","FL Studio");

 }
 if(!p->realPathManager){p->failure="Cannot initialize ILTools path manager";close();return false;}
 p->module=LoadLibraryExW(dll,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!p->module){p->failure="Cannot load native FL plugin. Windows error "+std::to_string(GetLastError());return false;}
 p->sharedLoader.attach(p->module,p->config.rootPath);
 auto create=(void*(__stdcall*)(void*,Word))GetProcAddress(p->module,"CreatePlugInstance");if(!create){p->failure="DLL does not export CreatePlugInstance";close();return false;}
 #ifdef GRANULIZER_TRACE
 fprintf(stderr,"CREATE host=%p vt=%p first=%p owner=%p\n",&p->host,p->host.vt,p->host.vt[0],p);fflush(stderr);
#endif
 p->plug=create(&p->host,(Word)p);if(!p->plug){p->failure="Original DLL refused to create an instance";close();return false;}
 p->info=*(PlugInfo**)((char*)p->plug+16);if(!p->info||p->info->params<0||p->info->params>65536){p->failure="Unsupported FL plugin ABI";close();return false;}
 if(!p->usable()){close();return false;}
 if(p->config.valid&&bool(p->info->flags&1)!=p->config.instrument){p->failure="Native plugin kind does not match bridge configuration";close();return false;}
 if(p->info->flags&(1<<26)){p->failure="VFX note routing requires the FL Studio host";close();return false;}
 p->ranges.resize(p->info->params);for(int k=0;k<p->info->params&&p->usable();k++){p->ranges[k].isFloat=(p->disp(26,k)&2)!=0;p->ranges[k].lo=p->param(k,0,32);p->ranges[k].hi=p->param(k,MidiMax,32);}
 if(!p->usable()){close();return false;}
 // Presets are host-owned FL containers. Expose only identity-checked native
 // states; wrapper/mixer metadata remains the responsibility of the DAW.
 {
  std::wstring dir=p->config.rootPath+L"\\Data\\Patches\\Plugin presets\\"+(p->config.instrument?L"Generators\\":L"Effects\\")+p->config.name+L"\\";
  auto scan=[&](auto&& visit,const std::wstring& relative,int depth)->void{
   if(depth>16||p->presets.size()>=4096)return;WIN32_FIND_DATAW f{};HANDLE search=FindFirstFileW((dir+relative+L"*").c_str(),&f);if(search==INVALID_HANDLE_VALUE)return;
   do{if(f.cFileName[0]==L'.'||(f.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))continue;std::wstring name=f.cFileName;
    if(f.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)visit(visit,relative+name+L"\\",depth+1);
    else if(name.size()>4&&!_wcsicmp(name.c_str()+name.size()-4,L".fst")&&p->presets.size()<4096){
     std::vector<uint8_t> native;std::string error;
     if(!FST::parseFst((dir+relative+name).c_str(),p->config.nameUtf8,native,error))continue;
     std::wstring label=relative+name.substr(0,name.size()-4);std::replace(label.begin(),label.end(),L'\\',L'/');p->presets.push_back({dir+relative+name,utf8(label.c_str())});
    }
   }while(FindNextFileW(search,&f));FindClose(search);
  };scan(scan,L"",0);
  std::sort(p->presets.begin(),p->presets.end(),[](const auto&a,const auto&b){return a.name<b.name;});
 }
 configure(44100,4096);if(p->info->flags&1)p->call<int>(5,1,p->voiceLimit(),0);p->disp(21,0,30);p->disp(40);if(!p->usable()){close();return false;}return true;}
void Engine::close(){std::lock_guard<std::recursive_mutex>g(p->mutex);p->closing=true;if(p->plug){p->killAll();p->disp(0);p->call<void>(0);p->plug=nullptr;p->info=nullptr;}p->closing=false;p->tempoInitialized=false;p->notifiedRate=0;p->channel=ChannelSettings{};p->sample.reset();p->ranges.clear();p->presets.clear();
 p->sharedLoader.detach();
 if(p->module){FreeLibrary(p->module);p->module=nullptr;}
 p->fonts.detach();
 if(p->realPathManager){auto destroy=(void(__stdcall*)(void*))GetProcAddress(p->toolsModule,"DestroyPathManager");if(destroy)destroy(p->realPathManager);p->realPathManager=nullptr;}
 if(p->toolsModule){FreeLibrary(p->toolsModule);p->toolsModule=nullptr;}
 for(auto it=p->dependencies.rbegin();it!=p->dependencies.rend();++it)FreeLibrary(*it);p->dependencies.clear();if(p->host.app){releaseDialogOwner(p->dialogOwnerKey);p->host.app=nullptr;}p->dialogOwnerKey.clear();}
bool Engine::ready()const{std::lock_guard<std::recursive_mutex>g(p->mutex);return p->usable();}
bool Engine::canAutomate(int index)const{std::lock_guard<std::recursive_mutex>g(p->mutex);return p->canAutomate(index);}
const char* Engine::error()const{return p->failure.c_str();}
bool Engine::isInstrument()const{return p->info&&(p->info->flags&1);}
int Engine::nativeFlags()const{return p->info?p->info->flags:0;}
ChannelSettings Engine::channelSettings()const{std::lock_guard<std::recursive_mutex>g(p->mutex);return p->channel;}
bool Engine::supportsPortamento()const{return isInstrument();}
void Engine::setChannelSettings(const ChannelSettings&value){
 std::lock_guard<std::recursive_mutex>g(p->mutex);ChannelSettings next=value;next.sanitize();
 const bool modeChanged=next.mono!=p->channel.mono;
 const double shift=(p->channel.rootNote-next.rootNote+next.transpose-p->channel.transpose)*100.0+next.fineCents-p->channel.fineCents;
 if(modeChanged)p->killAll();p->channel=next;
 if(!p->usable()||!(p->info->flags&1))return;
 p->enforceLimit();p->call<int>(5,1,p->voiceLimit(),0);
 for(auto&v:p->voices)if(v.used){v.currentPitch+=shift;v.targetPitch+=shift;if(!next.portamento||next.slideMs==0){v.currentPitch=v.targetPitch;v.glideSamples=0;}p->writePitch(v);}
}
bool Engine::supportsSampleLoad()const{return p->info&&(p->info->flags&(1<<19));}
int Engine::latencySamples()const{return p->latency;}
int Engine::parameterCount()const{return p->info?p->info->params:0;}
void Engine::parameterName(int index,char*name,int capacity){if(capacity<1)return;*name=0;std::lock_guard<std::recursive_mutex>g(p->mutex);if(!p->usable()||index<0||index>=parameterCount())return;char s[256]{};p->call<void>(4,0,index,0,s);strncpy(name,s,capacity-1);name[capacity-1]=0;}
double Engine::getParameter(int index){std::lock_guard<std::recursive_mutex>g(p->mutex);if(!p->usable()||index<0||index>=parameterCount())return 0;auto r=p->currentRange(index);int v=p->param(index,0,2);if(r.isFloat){float f;memcpy(&f,&v,4);return std::isfinite(f)?std::clamp(double(f),0.,1.):0;}if(r.hi==r.lo)return 0;return std::clamp((double(v)-r.lo)/(double(r.hi)-r.lo),0.,1.);}
void Engine::setParameter(int index,double value){std::lock_guard<std::recursive_mutex>g(p->mutex);if(!p->canAutomate(index)||!std::isfinite(value))return;++p->suppressEdits;p->param(index,int(std::clamp(value,0.,1.)*MidiMax),1|16|32);--p->suppressEdits;}
void Engine::configure(double rate,int maxBlock){std::lock_guard<std::recursive_mutex>g(p->mutex);if(!std::isfinite(rate)||rate<8000||rate>384000)return;for(auto&v:p->voices)if(v.used&&v.glideSamples>0)v.glideSamples*=rate/p->rate;p->rate=rate;p->allocate(maxBlock);if(!p->usable())return;p->disp(3,0,p->block);p->disp(4,0,Word(rate));p->disp(1,0,0);struct{int bar,beat,ppq;}sig{16,4,96};p->disp(14,0,(Word)&sig);p->disp(11,0,1);setTransport(p->tempo,p->ppq,p->playing);}
void Engine::setTransport(double tempo,double ppq,bool playing){
 std::lock_guard<std::recursive_mutex>g(p->mutex);if(!p->usable())return;
 if(std::isfinite(tempo)&&tempo>0){bool changed=!p->tempoInitialized||tempo!=p->tempo||p->notifiedRate!=p->rate;p->tempo=tempo;
  if(changed){p->call<int>(5,0,floatBits(float(tempo)),int(p->rate*60/(tempo*96)));p->disp(20,0,floatBits(float(p->rate*60/(tempo*96))));p->tempoInitialized=true;p->notifiedRate=p->rate;}
 }
 if(std::isfinite(ppq)){double tolerance=2*p->tempo/(p->rate*60);bool seek=std::abs(ppq-p->ppq)>tolerance;p->ppq=ppq;
  if(seek){p->tickRemaining=0;p->disp(13);}
 }
 if(playing!=p->playing){p->playing=playing;p->disp(12,0,playing);}
}
void Engine::noteOn(int id,int pitch,int channel,float velocity){noteOnInternal(id,pitch,channel,velocity,false);}
void Engine::noteOnInternal(int id,int pitch,int channel,float velocity,bool preview){
 std::lock_guard<std::recursive_mutex>g(p->mutex);
 if(!p->usable()||!(p->info->flags&1)||pitch<0||pitch>127||channel<0||channel>15||!std::isfinite(velocity))return;
 if(velocity<=0){noteOffInternal(id,pitch,channel,0,preview);return;}
 Impl::HeldNote note{id,pitch,channel,std::clamp(velocity,0.f,1.f),++p->nextSerial,preview};
 if(p->channel.mono){
  // A host may replace an existing note ID. Keep one held entry per valid ID.
  if(id>=0){for(size_t i=0;i<p->heldCount;){if(p->held[i].id==id&&p->held[i].preview==preview){std::move(p->held.begin()+i+1,p->held.begin()+p->heldCount,p->held.begin()+i);--p->heldCount;}else ++i;}}
  if(p->heldCount==p->held.size()){std::move(p->held.begin()+1,p->held.end(),p->held.begin());--p->heldCount;}
  p->held[p->heldCount++]=note;
  if(auto*v=p->activeMono())p->retarget(*v,note,p->channel.portamento&&p->channel.slideMs>0);
  else p->trigger(note);
 }else p->trigger(note);
}
void Engine::noteOff(int id,int pitch,int channel,float velocity){noteOffInternal(id,pitch,channel,velocity,false);}
void Engine::noteOffInternal(int id,int pitch,int channel,float velocity,bool preview){
 std::lock_guard<std::recursive_mutex>g(p->mutex);if(!p->usable())return;
 velocity=std::isfinite(velocity)?std::clamp(velocity,0.f,1.f):0;
 if(p->channel.mono){
  size_t found=p->heldCount;for(size_t i=0;i<p->heldCount;++i)if(p->held[i].preview==preview&&(id>=0?p->held[i].id==id:(p->held[i].pitch==pitch&&p->held[i].channel==channel))){found=i;break;}
  if(found==p->heldCount)return;
  const bool wasCurrent=found==p->heldCount-1;
  std::move(p->held.begin()+found+1,p->held.begin()+p->heldCount,p->held.begin()+found);--p->heldCount;
  if(!wasCurrent)return;
  auto*v=p->activeMono();
  if(p->heldCount){const auto&note=p->held[p->heldCount-1];if(v)p->retarget(*v,note,p->channel.portamento&&p->channel.slideMs>0);else p->trigger(note);}
  else if(v){v->released=true;v->releaseVelocity=velocity;p->call<void>(10,v->handle);}
 }else for(auto&v:p->voices)if(v.used&&!v.released&&v.preview==preview&&(id>=0?v.id==id:(v.pitch==pitch&&v.channel==channel))){v.released=true;v.releaseVelocity=velocity;p->call<void>(10,v.handle);if(id<0)break;}
}
void Engine::previewNoteOn(int pitch,int channel,float velocity){
 std::lock_guard<std::recursive_mutex>g(p->mutex);
 if(pitch<0||pitch>127||channel<0||channel>15)return;
 // Preview identity is separate from DAW IDs, even when their numeric values match.
 const int id=pitch+channel*128;noteOffInternal(id,pitch,channel,0,true);
 if(velocity>0)noteOnInternal(id,pitch,channel,velocity,true);
}
void Engine::previewNoteOff(int pitch){
 std::lock_guard<std::recursive_mutex>g(p->mutex);if(pitch<0||pitch>127)return;
 for(int channel=0;channel<16;++channel)noteOffInternal(pitch+channel*128,pitch,channel,0,true);
}
void Engine::clearPreviewNotes(){
 std::lock_guard<std::recursive_mutex>g(p->mutex);
 if(p->channel.mono){for(size_t i=p->heldCount;i>0;){--i;if(p->held[i].preview){auto note=p->held[i];noteOffInternal(note.id,note.pitch,note.channel,0,true);}}}
 else for(auto&v:p->voices)if(v.used&&!v.released&&v.preview)noteOffInternal(v.id,v.pitch,v.channel,0,true);
}
void Engine::allNotesOff(){std::lock_guard<std::recursive_mutex>g(p->mutex);p->killAll();}
void Engine::render(float*left,float*right,int frames,const float*inLeft,const float*inRight){if(!left||!right||frames<0)return;std::lock_guard<std::recursive_mutex>g(p->mutex);if(!p->usable()){std::fill_n(left,frames,0);std::fill_n(right,frames,0);return;}
 for(int offset=0;offset<frames;){if(p->tickRemaining<=0){if(p->info->flags&(1<<5))p->call<void>(14);p->tickRemaining+=p->rate*60/(p->tempo*96);}int n=std::min({frames-offset,p->block,std::max(1,int(std::ceil(p->tickRemaining)))});std::fill_n(p->scratch.data(),n*2,0);if(p->sliding())n=std::min(n,32);int rendered=n;
 if(!(p->info->flags&1)){for(int j=0;j<n;j++){p->input[j*2]=inLeft?inLeft[offset+j]:0;p->input[j*2+1]=inRight?inRight[offset+j]:0;}if(p->info->flags&(1<<6))std::copy_n(p->input.data(),n*2,p->scratch.data());else p->call<void>(7,p->input.data(),p->scratch.data(),n);}
 else if(p->info->flags&(1<<2)){for(auto&v:p->voices)if(v.used){std::fill_n(p->voiceScratch.data(),n*2,0);int vn=n;int flags=p->call<int>(13,v.handle,p->voiceScratch.data(),&vn);vn=std::clamp(vn,0,n);for(int j=0;j<vn*2;j++)p->scratch[j]+=p->voiceScratch[j];if(flags&1){Word h=v.handle;v.used=false;v.handle=-1;p->call<void>(11,h);}}}
 else if(!(p->info->flags&(1<<6)))p->call<void>(8,p->scratch.data(),&rendered);
 if(!p->usable()){std::fill_n(left,frames,0);std::fill_n(right,frames,0);return;}
 rendered=std::clamp(rendered,0,n);for(int j=0;j<n;j++){float l=j<rendered?p->scratch[j*2]:0,r=j<rendered?p->scratch[j*2+1]:0;if(p->info->flags&1){l*=p->channel.gain*std::min(1.f,1.f-p->channel.pan);r*=p->channel.gain*std::min(1.f,1.f+p->channel.pan);}left[offset+j]=std::isfinite(l)?l:0;right[offset+j]=std::isfinite(r)?r:0;}p->advancePitch(n);offset+=n;p->tickRemaining-=n;if(p->playing)p->ppq+=n*p->tempo/(p->rate*60);}}
bool Engine::save(std::vector<uint8_t>&out){std::lock_guard<std::recursive_mutex>g(p->mutex);if(!p->usable())return false;IStream*s=nullptr;if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&s)))return false;p->call<void>(3,s,1);STATSTG stat{};bool ok=p->usable()&&SUCCEEDED(s->Stat(&stat,STATFLAG_NONAME))&&stat.cbSize.QuadPart<MaxState;std::vector<uint8_t>state;if(ok){state.resize(size_t(stat.cbSize.QuadPart));LARGE_INTEGER zero{};s->Seek(zero,STREAM_SEEK_SET,nullptr);ULONG got=0;ok=SUCCEEDED(s->Read(state.data(),ULONG(state.size()),&got))&&got==state.size();}s->Release();if(!ok)return false;
 uint32_t head[5]={0x31475246,uint32_t(state.size()),p->sample?p->sample->rate:44100,uint32_t(p->sample?p->sample->pcm.size():0),uint32_t(p->sample?p->sample->name.size():0)};size_t bytes=sizeof(head)+state.size()+size_t(head[3])*2+head[4];if(bytes>MaxState)return false;out.resize(bytes);uint8_t*q=out.data();memcpy(q,head,sizeof head);q+=sizeof head;memcpy(q,state.data(),state.size());q+=state.size();if(p->sample){memcpy(q,p->sample->pcm.data(),size_t(head[3])*2);q+=size_t(head[3])*2;memcpy(q,p->sample->name.data(),head[4]);}return true;}
bool Engine::restore(const uint8_t*data,size_t size){std::lock_guard<std::recursive_mutex>g(p->mutex);if(!p->usable()||!data||size<20||size>MaxState)return false;SuppressEdits suppress(p->suppressEdits);uint32_t h[5];memcpy(h,data,20);if(h[0]!=0x31475246||size_t(h[1])+size_t(h[3])*2+h[4]+20!=size||h[3]%2||h[2]<8000||h[2]>384000||h[4]>32768)return false;auto next=std::make_unique<Sample>();next->rate=h[2];next->pcm.resize(h[3]);memcpy(next->pcm.data(),data+20+h[1],h[3]*2);next->name.assign((const char*)data+20+h[1]+h[3]*2,h[4]);IStream*s=nullptr;if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&s)))return false;ULONG wrote=0;HRESULT writeResult=s->Write(data+20,h[1],&wrote);LARGE_INTEGER zero{};if(FAILED(writeResult)||wrote!=h[1]||FAILED(s->Seek(zero,STREAM_SEEK_SET,nullptr))){s->Release();return false;}p->killAll();if(supportsSampleLoad())p->disp(10);p->sample=h[3]?std::move(next):nullptr;p->call<void>(3,s,0);s->Release();if(supportsSampleLoad())p->disp(10,(Word)p->sample.get(),0);p->disp(27);return p->usable()&&wrote==h[1];}
bool Engine::showEditor(void*parent){std::lock_guard<std::recursive_mutex>g(p->mutex);if(!p->usable()||!parent)return false;p->parent=(HWND)parent;p->disp(0,0,(Word)parent);if(!p->usable())return false;HWND child=*(HWND*)((char*)p->plug+24);if(!child)return false;RECT r{};GetClientRect(child,&r);p->width=r.right;p->height=r.bottom;ShowWindow(child,SW_SHOW);return true;}
void Engine::hideEditor(){std::lock_guard<std::recursive_mutex>g(p->mutex);clearPreviewNotes();if(p->plug)p->disp(0);p->parent=nullptr;}
void Engine::editorSize(int&w,int&h){std::lock_guard<std::recursive_mutex>g(p->mutex);if(p->plug){HWND child=*(HWND*)((char*)p->plug+24);RECT r{};if(child&&IsWindow(child)&&GetClientRect(child,&r)&&r.right>0&&r.bottom>0){p->width=r.right;p->height=r.bottom;}}w=p->width;h=p->height;}
void Engine::idle(){std::lock_guard<std::recursive_mutex>g(p->mutex);if(p->plug)p->call<void>(2);}
bool Engine::loadSample(const wchar_t*file){std::lock_guard<std::recursive_mutex>g(p->mutex);return supportsSampleLoad()&&p->load(file);}

void Engine::setParameterEditCallback(void (*cb)(void*,int,double),void*ctx){std::lock_guard<std::recursive_mutex>g(p->mutex);p->editCallback=cb;p->editContext=ctx;}

bool Engine::canLoadFile()const{if(supportsSampleLoad())return true;const auto&n=p->config.name;
 // These legacy dispatchers are confirmed against the original native modules.
 // Other sample editors keep their own native OLE and WM_DROPFILES handlers.
 return n==L"Slicex"||n==L"Fruity Convolver"||n==L"Edison"||n==L"FPC";}
bool Engine::loadFile(const wchar_t*file){std::lock_guard<std::recursive_mutex>g(p->mutex);if(!p->usable()||!file||!*file||!canLoadFile())return false;
 WIN32_FILE_ATTRIBUTE_DATA info{};if(!GetFileAttributesExW(file,GetFileExInfoStandard,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)){p->failure="Sample file does not exist";return false;}
 // FL-host sample instruments use the host's sample service, not dispatcher 18.
 if(supportsSampleLoad())return p->load(file);
 std::vector<uint8_t>before,after;const bool saved=save(before);std::string path=utf8(file);Word handled=p->disp(18,0,(Word)path.c_str());
 if(!p->usable())return false;
 if(handled==2){p->failure="The native plugin rejected this file";return false;}
 bool accepted=handled==1;
 if(!accepted&&handled==0&&p->config.name!=L"FPC"&&saved&&save(after)){
  // Edison/Slicex embed the imported audio, and Convolver stores its file path.
  // Their legacy implementation returns zero even when the import succeeds.
  accepted=before!=after;
  if(!accepted&&p->lastAcceptedDrop==file){const auto&old=p->lastAcceptedDropInfo;
   accepted=old.nFileSizeHigh==info.nFileSizeHigh&&old.nFileSizeLow==info.nFileSizeLow&&CompareFileTime(&old.ftLastWriteTime,&info.ftLastWriteTime)==0;}
 }
 if(!p->usable())return false;
 if(accepted){p->lastAcceptedDrop=file;p->lastAcceptedDropInfo=info;p->failure.clear();return true;}
 p->failure="This plugin did not accept the file drop";return false;}

void* Engine::dialogOwner()const{return p->host.app;}

int Engine::presetCount()const{return int(p->presets.size());}
void Engine::presetName(int index,char*name,int capacity)const{if(!name||capacity<1)return;*name=0;if(index<0||size_t(index)>=p->presets.size())return;strncpy(name,p->presets[index].name.c_str(),capacity-1);name[capacity-1]=0;}
bool Engine::loadPreset(int index){std::lock_guard<std::recursive_mutex>g(p->mutex);if(!p->usable()||index<0||size_t(index)>=p->presets.size())return false;
 std::vector<uint8_t> data;if(!FST::parseFst(p->presets[index].path.c_str(),p->config.nameUtf8,data,p->failure))return false;
 if(FST::sameName(p->config.nameUtf8,"Fruity Convolver")&&!PresetPaths::relocateConvolver(data,p->config.rootPath,p->failure))return false;
 if(FST::sameName(p->config.nameUtf8,"Harmor")&&!PresetPaths::relocateHarmor(data,p->config.rootPath,p->failure))return false;
 IStream* stream=nullptr;if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)))return false;ULONG wrote=0;LARGE_INTEGER zero{};
 if(FAILED(stream->Write(data.data(),ULONG(data.size()),&wrote))||wrote!=data.size()||FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr))){stream->Release();return false;}
 {SuppressEdits suppress(p->suppressEdits);p->killAll();p->disp(2);p->call<void>(3,stream,0);p->disp(27);if(p->info->flags&1)p->call<int>(5,1,p->voiceLimit(),0);}
 stream->Release();if(!p->usable())return false;p->lastAcceptedDrop.clear();p->failure.clear();return true;}

void* Engine::nativeModule() const {return p->module;}
