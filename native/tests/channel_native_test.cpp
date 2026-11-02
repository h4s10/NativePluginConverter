// SPDX-License-Identifier: MIT
#include <vector>
#include <algorithm>
#include <cmath>
#define private public
#include "../bridge/engine.h"
#undef private
#include "../bridge/engine.cpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
static DWORD WINAPI watchdog(void*){Sleep(45000);TerminateProcess(GetCurrentProcess(),124);return 0;}
int wmain(int argc,wchar_t**argv){try{
 if(argc<3)return 2;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);CloseHandle(CreateThread(nullptr,0,watchdog,nullptr,0,nullptr));CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 Engine e;check(e.openConfigured(argv[1]),e.error());check(e.isInstrument(),"not instrument");e.configure(48000,512);std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
 auto render=[&](int n,const char*file){std::vector<float>l(n),r(n);for(int off=0;off<n;off+=512)e.render(l.data()+off,r.data()+off,std::min(512,n-off));double energy=0;for(auto v:l){check(std::isfinite(v),"nonfinite audio");energy+=v*v;}if(file){std::ofstream f(out/file,std::ios::binary);f.write((char*)l.data(),l.size()*4);std::cout<<file<<" energy="<<energy<<"\n";}return energy;};
 e.noteOn(1,60,0,.8);check(render(32768,"base.f32")>1e-8,"default silence");ChannelSettings s;s.transpose=12;e.setChannelSettings(s);render(4096,nullptr);check(render(32768,"octave.f32")>1e-8,"transpose silence");s.transpose=0;s.rootNote=72;e.setChannelSettings(s);render(4096,nullptr);check(render(32768,"root.f32")>1e-8,"root silence");
 e.allNotesOff();s=ChannelSettings{};s.mono=true;s.portamento=true;s.slideMs=100;e.setChannelSettings(s);e.noteOn(10,60,0,.8);render(8192,nullptr);e.noteOn(11,72,0,.8);check(e.p->usedVoices()==1,"mono voices");render(4800,"glide.f32");check(std::abs(e.p->activeMono()->currentPitch-1200)<.01,"glide endpoint");check(render(32768,"legato.f32")>1e-8,"legato silence");e.noteOff(11,72,0,0);render(4800,nullptr);check(e.p->activeMono()->pitch==60,"held fallback");check(render(32768,"fallback.f32")>1e-8,"fallback silence");e.noteOff(10,60,0,0);render(16384,nullptr);e.allNotesOff();
 s=ChannelSettings{};s.maxPolyphony=2;e.setChannelSettings(s);e.noteOn(20,60,0,.8);e.noteOn(21,64,0,.8);e.noteOn(22,67,0,.8);check(e.p->usedVoices()<=2,"polyphony exceeded");render(8192,nullptr);e.noteOff(20,60,0,0);e.noteOff(21,64,0,0);e.noteOff(22,67,0,0);render(8192,nullptr);e.allNotesOff();
 std::cout<<"PASS actual native engine: pitch changes, mono/porta/fallback, poly limit; flags="<<e.nativeFlags()<<"\n";e.close();CoUninitialize();return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
