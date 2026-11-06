// SPDX-License-Identifier: MIT
// Headless adapter contract tests with a fake engine: this does not validate FL ABI.
#include "../bridge/vst3.cpp"
#include <cstdlib>

// Keep setup calls and checks active in release builds (Zig defines NDEBUG).
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::fflush(stderr); std::exit(10); } } while (0)

static void (*editCallback)(void*,int,double) = nullptr;
static void* editContext = nullptr;
struct Engine::Impl { ChannelSettings settings{}; bool opened = false; bool note = false; double value[2] = {0.5,0.25}; };
static bool automationAllowed[2]={true,true};
bool Engine::canAutomate(int index) const {return p->opened&&index>=0&&index<2&&automationAllowed[index];}
Engine::Engine() : p(new Impl) {}
Engine::~Engine() { delete p; }
bool Engine::open(const wchar_t*) { p->opened = true; return true; }
bool Engine::openConfigured(const wchar_t* ini) { p->opened=BridgeConfig::load(ini).valid;return p->opened; }
bool Engine::isInstrument() const { return instrument(); }
int Engine::nativeFlags() const { return 0; }
ChannelSettings Engine::channelSettings() const { return p->settings; }
void Engine::setChannelSettings(const ChannelSettings& v) { p->settings=v; }
bool Engine::supportsPortamento() const { return instrument(); }
bool Engine::supportsSampleLoad() const { return instrument(); }
int Engine::latencySamples() const { return 17; }
void Engine::close() { p->opened = false; }
bool Engine::ready() const { return p->opened; }
const char* Engine::error() const { return ""; }
int Engine::parameterCount() const { return 2; }
void Engine::parameterName(int n, char* s, int len) { std::snprintf(s,len,"Parameter %d",n); }
double Engine::getParameter(int n) { return p->value[n]; }
void Engine::setParameter(int n, double v) { p->value[n] = v; }
void Engine::setParameterEditCallback(void (*cb)(void*,int,double), void* ctx) { editCallback=cb;editContext=ctx; }
void Engine::configure(double,int) {}
void Engine::setTransport(double,double,bool) {}
void Engine::noteOn(int,int,int,float) { p->note = true; }
void Engine::noteOff(int,int,int,float) { p->note = false; }
void Engine::allNotesOff() { p->note = false; }
void Engine::render(float* l, float* r, int n,const float* il,const float* ir) {
 for(int i=0;i<n;++i) { if(instrument())l[i]=r[i]=p->note?float(p->value[0]):0;else { l[i]=(il?il[i]:0)*float(p->value[0]);r[i]=(ir?ir[i]:0)*float(p->value[0]); } }
}
bool Engine::save(std::vector<uint8_t>& b) { b.resize(sizeof(p->value)); std::memcpy(b.data(),p->value,b.size()); return true; }
bool Engine::restore(const uint8_t* b,size_t n) { if(n!=sizeof(p->value))return false;std::memcpy(p->value,b,n);return true; }
bool Engine::showEditor(void*) { return true; }
void Engine::hideEditor() {}
void Engine::editorSize(int& w,int& h) { w=400;h=300; }
void Engine::idle() {}
bool Engine::loadSample(const wchar_t*) { return true; }
bool Engine::canLoadFile()const { return instrument(); }
bool Engine::loadFile(const wchar_t*) { return true; }
void* Engine::dialogOwner()const { return nullptr; }
int Engine::presetCount()const { return 0; }
void Engine::presetName(int,char* out,int capacity)const { if(capacity>0)out[0]=0; }
bool Engine::loadPreset(int) { return false; }

#define UNKNOWN_IMPL tresult PLUGIN_API queryInterface(const TUID,void** out) override {if(out)*out=nullptr;return kNoInterface;} uint32 PLUGIN_API addRef() override{return 1;} uint32 PLUGIN_API release() override{return 1;}
struct Handler: IComponentHandler {
    UNKNOWN_IMPL
    int begins=0, edits=0, ends=0, restarts=0;double value=0;
    tresult PLUGIN_API beginEdit(ParamID) override {++begins;return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID,double v) override {++edits;value=v;return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID) override {++ends;return kResultOk;}
    tresult PLUGIN_API restartComponent(int32 flags) override {if(flags&kParamTitlesChanged)++restarts;return kResultOk;}
};
struct Queue: IParamValueQueue {
    UNKNOWN_IMPL
    ParamID id=0; std::vector<std::pair<int,double>> points;
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return int32(points.size()); }
    tresult PLUGIN_API getPoint(int32 i,int32& offset,ParamValue& v) override { if(i<0||i>=int32(points.size()))return kInvalidArgument;offset=points[i].first;v=points[i].second;return kResultOk; }
    tresult PLUGIN_API addPoint(int32 off,ParamValue v,int32& i) override {i=int32(points.size());points.emplace_back(off,v);return kResultOk;}
};
struct Changes: IParameterChanges {
    UNKNOWN_IMPL
    Queue q;
    int32 PLUGIN_API getParameterCount() override { return 1; }
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override { return i==0?&q:nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&,int32& i) override { i=0;return &q; }
};
struct Events: IEventList {
    UNKNOWN_IMPL
    std::vector<Event> es;
    int32 PLUGIN_API getEventCount() override { return int32(es.size()); }
    tresult PLUGIN_API getEvent(int32 i,Event& e) override { if(i<0||i>=int32(es.size()))return kInvalidArgument;e=es[i];return kResultOk; }
    tresult PLUGIN_API addEvent(Event& e) override { es.push_back(e);return kResultOk; }
};
struct Stream: IBStream {
    UNKNOWN_IMPL
    std::vector<uint8_t> b; size_t pos=0;
    tresult PLUGIN_API read(void* dst,int32 n,int32* done) override { n=std::min({n,2,int32(b.size()-pos)}); std::memcpy(dst,b.data()+pos,n);pos+=n;if(done)*done=n;return kResultOk; }
    tresult PLUGIN_API write(void* src,int32 n,int32* done) override { n=std::min(n,3);b.resize(pos+n);std::memcpy(b.data()+pos,src,n);pos+=n;if(done)*done=n;return kResultOk; }
    tresult PLUGIN_API seek(int64 p,int32 mode,int64* result) override { if(mode!=0||p<0||p>int64(b.size()))return kInvalidArgument;pos=size_t(p);if(result)*result=pos;return kResultOk; }
    tresult PLUGIN_API tell(int64* p) override { if(!p)return kInvalidArgument;*p=pos;return kResultOk; }
};
int main(int argc,char** argv) {
    const bool effect=argc>1 && !std::strcmp(argv[1],"effect");
    moduleHandle=GetModuleHandleW(nullptr);
    auto ini=configurationPath();
    auto writeConfig=[&](const char* dll,const char* uid){
      FILE* f=_wfopen(ini.c_str(),L"wb");CHECK(f);
      std::fprintf(f,"[plugin]\nname=Test \xD0\xA1\xD0\xB8\xD0\xBD\xD1\x82\nkind=%s\ndll=%s\nroot=FL\nuid=%s\n[dependencies]\ncount=0\n",effect?"effect":"instrument",dll,uid);std::fclose(f);
    };
    writeConfig("../outside.dll","123456789ABCDEF0123456789ABCDEF0");CHECK(!BridgeConfig::load(ini.c_str()).valid);
    writeConfig("FL/native.dll","123456789ABCDEF0123456789ABCDEFX");CHECK(!BridgeConfig::load(ini.c_str()).valid);
    writeConfig("FL/native.dll",effect?"223456789ABCDEF0123456789ABCDEF0":"123456789ABCDEF0123456789ABCDEF0");
    CHECK(BridgeConfig::load(ini.c_str()).valid);
    auto* factory=GetPluginFactory();CHECK(factory);CHECK(factory->countClasses()==1);
    PClassInfo classInfo{};CHECK(factory->getClassInfo(0,&classInfo)==kResultOk);
    IComponent* c=nullptr;
    CHECK(factory->createInstance(classInfo.cid,IComponent_iid,reinterpret_cast<void**>(&c))==kResultOk);
    CHECK(c->initialize(nullptr)==kResultOk);
    IAudioProcessor* a=nullptr;IEditController* e=nullptr;
    CHECK(c->queryInterface(IAudioProcessor_iid,reinterpret_cast<void**>(&a))==kResultOk);
    CHECK(c->queryInterface(IEditController_iid,reinterpret_cast<void**>(&e))==kResultOk);
    FUnknown *uc=nullptr,*ua=nullptr,*ue=nullptr;
    c->queryInterface(FUnknown_iid,reinterpret_cast<void**>(&uc));a->queryInterface(FUnknown_iid,reinterpret_cast<void**>(&ua));e->queryInterface(FUnknown_iid,reinterpret_cast<void**>(&ue));
    CHECK(uc==ua && uc==ue);uc->release();ua->release();ue->release();
    CHECK(e->getParameterCount()==2);
    ParameterInfo pi{};CHECK(e->getParameterInfo(0,pi)==kResultOk);CHECK(pi.defaultNormalizedValue==0.5);
    CHECK(e->getParameterInfo(2,pi)==kInvalidArgument);
    CHECK(c->getBusCount(kAudio,kOutput)==1 && c->getBusCount(kAudio,kInput)==(effect?1:0));
    CHECK(a->getLatencySamples()==17);
    auto* f3=static_cast<Factory*>(factory);PClassInfo2 ci2{};CHECK(f3->getClassInfo2(0,&ci2)==kResultOk);
    CHECK(std::string(ci2.version)=="0.6.3");
    CHECK(std::string(ci2.subCategories)==(effect?"Fx":"Instrument|Synth"));
    PClassInfoW ciw{};CHECK(f3->getClassInfoUnicode(0,&ciw)==kResultOk);CHECK(ciw.name[5]==0x421);
    CHECK(static_cast<unsigned char>(classInfo.cid[0])==(effect?0x22:0x12));
    SpeakerArrangement stereo=SpeakerArr::kStereo;
    CHECK(a->setBusArrangements(effect?&stereo:nullptr,effect?1:0,&stereo,1)==kResultTrue);
    CHECK(a->setBusArrangements(nullptr,effect?0:1,&stereo,1)==kResultFalse);
    if(effect)CHECK(c->activateBus(kAudio,kInput,0,true)==kResultOk);
    c->activateBus(kAudio,kOutput,0,true);c->activateBus(kEvent,kInput,0,true);
    ProcessSetup setup{};setup.sampleRate=48000;setup.maxSamplesPerBlock=64;setup.symbolicSampleSize=kSample32;
    CHECK(a->setupProcessing(setup)==kResultOk);CHECK(c->setActive(true)==kResultOk);CHECK(a->setProcessing(true)==kResultOk);
    Changes changes;changes.q.points={{8,0.75}};
    Events events;Event on{};on.type=Event::kNoteOnEvent;on.sampleOffset=4;on.noteOn.pitch=60;on.noteOn.noteId=1;on.noteOn.velocity=1;events.es.push_back(on);
    Event off{};off.type=Event::kNoteOffEvent;off.sampleOffset=12;off.noteOff.pitch=60;off.noteOff.noteId=1;events.es.push_back(off);
    float l[64]{},r[64]{};float* channels[]={l,r};
    AudioBusBuffers bus{};bus.numChannels=2;bus.channelBuffers32=channels;
    ProcessData data{};data.symbolicSampleSize=kSample32;data.numSamples=16;data.numOutputs=1;data.outputs=&bus;data.inputEvents=&events;data.inputParameterChanges=&changes;
    if(effect){std::fill_n(l,16,0.8f);std::fill_n(r,16,0.4f);data.numInputs=1;data.inputs=&bus;}
    CHECK(a->process(data)==kResultOk);
    for(int i=0;i<16;++i) {float expected=i<4||i>=12?0:(i<8?0.5f:0.75f);if(effect){expected=i<8?0.5f:0.75f;CHECK(l[i]==0.8f*expected && r[i]==0.4f*expected);}else CHECK(l[i]==expected && r[i]==expected);}
    Stream state;CHECK(c->getState(&state)==kResultOk);CHECK(state.b.size()==12+16+16+4+40);
    changes.q.points={{0,0.125}};data.numSamples=0;data.inputEvents=nullptr;CHECK(a->process(data)==kResultOk);CHECK(e->getParamNormalized(0)==0.125);
    state.pos=0;CHECK(c->setState(&state)==kResultOk);CHECK(e->getParamNormalized(0)==0.75);
    Stream legacy=state;legacy.b[4]=2;legacy.b.resize(12+16+16);legacy.pos=0;CHECK(c->setState(&legacy)==kResultOk);
    static_cast<Plugin*>(c)->setPresetLabel("Filter/Smooth");Stream named;CHECK(c->getState(&named)==kResultOk);static_cast<Plugin*>(c)->setPresetLabel("");named.pos=0;CHECK(c->setState(&named)==kResultOk);CHECK(static_cast<Plugin*>(c)->getPresetLabel()=="Filter/Smooth");
    ChannelSettings channel;channel.maxPolyphony=7;channel.mono=true;channel.portamento=true;channel.slideMs=460;channel.rootNote=48;channel.transpose=-3;channel.fineCents=17;channel.gain=0.4f;channel.pan=-0.25f;
    auto* plugin=static_cast<Plugin*>(c);plugin->engine.setChannelSettings(channel);Stream custom;CHECK(c->getState(&custom)==kResultOk);plugin->engine.setChannelSettings(ChannelSettings{});custom.pos=0;CHECK(c->setState(&custom)==kResultOk);
    auto restored=plugin->engine.channelSettings();CHECK(restored.maxPolyphony==7&&restored.mono&&restored.portamento&&restored.slideMs==460&&restored.rootNote==48&&restored.transpose==-3&&restored.fineCents==17&&restored.gain==0.4f&&restored.pan==-0.25f);
    Stream v3=custom;v3.b.resize(v3.b.size()-40);v3.b[4]=3;v3.pos=0;CHECK(c->setState(&v3)==kResultOk);CHECK(plugin->engine.channelSettings().maxPolyphony==256&&!plugin->engine.channelSettings().mono);
    Stream invalid=custom;invalid.pos=0;uint32 bad=257;memcpy(invalid.b.data()+invalid.b.size()-36,&bad,4);CHECK(c->setState(&invalid)==kResultFalse);CHECK(plugin->engine.channelSettings().maxPolyphony==256);
    invalid=custom;invalid.pos=0;uint32 nan=0x7fc00000;memcpy(invalid.b.data()+invalid.b.size()-8,&nan,4);CHECK(c->setState(&invalid)==kResultFalse);
    for(size_t cut=1;cut<=40;cut++){invalid=custom;invalid.pos=0;invalid.b.resize(invalid.b.size()-cut);CHECK(c->setState(&invalid)==kResultFalse);}
    named.b.pop_back();named.pos=0;CHECK(c->setState(&named)==kResultFalse);
    state.pos=0;state.b[12]^=1;CHECK(c->setState(&state)==kResultFalse);state.b[12]^=1;
    state.pos=0;state.b[0]^=1;CHECK(c->setState(&state)==kResultFalse);
    data.numSamples=65;CHECK(a->process(data)==kInvalidArgument);
    data.numSamples=16;data.inputParameterChanges=nullptr;a->setProcessing(false);std::fill(l,l+16,1);CHECK(a->process(data)==kResultOk);CHECK(bus.silenceFlags==3);for(int i=0;i<16;++i)CHECK(l[i]==0);
    Handler handler;e->setComponentHandler(&handler);
    CHECK(editCallback);editCallback(editContext,0,0.333);
    CHECK(handler.edits==0);CHECK(e->getParamNormalized(0)==0.333);
    static_cast<Plugin*>(c)->editorIdle();
    CHECK(handler.begins==1 && handler.edits==1 && handler.ends==1 && handler.value==0.333);
    static_cast<Plugin*>(c)->editorIdle();CHECK(handler.edits==1);
    if(effect){
       a->setProcessing(true);data.numSamples=16;data.numInputs=0;data.inputs=nullptr;CHECK(a->process(data)==kResultOk);for(int i=0;i<16;i++)CHECK(l[i]==0&&r[i]==0);
       data.numInputs=1;data.inputs=&bus;bus.silenceFlags=1;std::fill_n(l,16,1.f);std::fill_n(r,16,1.f);CHECK(a->process(data)==kResultOk);for(int i=0;i<16;i++)CHECK(l[i]==0&&r[i]==0.75f);
    }
    editCallback(editContext,0,0.4); // queued before the native restriction arrives
    const int priorEdits=handler.edits;
    automationAllowed[0]=false;
    plugin->editorIdle();CHECK(handler.edits==priorEdits&&handler.restarts==1);
    CHECK(e->getParameterInfo(0,pi)==kResultOk&&!(pi.flags&ParameterInfo::kCanAutomate)&&(pi.flags&ParameterInfo::kIsReadOnly));
    CHECK(e->setParamNormalized(0,0.9)==kResultFalse);
    const double oldNative=plugin->engine.getParameter(0),oldCached=e->getParamNormalized(0);
    data.numSamples=0;data.inputParameterChanges=&changes;changes.q.points={{0,0.9}};
    CHECK(a->process(data)==kResultOk);CHECK(plugin->engine.getParameter(0)==oldNative&&e->getParamNormalized(0)==oldCached);
    editCallback(editContext,0,0.8);plugin->editorIdle();CHECK(handler.edits==priorEdits);
    editCallback(editContext,1,0.6);plugin->editorIdle();CHECK(handler.edits==priorEdits+1);
    c->setActive(false);c->terminate();CHECK(!editCallback);e->release();a->release();c->release();factory->release();
    DeleteFileW(ini.c_str());
    std::puts("PASS: configured identity/category/Unicode, effect input alias/silence/latency, cross-plugin state rejection; VST3 factory/QI, buses, sample-offset MIDI/automation, zero-frame flush, partial-stream state roundtrip, invalid state and lifecycle (mock engine only)");
    return 0;
}

void* Engine::nativeModule() const {return nullptr;}
