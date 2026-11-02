// SPDX-License-Identifier: MIT
// Behavioral test of host voice allocation against a fake native ABI. No DAW.
#include <vector>
#include <algorithm>
#include <cmath>
#define private public
#include "../bridge/engine.h"
#undef private
#include "../bridge/engine.cpp"
#include <stdexcept>
#include <iostream>
namespace {
int checks=0;
void check(bool b,const char*why){++checks;if(!b)throw std::runtime_error(why);}
struct FakeVoice {VoiceParams*params=nullptr;Word tag=0;bool active=false,released=false;};
struct Fake {void**vt=nullptr;std::array<void*,32> methods{};std::array<FakeVoice,256> voices{};Engine::Impl*engine=nullptr;int triggers=0,kills=0,releases=0,maxPoly=0;bool old=false;float renderedPitch=0;
 Fake(){methods.fill((void*)noop);methods[5]=(void*)event;methods[8]=(void*)render;methods[9]=(void*)trigger;methods[10]=(void*)release;methods[11]=(void*)kill;vt=methods.data();}
 static Word __stdcall noop(void*,Word=0,Word=0,Word=0,Word=0){return 0;}
 static int __stdcall event(Fake*s,int id,int value,int){if(id==1)s->maxPoly=value;return 0;}
 static Word __stdcall trigger(Fake*s,VoiceParams*p,Word tag){s->triggers++;for(size_t i=0;i<s->voices.size();++i)if(!s->voices[i].active){s->voices[i]={p,tag,true,false};return Word(i);}return -1;}
 static void __stdcall release(Fake*s,Word h){check(h>=0&&h<256&&s->voices[h].active,"release dead handle");s->releases++;s->voices[h].released=true;}
 static void __stdcall kill(Fake*s,Word h){check(h>=0&&h<256&&s->voices[h].active,"kill dead handle");s->kills++;s->voices[h].active=false;Engine::Impl::killVoice(&s->engine->host,s->voices[h].tag,0);}
 static void __stdcall render(Fake*s,float*out,int*n){float level=0;for(auto&v:s->voices)if(v.active){level+=1;if(s->old){int c;memcpy(&c,&v.params->final.pitch,4);s->renderedPitch=float(c);}else s->renderedPitch=v.params->final.pitch;}for(int i=0;i<*n*2;++i)out[i]=level;}
 int count()const{int n=0;for(auto&v:voices)n+=v.active;return n;}
};
struct Fixture {Fake fake;Engine e;PlugInfo info{};Fixture(bool old=false){info.flags=1|(old?0:1<<21);e.p->plug=&fake;e.p->info=&info;fake.engine=e.p;fake.old=old;e.configure(48000,512);}~Fixture(){e.close();}Voice*current(){return e.p->activeMono();}void render(int n){std::vector<float>l(n),r(n);e.render(l.data(),r.data(),n);} };
}
int main(){try{
 {Fixture f;f.e.noteOn(1,60,0,1);check(f.fake.count()==1,"default trigger");auto&v=f.fake.voices[0];check(v.params->initial.pitch==6000&&v.params->final.pitch==0,"default pitch regression");auto s=f.e.channelSettings();s.maxPolyphony=2;f.e.setChannelSettings(s);f.e.noteOn(2,64,0,1);f.e.noteOn(3,67,0,1);check(f.fake.count()==2&&f.fake.kills==1,"max poly steals oldest");f.e.noteOff(1,60,0,0);check(f.fake.releases==0,"stolen noteoff released another note");f.e.noteOff(2,64,0,.3);check(f.fake.releases==1&&f.fake.count()==2,"release tail was killed");f.e.noteOn(4,69,0,1);check(f.fake.count()==2&&f.fake.kills==2,"released tail not preferred for steal");s.maxPolyphony=1;f.e.setChannelSettings(s);check(f.fake.count()==1&&f.fake.maxPoly==1,"limit reduction not applied");f.e.allNotesOff();check(f.fake.count()==0,"all notes off");}
 {Fixture f;auto s=f.e.channelSettings();s.mono=true;f.e.setChannelSettings(s);f.e.noteOn(10,60,0,.5);f.e.noteOn(11,64,0,.8);f.e.noteOn(12,67,1,.9);check(f.fake.count()==1&&f.fake.triggers==1,"mono retriggers overlapping notes");check(f.current()->id==12&&f.current()->params.final.pitch==700,"mono last note");f.e.noteOff(11,64,0,0);check(f.current()->id==12,"non-current noteoff changed mono");f.e.noteOff(12,67,1,0);check(f.current()->id==10&&f.current()->params.final.pitch==0,"held fallback missing");f.e.noteOff(10,60,0,.7);check(f.fake.releases==1&&!f.current(),"final mono release");f.e.noteOn(13,65,0,1);check(f.fake.triggers==2&&f.fake.count()==1,"new phrase cannot replace release tail");f.e.allNotesOff();check(f.e.p->heldCount==0,"panic retained held keys");f.e.noteOff(13,65,0,0);check(f.fake.count()==0,"stale noteoff restarts voice");}
 for(bool old:{false,true}){Fixture f(old);auto s=f.e.channelSettings();s.mono=true;s.portamento=true;s.slideMs=100;f.e.setChannelSettings(s);f.e.noteOn(1,60,0,1);f.e.noteOn(2,72,0,1);check(f.current()->currentPitch==0&&f.current()->targetPitch==1200,"porta initial target");f.render(2400);check(std::abs(f.current()->currentPitch-600)<.01,"porta midpoint");f.render(2400);check(f.current()->currentPitch==1200&&f.current()->glideSamples==0,"porta endpoint");check(f.fake.renderedPitch>=1190&&f.fake.renderedPitch<=1200,"native does not see live pitch");f.e.noteOff(2,72,0,0);f.render(4800);check(f.current()->currentPitch==0,"porta held fallback");s.rootNote=72;s.transpose=2;s.fineCents=33;f.e.setChannelSettings(s);check(f.current()->currentPitch==-967,"root and tuning not applied live");check(f.current()->params.initial.pitch==(old?([]{float x;int n=6000;memcpy(&x,&n,4);return x;})():6000),"original key mutated");}
 {Fixture f;f.e.noteOn(1,60,0,1);auto s=f.e.channelSettings();s.gain=.5;s.pan=1;f.e.setChannelSettings(s);float l[20]{},r[20]{};f.e.render(l,r,20);check(l[0]==0&&r[0]==.5,"output gain/pan");s.mono=true;f.e.setChannelSettings(s);check(f.fake.count()==0,"mode switch retained orphan voices");s.rootNote=-9;s.gain=NAN;s.pan=INFINITY;s.maxPolyphony=1000;s.slideMs=-4;f.e.setChannelSettings(s);s=f.e.channelSettings();check(s.rootNote==0&&s.gain==1&&s.pan==0&&s.maxPolyphony==256&&s.slideMs==0,"invalid settings sanitize");}
 {Fixture f;auto s=f.e.channelSettings();s.mono=true;f.e.setChannelSettings(s);for(int i=0;i<300;++i)f.e.noteOn(i,i%128,0,1);check(f.e.p->heldCount==256&&f.fake.count()==1,"bounded held queue");for(int i=299;i>=0;--i)f.e.noteOff(i,i%128,0,0);check(!f.current()&&f.e.p->heldCount==0,"bounded queue noteoff");}
 {Fixture f;f.e.noteOn(60,60,0,1);f.e.previewNoteOn(60,0,1);check(f.fake.count()==2,"preview ID collides with DAW ID");f.e.previewNoteOff(60);check(f.fake.releases==1,"preview released DAW note");f.e.noteOff(60,60,0,0);check(f.fake.releases==2,"DAW note identity lost");}
 {Fixture f;auto s=f.e.channelSettings();s.mono=true;f.e.setChannelSettings(s);f.e.noteOn(60,48,0,1);f.e.previewNoteOn(60,0,1);check(f.current()->preview&&f.current()->pitch==60,"mono preview priority");f.e.clearPreviewNotes();check(!f.current()->preview&&f.current()->pitch==48,"closing editor did not restore held MIDI note");f.e.noteOff(60,48,0,0);check(f.fake.releases==1,"MIDI release after preview fallback");}
 {Fixture f;Engine::Impl::dispatch(&f.e.p->host,0,20,57,99);check(f.fake.count()==1&&f.fake.voices[0].params->initial.vol==99/128.f,"preview dispatcher velocity");Engine::Impl::dispatch(&f.e.p->host,0,21,57,-1);check(f.fake.releases==1,"preview noteoff ignores negative value");Engine::Impl::dispatch(&f.e.p->host,0,56,60|(4<<16)|(1<<30),128);check(f.e.p->voices[1].channel==4,"preview channel includes recording bit");Engine::Impl::dispatch(&f.e.p->host,0,56,60|(4<<16),0);check(f.fake.releases==2,"combined preview noteoff");Engine::Impl::dispatch(&f.e.p->host,0,20,200,99);check(f.fake.triggers==2,"invalid preview pitch");}
 std::cout<<"PASS "<<checks<<" channel voice, note identity, polyphony, release, mono fallback, float/int glide, tuning and output checks\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
