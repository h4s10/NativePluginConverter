// SPDX-License-Identifier: MIT
// Synthetic native ABI tests: no vendor DLLs, license keys or activation data.
#include <vector>
#define private public
#include "../bridge/engine.h"
#undef private
#include "../bridge/engine.cpp"
#include <stdexcept>
#include <iostream>
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
struct Fake {
 void** vt=nullptr;std::array<void*,32> methods{};Engine::Impl* engine=nullptr;
 int writes=0,reads=0,parameters=0,renders=0,destroyed=0,dispatches=0;
 bool demoOnSave=false,demoOnRestore=false,demoOnRender=false,productOnRender=false;
 Fake(){methods.fill((void*)noop);methods[0]=(void*)destroy;methods[1]=(void*)dispatch;methods[3]=(void*)state;methods[6]=(void*)parameter;methods[7]=(void*)render;vt=methods.data();}
 static Word __stdcall noop(void*,Word=0,Word=0,Word=0,Word=0){return 0;}
 static void __stdcall destroy(Fake*s){++s->destroyed;}
 static Word __stdcall dispatch(Fake*s,Word,Word,Word){++s->dispatches;return 0;}
 static void __stdcall state(Fake*s,IStream* stream,int save){ULONG n=0;uint32_t value=0x12345678;if(save){++s->writes;stream->Write(&value,sizeof value,&n);if(s->demoOnSave)s->engine->info->flags|=1<<15;}else{++s->reads;stream->Read(&value,sizeof value,&n);if(s->demoOnRestore)s->engine->info->flags|=1<<15;}}
 static int __stdcall parameter(Fake*s,int,int v,int){++s->parameters;return v;}
 static void __stdcall render(Fake*s,float*,float*out,int n){++s->renders;std::fill_n(out,n*2,1.f);if(s->demoOnRender)s->engine->info->flags|=1<<15;if(s->productOnRender)Engine::Impl::dispatch(&s->engine->host,0,26,0,0);}
};
struct Fixture {
 Fake fake;Engine engine;PlugInfo info{};int edits=0;
 Fixture(bool demo=false){info.flags=demo?(1<<15):0;info.params=4;engine.p->plug=&fake;engine.p->info=&info;engine.p->ranges.resize(4);fake.engine=engine.p;engine.setParameterEditCallback([](void*p,int,double){++*static_cast<int*>(p);},&edits);}
 ~Fixture(){engine.close();}
 Word host(Word id,Word index=0,Word value=0){return Engine::Impl::dispatch(&engine.p->host,0,id,index,value);}
 void edit(int index){Engine::Impl::paramChanged(&engine.p->host,0,index,MidiMax/2);}
 void silent(){float l[32],r[32];std::fill_n(l,32,9);std::fill_n(r,32,9);engine.render(l,r,32);check(std::all_of(l,l+32,[](float v){return v==0;})&&std::all_of(r,r+32,[](float v){return v==0;}),"blocked block was not silent");}
};
}
int main(){try{
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 std::vector<uint8_t> saved;
 {Fixture f;check(f.engine.ready(),"control ready");check(f.engine.save(saved)&&f.fake.writes==1,"control save");check(f.engine.restore(saved.data(),saved.size())&&f.fake.reads==1,"control restore");f.engine.setParameter(0,.5);f.edit(0);check(f.fake.parameters==1&&f.edits==1,"control automation");float l[8]{},r[8]{};f.engine.render(l,r,8);check(l[0]==1&&r[0]==1&&f.fake.renders==1,"control audio");}
 {Fixture f(true);check(!f.engine.ready(),"demo ready");check(f.engine.p->call<Word>(9,(void*)nullptr,Word(0))==-1,"blocked trigger returned valid voice handle");std::vector<uint8_t> out{42};check(!f.engine.save(out)&&out==std::vector<uint8_t>{42}&&f.fake.writes==0,"demo state leaked");check(!f.engine.restore(saved.data(),saved.size())&&f.fake.reads==0,"demo restore forwarded");f.engine.setParameter(0,.5);f.edit(0);check(f.fake.parameters==0&&f.edits==0&&!f.engine.canAutomate(0),"demo automation");check(!f.engine.loadPreset(0)&&!f.engine.loadFile(L"missing.wav"),"demo load");f.silent();check(f.fake.renders==0,"demo rendered");f.info.flags=0;check(!f.engine.ready(),"demo latch reset by flag clear");f.engine.close();check(f.fake.destroyed==1,"blocked instance not destroyed");check(std::string(f.engine.error()).find("Demo/trial")!=std::string::npos,"failure lost on close");}
 {Fixture f;f.host(8,1,2);check(f.engine.canAutomate(0)&&!f.engine.canAutomate(1)&&!f.engine.canAutomate(2)&&f.engine.canAutomate(3),"inclusive automation range");for(int i=0;i<4;++i){f.engine.setParameter(i,.5);f.edit(i);}check(f.fake.parameters==2&&f.edits==2,"disabled parameters forwarded");check(f.engine.ready(),"automation request blocked audio");f.host(8,3,1);f.host(8,-1,3);check(f.engine.canAutomate(3),"invalid ranges changed policy");f.host(8,3,INTPTR_MAX);check(!f.engine.canAutomate(3),"large interval not clamped");check(!f.engine.canAutomate(-1)&&!f.engine.canAutomate(65536),"invalid index accepted");}
 {Fixture f;f.engine.p->plug=nullptr;f.engine.p->info=nullptr;f.host(8,0,1);uint32_t sentinel=0x13572468;check(f.host(26,123,(Word)&sentinel)==0&&sentinel==0x13572468,"product callback manufactured data");f.engine.p->plug=&f.fake;f.engine.p->info=&f.info;check(!f.engine.ready(),"creation-time product request ignored");check(f.engine.p->automationDisabled.test(0)&&f.engine.p->automationDisabled.test(1),"creation-time automation request lost");f.silent();std::vector<uint8_t> out;check(!f.engine.save(out)&&f.fake.writes==0,"unsupported product check allowed save");check(std::string(f.engine.error()).find("product-check")!=std::string::npos,"product-check reason missing");}
 {Fixture f;f.fake.demoOnSave=true;std::vector<uint8_t> out{42};check(!f.engine.save(out)&&out==std::vector<uint8_t>{42},"demo during save leaked state");check(!f.engine.ready(),"demo during save not sticky");}
 {Fixture f;f.fake.demoOnRestore=true;check(!f.engine.restore(saved.data(),saved.size())&&f.fake.reads==1,"demo during restore accepted");check(f.fake.dispatches==0,"post-restore dispatcher called after demo");f.silent();}
 for(bool product:{false,true}){Fixture f;f.fake.demoOnRender=!product;f.fake.productOnRender=product;f.silent();check(f.fake.renders==1&&!f.engine.ready(),"runtime license signal ignored");f.silent();check(f.fake.renders==1,"blocked render called again");}
 CoUninitialize();std::cout<<"PASS "<<checks<<" license-policy and automation checks (synthetic ABI, not activation validation)\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
