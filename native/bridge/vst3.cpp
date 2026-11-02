// SPDX-License-Identifier: MIT
// Windows x64 VST3 adapter. The native Image-Line module is loaded at runtime.
// This translation unit uses only the public VST3 interface headers.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <new>
#include <mutex>
#include <string>
#include <vector>
#include "engine.h"
#include "config.h"
#include "file_drop.h"
#include "editor_host_guard.h"
#include "editor_shortcuts.h"
#include "preset_menu.h"
#include "preset.h"
#include "channel_panel.h"
#include "toolbar_button.h"
#include "pluginterfaces/base/keycodes.h"
#include <shlobj.h>
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
HMODULE moduleHandle = nullptr;

constexpr char vendorName[] = "Independent Native Bridge";
constexpr uint32 stateMagic = 0x31424746; // FGB1, little endian
constexpr uint32 stateVersion = 4;
constexpr int presetBarHeight = 28;
constexpr UINT presetPrevious = 0x681, presetChoose = 0x682, presetNext = 0x683, channelChoose = 0x684;
constexpr uint32 maxStateSize = 256u * 1024u * 1024u;
constexpr UINT loadDroppedSampleMessage = WM_APP + 0x471;
constexpr wchar_t windowClassName[] = L"FLNativeVST3BridgeWindow";

bool sameId(const char* a, const char* b) { return a && b && std::memcmp(a, b, 16) == 0; }
double normalized(double v) { return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0; }
void utf16(TChar* out, size_t capacity, const char* text) {
    if (!out || !capacity) return;
    out[0] = 0;
    if (!text) return;
    std::wstring converted = BridgeConfig::wide(text);
    const auto size = std::min(capacity - 1, converted.size());
    for (size_t i = 0; i < size; ++i) out[i] = static_cast<TChar>(converted[i]);
    out[size] = 0;
}
template<size_t N> void ascii(char (&dst)[N], const char* src) {
    std::snprintf(dst, N, "%s", src ? src : "");
}
std::wstring configurationPath() {
    wchar_t path[32768]{};
    DWORD n = GetModuleFileNameW(moduleHandle, path, static_cast<DWORD>(std::size(path)));
    if (!n || n >= std::size(path)) return {};
    std::wstring result(path, n);
    auto slash = result.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    result.resize(slash + 1);
    return result + L"bridge.ini";
}
const BridgeConfig& configuration() {
    static const BridgeConfig value = BridgeConfig::load(configurationPath().c_str());
    return value;
}
const char* componentId() { return configuration().uid.data(); }
const char* pluginName() { return configuration().nameUtf8.c_str(); }
bool instrument() { return configuration().instrument; }
bool readExact(IBStream* stream, void* data, uint32 size) {
    auto* ptr = static_cast<uint8_t*>(data);
    while (size) {
        int32 done = 0;
        const auto result = stream->read(ptr, static_cast<int32>(size), &done);
        if (done <= 0 || static_cast<uint32>(done) > size) return false;
        ptr += done;
        size -= done;
        if (result != kResultOk && size) return false;
    }
    return true;
}
bool writeExact(IBStream* stream, const void* data, uint32 size) {
    auto* ptr = static_cast<const uint8_t*>(data);
    while (size) {
        int32 done = 0;
        const auto result = stream->write(const_cast<uint8_t*>(ptr), static_cast<int32>(size), &done);
        if (done <= 0 || static_cast<uint32>(done) > size) return false;
        ptr += done;
        size -= done;
        if (result != kResultOk && size) return false;
    }
    return true;
}

class Plugin;
class Editor final : public IPlugView {
public:
    explicit Editor(Plugin* owner);
    ~Editor();
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override;
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { auto n = --refs; if (!n) delete this; return n; }
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override {
        return type && !std::strcmp(type, kPlatformTypeHWND) ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void* parent, FIDString type) override;
    tresult PLUGIN_API removed() override;
    tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16 keyCode, int16) override;
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API getSize(ViewRect* size) override;
    tresult PLUGIN_API onSize(ViewRect* size) override;
    tresult PLUGIN_API onFocus(TBool state) override;
    tresult PLUGIN_API setFrame(IPlugFrame* value) override;
    tresult PLUGIN_API canResize() override { return kResultFalse; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* rect) override;
private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
    void loadSamplePath(const wchar_t* path);
    static bool queueDrop(void* context, const std::wstring& path);
    FileDropTarget* dropTarget = nullptr;
    EditorHostGuard hostGuard;
    EditorShortcuts shortcuts;
    bool presetMenuOpen=false;
    static void showPresetMenu(void*,POINT);
    void selectPreset(int index);
    void updatePresetTitle();
    int presetHeight() const;
    HWND previousButton=nullptr, presetButton=nullptr, nextButton=nullptr, channelButton=nullptr;
    std::unique_ptr<ChannelPanel> channelPanel;
    bool channelVisible=false;
    void toggleChannel();
    void desiredSize(int& w,int& h);
    HFONT presetFont=nullptr;
    std::string shownPreset;
    struct PopupDrop { HWND window; FileDropTarget* target; };
    std::vector<PopupDrop> popupDrops;
    void refreshPopupDrops();
    static bool forwardPopupDrop(void*, const std::wstring&);
    std::wstring droppedPath;
    unsigned dropRefreshTicks = 0;
    bool stoppedNotice = false;
    void tick();
    std::atomic<uint32> refs{1};
    Plugin* owner;
    IPlugFrame* frame = nullptr;
    HWND window = nullptr;
    HWND panel = nullptr;
    int width = 640;
    int height = 400;
};

class Plugin final : public IComponent, public IAudioProcessor, public IEditController,
                     public IProcessContextRequirements {
public:
    Plugin() = default;
    ~Plugin() {
        engine.setParameterEditCallback(nullptr, nullptr);
        if (handler) handler->release();
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (sameId(iid, FUnknown_iid) || sameId(iid, IPluginBase_iid) || sameId(iid, IComponent_iid))
            *obj = static_cast<IComponent*>(this);
        else if (sameId(iid, IAudioProcessor_iid)) *obj = static_cast<IAudioProcessor*>(this);
        else if (sameId(iid, IEditController_iid)) *obj = static_cast<IEditController*>(this);
        else if (sameId(iid, IProcessContextRequirements_iid)) *obj = static_cast<IProcessContextRequirements*>(this);
        if (!*obj) return kNoInterface;
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { auto n = --refs; if (!n) delete this; return n; }
    tresult PLUGIN_API initialize(FUnknown*) override {
        if (initialized) return kResultFalse;
        try {
            const auto& config = configuration();
            if (!config.valid || !engine.openConfigured(config.iniPath.c_str())) return kResultFalse;
            if (engine.isInstrument() != config.instrument) { engine.close(); return kResultFalse; }
            count = engine.parameterCount();
            if (count < 0 || count > 65536) { engine.close(); return kResultFalse; }
            values = std::make_unique<std::atomic<double>[]>(count);
            editValues = std::make_unique<std::atomic<double>[]>(count);
            pendingEdits = std::make_unique<std::atomic<bool>[]>(count);
            defaults.resize(count);
            automationInfo.resize(count);
            cursors.resize(count);
            for (int i = 0; i < count; ++i) {
                defaults[i] = normalized(engine.getParameter(i));
                automationInfo[i] = engine.canAutomate(i);
                values[i].store(defaults[i]);
                editValues[i].store(defaults[i]);
                pendingEdits[i].store(false);
            }
            engine.configure(sampleRate, maxBlock);
            if (!engine.ready()) { engine.close(); count = 0; return kResultFalse; }
            scratchLeft.resize(maxBlock);
            scratchRight.resize(maxBlock);
            inputLeft.resize(maxBlock); inputRight.resize(maxBlock);
            initialized = true;
            engine.setParameterEditCallback(nativeParameterEdited, this);
            return kResultOk;
        } catch (...) {
            engine.close(); count = 0; return kOutOfMemory;
        }
    }
    tresult PLUGIN_API terminate() override {
        if (!initialized) return kResultFalse;
        engine.setParameterEditCallback(nullptr, nullptr);
        engine.allNotesOff();
        engine.hideEditor();
        engine.close();
        initialized = false;
        active = false;
        processing = false;
        setComponentHandler(nullptr);
        return kResultOk;
    }
    tresult PLUGIN_API getControllerClassId(TUID) override { return kResultFalse; }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) override {
        return (type == kAudio && (dir == kOutput || (dir == kInput && !instrument()))) || (type == kEvent && dir == kInput) ? 1 : 0;
    }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo& bus) override {
        if (index != 0 || getBusCount(type, dir) == 0) return kInvalidArgument;
        bus = {};
        bus.mediaType = type; bus.direction = dir; bus.channelCount = type == kAudio ? 2 : 16;
        bus.busType = kMain; bus.flags = BusInfo::kDefaultActive;
        utf16(bus.name, 128, type == kAudio ? (dir == kInput ? "Stereo Input" : "Stereo Output") : "Note Input");
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo& in, RoutingInfo& out) override {
        if (in.busIndex != 0 || (in.mediaType != kEvent && (in.mediaType != kAudio || instrument()))) return kResultFalse;
        out = { kAudio, 0, -1 }; return kResultOk;
    }
    tresult PLUGIN_API activateBus(MediaType type, BusDirection dir, int32 index, TBool state) override {
        if (index != 0 || !getBusCount(type, dir)) return kInvalidArgument;
        if (type == kAudio) { if (dir == kOutput) audioActive = state != 0; else inputActive = state != 0; } else eventActive = state != 0;
        return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override {
        if (!initialized) return kNotInitialized;
        if (!state) engine.allNotesOff();
        active = state != 0;
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* stream) override {
        if (!stream || !initialized) return kInvalidArgument;
        uint32 header[3]{};
        if (!readExact(stream, header, sizeof(header)) || header[0] != stateMagic ||
            (header[1] < 2 || header[1] > stateVersion) || header[2] > maxStateSize) return kResultFalse;
        char savedId[16]{};
        if (!readExact(stream, savedId, sizeof(savedId)) || !sameId(savedId, componentId())) return kResultFalse;
        try {
            std::vector<uint8_t> data(header[2]);
            if (!readExact(stream, data.data(), header[2])) return kResultFalse;
            std::string label;
            if(header[1]>=3){
                uint32 size=0;if(!readExact(stream,&size,sizeof(size))||size>2048)return kResultFalse;
                label.resize(size);if(!readExact(stream,label.data(),size)||!FST::validUtf8(label))return kResultFalse;
            }
            ChannelSettings channel;
            if(header[1]>=4){
                uint32 wire[10]{};if(!readExact(stream,wire,sizeof(wire))||wire[0]!=0x314e4843||wire[2]>1||wire[3]>1)return kResultFalse;
                channel.maxPolyphony=int32(wire[1]);channel.mono=wire[2]!=0;channel.portamento=wire[3]!=0;
                channel.slideMs=int32(wire[4]);channel.rootNote=int32(wire[5]);channel.transpose=int32(wire[6]);channel.fineCents=int32(wire[7]);
                std::memcpy(&channel.gain,&wire[8],4);std::memcpy(&channel.pan,&wire[9],4);
                if(channel.maxPolyphony<1||channel.maxPolyphony>256||channel.slideMs<0||channel.slideMs>5000||channel.rootNote<0||channel.rootNote>127||channel.transpose<-48||channel.transpose>48||channel.fineCents<-100||channel.fineCents>100||!std::isfinite(channel.gain)||channel.gain<0||channel.gain>2||!std::isfinite(channel.pan)||channel.pan<-1||channel.pan>1)return kResultFalse;
            }
            if (!engine.restore(data.data(), data.size())) return kResultFalse;
            engine.setChannelSettings(channel);
            setPresetLabel(label);
            syncValues();
            return kResultOk;
        } catch (...) { return kOutOfMemory; }
    }
    tresult PLUGIN_API getState(IBStream* stream) override {
        if (!stream || !initialized) return kInvalidArgument;
        try {
            std::vector<uint8_t> data;
            if (!engine.save(data) || data.size() > maxStateSize) return kResultFalse;
            uint32 header[] = {stateMagic, stateVersion, static_cast<uint32>(data.size())};
            const auto presetLabel=getPresetLabel();
            uint32 labelSize=static_cast<uint32>(presetLabel.size());
            const auto channel=engine.channelSettings();
            uint32 wire[10]={0x314e4843,uint32(channel.maxPolyphony),uint32(channel.mono),uint32(channel.portamento),uint32(channel.slideMs),uint32(channel.rootNote),uint32(channel.transpose),uint32(channel.fineCents),0,0};
            std::memcpy(&wire[8],&channel.gain,4);std::memcpy(&wire[9],&channel.pan,4);
            return writeExact(stream, header, sizeof(header)) && writeExact(stream, componentId(), 16) && writeExact(stream, data.data(), header[2]) && writeExact(stream,&labelSize,sizeof(labelSize)) && writeExact(stream,presetLabel.data(),labelSize) && writeExact(stream,wire,sizeof(wire)) ? kResultOk : kResultFalse;
        } catch (...) { return kOutOfMemory; }
    }
    // A single component shares controller and processor state; no duplicate restoration.
    tresult PLUGIN_API setComponentState(IBStream*) override {
        if (!initialized) return kNotInitialized;
        syncValues(); return kResultOk;
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* input, int32 inputs, SpeakerArrangement* outputs, int32 outputsCount) override {
        const bool validInput = instrument() ? inputs == 0 : inputs == 1 && input && input[0] == SpeakerArr::kStereo;
        return validInput && outputsCount == 1 && outputs && outputs[0] == SpeakerArr::kStereo ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection dir, int32 index, SpeakerArrangement& result) override {
        if (index || (dir != kOutput && (dir != kInput || instrument()))) return kInvalidArgument;
        result = SpeakerArr::kStereo; return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return static_cast<uint32>(std::max(0, engine.latencySamples())); }
    uint32 PLUGIN_API getTailSamples() override { return kInfiniteTail; }
    uint32 PLUGIN_API getProcessContextRequirements() override {
        return kNeedTempo | kNeedProjectTimeMusic | kNeedTransportState;
    }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        if (!initialized || setup.symbolicSampleSize != kSample32 || !std::isfinite(setup.sampleRate) ||
            setup.sampleRate < 8000 || setup.sampleRate > 384000 || setup.maxSamplesPerBlock < 1 ||
            setup.maxSamplesPerBlock > 1048576) return kInvalidArgument;
        try {
            scratchLeft.resize(setup.maxSamplesPerBlock);
            scratchRight.resize(setup.maxSamplesPerBlock);
            inputLeft.resize(setup.maxSamplesPerBlock); inputRight.resize(setup.maxSamplesPerBlock);
            sampleRate = setup.sampleRate; maxBlock = setup.maxSamplesPerBlock;
            engine.configure(sampleRate, maxBlock);
            return kResultOk;
        } catch (...) { return kOutOfMemory; }
    }
    tresult PLUGIN_API setProcessing(TBool state) override {
        if (!state) engine.allNotesOff();
        processing = state != 0; return kResultOk;
    }
    tresult PLUGIN_API process(ProcessData& data) override;
    int32 PLUGIN_API getParameterCount() override { return count; }
    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo& info) override {
        if (index < 0 || index >= count) return kInvalidArgument;
        info = {};
        info.id = static_cast<ParamID>(index); info.unitId = 0;
        info.flags = engine.canAutomate(index) ? ParameterInfo::kCanAutomate : ParameterInfo::kIsReadOnly; info.stepCount = 0;
        info.defaultNormalizedValue = defaults[index];
        char name[128]{};
        engine.parameterName(index, name, sizeof(name));
        if (!name[0]) std::snprintf(name, sizeof(name), "Parameter %d", index + 1);
        utf16(info.title, 128, name); utf16(info.shortTitle, 128, name);
        utf16(info.units, 128, "%");
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue value, String128 text) override {
        if (!valid(id)) return kInvalidArgument;
        char s[64]{}; std::snprintf(s, sizeof(s), "%.3f", normalized(value) * 100.0);
        utf16(text, 128, s); return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID id, TChar* text, ParamValue& value) override {
        if (!valid(id) || !text) return kInvalidArgument;
        wchar_t wide[128]{};
        size_t i = 0; for (; i < 127 && text[i]; ++i) wide[i] = static_cast<wchar_t>(text[i]);
        wchar_t* end = nullptr;
        double parsed = std::wcstod(wide, &end);
        if (end == wide || !std::isfinite(parsed)) return kResultFalse;
        while (*end == L' ') ++end;
        if (*end == L'%') ++end;
        while (*end == L' ') ++end;
        if (*end) return kResultFalse;
        value = normalized(parsed / 100.0); return kResultOk;
    }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID id, ParamValue value) override { return valid(id) ? normalized(value) * 100.0 : 0.0; }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID id, ParamValue value) override { return valid(id) ? normalized(value / 100.0) : 0.0; }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override { return valid(id) ? values[id].load(std::memory_order_relaxed) : 0.0; }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override {
        if (!valid(id)) return kInvalidArgument;
        if (!engine.canAutomate(static_cast<int>(id))) return kResultFalse;
        // A controller notification only updates the controller cache. Processor changes
        // arrive in process(inputParameterChanges), including while stopped (zero samples).
        values[id].store(normalized(value), std::memory_order_relaxed);
        return kResultOk;
    }
    tresult PLUGIN_API setComponentHandler(IComponentHandler* value) override {
        if (value) value->addRef();
        if (handler) handler->release();
        handler = value; return kResultOk;
    }
    IPlugView* PLUGIN_API createView(FIDString name) override {
        return initialized && name && !std::strcmp(name, ViewType::kEditor) ? new (std::nothrow) Editor(this) : nullptr;
    }
    void syncValues() {
        for (int i = 0; i < count; ++i) values[i].store(normalized(engine.getParameter(i)), std::memory_order_relaxed);
    }
    void editorIdle() {
        if (!initialized) return;
        engine.idle();
        // Native OnParamChanged notifications are queued separately from host automation.
        // Dispatch host callbacks on the editor thread, never under the engine's lock.
        bool infoChanged = false;
        for (int i = 0; i < count; ++i) {
            const bool allowed = engine.canAutomate(i);
            if (automationInfo[i] != allowed) { automationInfo[i] = allowed; infoChanged = true; }
            if (!pendingEdits[i].exchange(false, std::memory_order_acquire)) continue;
            if (!engine.canAutomate(i)) continue;
            const double value = editValues[i].load(std::memory_order_relaxed);
            if (handler) {
                handler->beginEdit(static_cast<ParamID>(i));
                handler->performEdit(static_cast<ParamID>(i), value);
                handler->endEdit(static_cast<ParamID>(i));
            }
        }
        if (infoChanged && handler) handler->restartComponent(kParamTitlesChanged);
    }
    void sampleChanged() {
        syncValues();
        if (!handler) return;
        handler->restartComponent(kParamValuesChanged);
        IComponentHandler2* handler2 = nullptr;
        if (handler->queryInterface(IComponentHandler2_iid, reinterpret_cast<void**>(&handler2)) == kResultOk && handler2) {
            handler2->setDirty(true); handler2->release();
        }
    }
    Engine engine;
    std::string getPresetLabel(){std::lock_guard<std::mutex> guard(presetMutex);return presetLabel;}
    void setPresetLabel(const std::string& value){std::lock_guard<std::mutex> guard(presetMutex);presetLabel=value;}
    bool editorAttached = false;
private:
    std::mutex presetMutex;
    std::string presetLabel;
    static void nativeParameterEdited(void* context, int index, double value) {
        auto* self = static_cast<Plugin*>(context);
        if (!self || index < 0 || index >= self->count || !self->engine.canAutomate(index)) return;
        value = normalized(value);
        self->values[index].store(value, std::memory_order_relaxed);
        self->editValues[index].store(value, std::memory_order_relaxed);
        self->pendingEdits[index].store(true, std::memory_order_release);
    }
    bool valid(ParamID id) const { return id < static_cast<ParamID>(count); }
    void applyParameter(ParamID id, double value) {
        if (!valid(id) || !engine.canAutomate(static_cast<int>(id))) return;
        value = normalized(value);
        engine.setParameter(static_cast<int>(id), value);
        if (!engine.canAutomate(static_cast<int>(id))) return;
        values[id].store(value, std::memory_order_relaxed);
    }
    void applyEvent(const Event& event) {
        if (event.busIndex != 0 || !eventActive) return;
        if (event.type == Event::kNoteOnEvent) {
            const auto& n = event.noteOn;
            if (n.velocity <= 0) engine.noteOff(n.noteId, n.pitch, n.channel, 0);
            else engine.noteOn(n.noteId, n.pitch, n.channel, n.velocity);
        } else if (event.type == Event::kNoteOffEvent) {
            const auto& n = event.noteOff; engine.noteOff(n.noteId, n.pitch, n.channel, n.velocity);
        }
    }
    std::atomic<uint32> refs{1};
    IComponentHandler* handler = nullptr;
    std::unique_ptr<std::atomic<double>[]> values;
    std::unique_ptr<std::atomic<double>[]> editValues;
    std::unique_ptr<std::atomic<bool>[]> pendingEdits;
    std::vector<double> defaults;
    std::vector<bool> automationInfo;
    std::vector<int32> cursors;
    std::vector<float> scratchLeft, scratchRight, inputLeft, inputRight;
    int count = 0;
    bool initialized = false, active = false, processing = false;
    bool audioActive = false, inputActive = false, eventActive = false;
    double sampleRate = 44100.0, tempo = 120.0, ppq = 0.0;
    bool playing = false;
    int maxBlock = 1024;
};

tresult Plugin::process(ProcessData& data) {
    if (!initialized || data.symbolicSampleSize != kSample32 || data.numSamples < 0 || data.numSamples > maxBlock) return kInvalidArgument;
    if (data.processContext) {
        const auto& c = *data.processContext;
        if ((c.state & ProcessContext::kTempoValid) && c.tempo > 0 && std::isfinite(c.tempo)) tempo = c.tempo;
        if ((c.state & ProcessContext::kProjectTimeMusicValid) && std::isfinite(c.projectTimeMusic)) ppq = c.projectTimeMusic;
        playing = (c.state & ProcessContext::kPlaying) != 0;
    }
    // Hosts may alias input and output. Keep the entire input block before rendering.
    if (!instrument() && data.numSamples) {
        std::fill_n(inputLeft.data(), data.numSamples, 0.0f);
        std::fill_n(inputRight.data(), data.numSamples, 0.0f);
        if (inputActive && data.numInputs > 0 && data.inputs) {
            const auto& input = data.inputs[0];
            if (input.numChannels != 2 || !input.channelBuffers32 || !input.channelBuffers32[0] || !input.channelBuffers32[1]) return kInvalidArgument;
            if (!(input.silenceFlags & 1)) std::copy_n(input.channelBuffers32[0], data.numSamples, inputLeft.data());
            if (!(input.silenceFlags & 2)) std::copy_n(input.channelBuffers32[1], data.numSamples, inputRight.data());
        }
    }
    float* left = scratchLeft.data();
    float* right = scratchRight.data();
    AudioBusBuffers* output = nullptr;
    if (data.numOutputs > 0 && data.outputs) {
        output = &data.outputs[0];
        if (output->numChannels != 2 || !output->channelBuffers32 || !output->channelBuffers32[0] || !output->channelBuffers32[1]) {
            if (data.numSamples) return kInvalidArgument;
        } else {
            left = output->channelBuffers32[0]; right = output->channelBuffers32[1];
        }
        output->silenceFlags = 0;
    }
    std::fill(cursors.begin(), cursors.end(), 0);
    const int queues = data.inputParameterChanges ? data.inputParameterChanges->getParameterCount() : 0;
    const int events = data.inputEvents ? data.inputEvents->getEventCount() : 0;
    int eventIndex = 0;
    int position = 0;
    const int frames = data.numSamples;
    for (;;) {
        int next = frames;
        // No allocation in process. Each parameter's queue cursor is persistent storage.
        for (int q = 0; q < queues; ++q) {
            auto* queue = data.inputParameterChanges->getParameterData(q);
            if (!queue) continue;
            ParamID id = queue->getParameterId();
            if (!valid(id)) continue;
            int32& cursor = cursors[id];
            const int points = queue->getPointCount();
            while (cursor < points) {
                int32 offset = 0; double value = 0;
                if (queue->getPoint(cursor, offset, value) != kResultOk) { ++cursor; continue; }
                offset = std::clamp(offset, 0, frames);
                if (offset > position) { next = std::min(next, offset); break; }
                applyParameter(id, value); ++cursor;
            }
        }
        while (eventIndex < events) {
            Event event{};
            if (data.inputEvents->getEvent(eventIndex, event) != kResultOk) { ++eventIndex; continue; }
            const int offset = std::clamp(event.sampleOffset, 0, frames);
            if (offset > position) { next = std::min(next, offset); break; }
            applyEvent(event); ++eventIndex;
        }
        if (position == frames) break;
        if (next <= position) next = position + 1;
        engine.setTransport(tempo, ppq + (playing ? position * tempo / (60.0 * sampleRate) : 0), playing);
        if (active && processing) engine.render(left + position, right + position, next - position,
            instrument() ? nullptr : inputLeft.data() + position, instrument() ? nullptr : inputRight.data() + position);
        else {
            std::fill(left + position, left + next, 0.0f);
            std::fill(right + position, right + next, 0.0f);
        }
        position = next;
    }
    if (!audioActive || !active || !processing) {
        std::fill(left, left + frames, 0.0f); std::fill(right, right + frames, 0.0f);
        if (output) output->silenceFlags = 3;
    }
    if (playing) ppq += frames * tempo / (60.0 * sampleRate);
    return kResultOk;
}

Editor::Editor(Plugin* plugin) : owner(plugin) {
    owner->addRef();
    owner->engine.editorSize(width, height);
    width = std::max(width, 1); height = std::max(height, 1)+presetHeight();
}
int Editor::presetHeight()const{return owner->engine.presetCount()>0||owner->engine.isInstrument()?presetBarHeight:0;}
void Editor::desiredSize(int&w,int&h){
    owner->engine.editorSize(w,h);
    w=std::max(w,1);h=std::max(h,1);
    if(channelVisible){w=std::max(w,ChannelPanel::minWidth);h=std::max(h,ChannelPanel::minHeight);}
    h+=presetHeight();
}
void Editor::toggleChannel(){
    if(!owner->engine.isInstrument()||!window)return;
    if(!channelPanel){
        channelPanel=std::make_unique<ChannelPanel>();
        if(!channelPanel->create(window,&owner->engine,this,[](void*p){static_cast<Editor*>(p)->owner->sampleChanged();})){channelPanel.reset();return;}
    }
    channelVisible=!channelVisible;
    if(channelVisible)channelPanel->refresh(true);
    ShowWindow(panel,channelVisible?SW_HIDE:SW_SHOW);ShowWindow(channelPanel->handle(),channelVisible?SW_SHOW:SW_HIDE);
    SetWindowTextW(channelButton,channelVisible?L"◀ Synth":L"Channel");
    int w,h;desiredSize(w,h);ViewRect rect(0,0,w,h);onSize(&rect);if(frame)frame->resizeView(this,&rect);
}
Editor::~Editor() { removed(); setFrame(nullptr); owner->release(); }
tresult Editor::queryInterface(const TUID iid, void** obj) {
    if (!obj) return kInvalidArgument;
    *obj = nullptr;
    if (!sameId(iid, FUnknown_iid) && !sameId(iid, IPlugView_iid)) return kNoInterface;
    *obj = static_cast<IPlugView*>(this); addRef(); return kResultOk;
}
tresult Editor::attached(void* parent, FIDString type) {
    if (!parent || isPlatformTypeSupported(type) != kResultTrue || window || owner->editorAttached) return kResultFalse;
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc; wc.hInstance = moduleHandle;
    wc.lpszClassName = windowClassName; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return kResultFalse;
    window = CreateWindowExW(0, windowClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, width, height, static_cast<HWND>(parent), nullptr, moduleHandle, this);
    if (!window) return kResultFalse;
    // Presets and instrument channel settings share a compact dark header.
    // Native client size is preserved; there is no white frame or minimum width.
    panel = CreateWindowExW(0, windowClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, presetHeight(), width, height-presetHeight(), window, nullptr, moduleHandle, nullptr);
    if (!panel || !owner->engine.showEditor(panel)) { removed(); return kResultFalse; }
    if(presetHeight()){
        presetFont=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        auto button=[&](UINT id,const wchar_t*title)->HWND{return CreateWindowExW(0,L"BUTTON",title,WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,1,presetBarHeight,window,reinterpret_cast<HMENU>(UINT_PTR(id)),moduleHandle,nullptr);};
        if(owner->engine.presetCount()>0){
            previousButton=button(presetPrevious,L"‹");presetButton=button(presetChoose,L"Presets ▾");nextButton=button(presetNext,L"›");
            if(!previousButton||!presetButton||!nextButton){removed();return kResultFalse;}
            updatePresetTitle();
        }
        if(owner->engine.isInstrument()){channelButton=button(channelChoose,L"Channel");if(!channelButton){removed();return kResultFalse;}}
        for(HWND b:{previousButton,presetButton,nextButton,channelButton})if(b)configureToolbarButton(b);
    }
    owner->editorAttached = true;
    hostGuard.attach(static_cast<HWND>(parent), window, GetWindow(panel,GW_CHILD),
        static_cast<HWND>(owner->engine.dialogOwner()), static_cast<HMODULE>(owner->engine.nativeModule()));
    if (owner->engine.canLoadFile()) {
        // FL-host sample controls (including Granulizer) need the bridge target.
        // Self-contained samplers keep their own native OLE/legacy drop handlers.
        dropTarget = new (std::nothrow) FileDropTarget(this, queueDrop,
            owner->engine.supportsSampleLoad(), !owner->engine.supportsSampleLoad());
    }
    if (dropTarget) dropTarget->attach(window);
    if(owner->engine.presetCount()>0)shortcuts.attach(window,this,showPresetMenu);
    SetTimer(window, 1, 33, nullptr);
    int nativeWidth = 0, nativeHeight = 0;
    owner->engine.editorSize(nativeWidth, nativeHeight);
    ViewRect desired(0, 0, std::max(nativeWidth, 1), std::max(nativeHeight, 1)+presetHeight());
    onSize(&desired);
    if (frame) frame->resizeView(this, &desired);
    tick();
    return kResultOk;
}
tresult Editor::removed() {
    shortcuts.detach();
    hostGuard.detach();
    for(auto& entry:popupDrops){entry.target->detach();entry.target->Release();}
    popupDrops.clear();
    if (dropTarget) { dropTarget->detach(); dropTarget->Release(); dropTarget = nullptr; }
    droppedPath.clear();
    if (window) {
        KillTimer(window, 1);
        // Only this view owns the native editor when attached. Failure cleanup also
        // hides a partially created native window before destroying its parent.
        owner->engine.hideEditor();
        owner->editorAttached = false;
        HWND old = window; window = nullptr; panel = nullptr;
        DestroyWindow(old);
    }
    channelPanel.reset();channelVisible=false;
    previousButton=presetButton=nextButton=channelButton=nullptr;
    if(presetFont){DeleteObject(presetFont);presetFont=nullptr;}
    shownPreset.clear();
    return kResultOk;
}
tresult Editor::getSize(ViewRect* rect) {
    if (!rect) return kInvalidArgument;
    *rect = ViewRect(0, 0, width, height); return kResultOk;
}
tresult Editor::onSize(ViewRect* rect) {
    if (!rect || rect->getWidth() <= 0 || rect->getHeight() <= 0) return kInvalidArgument;
    width = rect->getWidth(); height = rect->getHeight();
    if (window) MoveWindow(window, 0, 0, width, height, TRUE);
    const int top=presetHeight();
    if (panel) MoveWindow(panel, 0, top, width, std::max(1,height-top), TRUE);
    const int channelWidth=channelButton?std::min(90,width/2):0;
    const int presetWidth=width-channelWidth;
    if(presetButton){const int arrow=std::min(28,std::max(1,presetWidth/4));MoveWindow(previousButton,0,0,arrow,top,TRUE);MoveWindow(presetButton,arrow,0,std::max(1,presetWidth-2*arrow),top,TRUE);MoveWindow(nextButton,presetWidth-arrow,0,arrow,top,TRUE);}
    if(channelButton)MoveWindow(channelButton,presetWidth,0,channelWidth,top,TRUE);
    if(channelPanel&&channelPanel->handle())MoveWindow(channelPanel->handle(),0,top,width,std::max(1,height-top),TRUE);
    return kResultOk;
}
tresult Editor::onKeyDown(char16,int16 keyCode,int16 modifiers) {
    if(modifiers||keyCode!=KEY_F4||!window||owner->engine.presetCount()<=0)return kResultFalse;
    POINT point{12,12};ClientToScreen(window,&point);showPresetMenu(this,point);return kResultTrue;
}
void Editor::showPresetMenu(void* context,POINT point) {
    auto&self=*static_cast<Editor*>(context);if(self.presetMenuOpen||!self.window)return;
    const int count=std::min(self.owner->engine.presetCount(),4096);if(count<=0)return;
    PresetMenu menu;if(!menu.handle())return;
    for(int i=0;i<count;++i){char text[2048]{};self.owner->engine.presetName(i,text,sizeof(text));auto label=BridgeConfig::wide(text);if(label.empty())label=L"Preset "+std::to_wstring(i+1);menu.add(unsigned(i+1),label,self.owner->getPresetLabel()==text);}
    self.presetMenuOpen=true;
    UINT choice=TrackPopupMenu(menu.handle(),TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,0,self.window,nullptr);
    self.presetMenuOpen=false;
    if(choice>0&&choice<=UINT(count)){
        self.selectPreset(int(choice-1));
    }
}
void Editor::selectPreset(int index){
    if(owner->engine.loadPreset(index)){
        char label[2048]{};owner->engine.presetName(index,label,sizeof(label));owner->setPresetLabel(label);
        owner->sampleChanged();updatePresetTitle();
    }else{
        auto message=L"The original plugin could not load this preset.\n"+BridgeConfig::wide(owner->engine.error());
        MessageBoxW(window,message.c_str(),configuration().name.c_str(),MB_OK|MB_ICONERROR);
    }
}
void Editor::updatePresetTitle(){
    if(!presetButton)return;
    shownPreset=owner->getPresetLabel();
    auto title=shownPreset.empty()?std::wstring(L"Presets ▾"):BridgeConfig::wide(shownPreset)+L" ▾";
    SetWindowTextW(presetButton,title.c_str());InvalidateRect(presetButton,nullptr,TRUE);
}
tresult Editor::onFocus(TBool state) { HWND target=channelVisible&&channelPanel?channelPanel->handle():panel; if(state&&target&&GetFocus()!=target&&!IsChild(target,GetFocus()))SetFocus(target);return kResultOk; }
tresult Editor::setFrame(IPlugFrame* value) {
    if (value) value->addRef();
    if (frame) frame->release();
    frame = value; return kResultOk;
}
tresult Editor::checkSizeConstraint(ViewRect* rect) {
    if (!rect) return kInvalidArgument;
    rect->right = rect->left + width; rect->bottom = rect->top + height; return kResultOk;
}
void Editor::tick() {
    owner->editorIdle();
    if (!owner->engine.ready()) {
        if (!stoppedNotice) {
            stoppedNotice = true;
            ShowWindow(panel, SW_HIDE);
            auto text = BridgeConfig::wide(owner->engine.error());
            CreateWindowExW(0,L"STATIC",text.c_str(),WS_CHILD|WS_VISIBLE,16,48,std::max(100,width-32),160,window,nullptr,moduleHandle,nullptr);
        }
        return;
    }
    if(shownPreset!=owner->getPresetLabel())updatePresetTitle();
    if(channelVisible&&channelPanel)channelPanel->refresh();
    hostGuard.refresh();
    if (++dropRefreshTicks % 10 == 0) {
        if (dropTarget) dropTarget->refresh(window);
        refreshPopupDrops();
        shortcuts.refresh(window);
    }
    int w = 0, h = 0;
    desiredSize(w,h);
    if (w != width || h != height) {
        ViewRect desired(0, 0, w, h);
        onSize(&desired);
        if (frame) frame->resizeView(this, &desired);
    }
}
// Secondary dialogs receive their own native WM_DROPFILES, never the main
// sample loader: dropping a convolution impulse must not replace Edison's audio.
bool Editor::forwardPopupDrop(void* context,const std::wstring& path) {
    return FileDropTarget::postLegacyDrop(context,path);
}
void Editor::refreshPopupDrops() {
    for(auto it=popupDrops.begin();it!=popupDrops.end();) {
        if(IsWindow(it->window)){++it;continue;}
        it->target->detach();it->target->Release();it=popupDrops.erase(it);
    }
    auto install=[](HWND hwnd,LPARAM value)->BOOL {
        auto& self=*reinterpret_cast<Editor*>(value);
        // Only controls already advertising the native legacy drop protocol.
        if(!(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_ACCEPTFILES))return TRUE;
        if(std::any_of(self.popupDrops.begin(),self.popupDrops.end(),[hwnd](const PopupDrop&x){return x.window==hwnd;}))return TRUE;
        auto* target=new(std::nothrow)FileDropTarget(hwnd,forwardPopupDrop,false,true,false);
        if(!target)return TRUE;target->attach(hwnd);self.popupDrops.push_back({hwnd,target});return TRUE;
    };
    // Sample editors without a verified dispatcher fallback can still support
    // standard OLE drops via their existing native legacy file-drop controls.
    if(!owner->engine.canLoadFile()&&panel)EnumChildWindows(panel,install,reinterpret_cast<LPARAM>(this));
    struct Search { Editor* self; WNDENUMPROC install; } search{this,install};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM value)->BOOL {
        auto& search=*reinterpret_cast<Search*>(value);
        if(search.self->hostGuard.ownsPopup(hwnd)){
            search.install(hwnd,reinterpret_cast<LPARAM>(search.self));
            EnumChildWindows(hwnd,search.install,reinterpret_cast<LPARAM>(search.self));
        }return TRUE;
    },reinterpret_cast<LPARAM>(&search));
}
void Editor::loadSamplePath(const wchar_t* path) {
    if (owner->engine.loadFile(path)) owner->sampleChanged();
    else MessageBoxW(window, L"The original plugin could not load this audio file. Try a WAV file supported by the plugin.", configuration().name.c_str(), MB_OK | MB_ICONERROR);
}
bool Editor::queueDrop(void* context, const std::wstring& path) {
    auto* self = static_cast<Editor*>(context);
    if (!self->window || !self->owner->engine.canLoadFile()) return false;
    self->droppedPath = path;
    // Load after the OLE drop callback returns, outside the host's drag loop.
    if (PostMessageW(self->window, loadDroppedSampleMessage, 0, 0)) return true;
    self->droppedPath.clear(); return false;
}
LRESULT CALLBACK Editor::windowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){
            const UINT id=LOWORD(wp);
            if(id==channelChoose){self->toggleChannel();return 0;}
            if(id==presetChoose){RECT rect{};GetWindowRect(self->presetButton,&rect);showPresetMenu(self,POINT{rect.left,rect.bottom});return 0;}
            if(id==presetPrevious||id==presetNext){
                const int count=self->owner->engine.presetCount();int current=-1;
                for(int i=0;i<count;i++){char label[2048]{};self->owner->engine.presetName(i,label,sizeof(label));if(self->owner->getPresetLabel()==label){current=i;break;}}
                if(count>0)self->selectPreset(current<0?(id==presetNext?0:count-1):(current+(id==presetNext?1:count-1))%count);
                return 0;
            }
        }
        if(message==WM_DRAWITEM){
            auto*d=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if(d&&d->CtlType==ODT_BUTTON&&d->CtlID>=presetPrevious&&d->CtlID<=channelChoose){
                HBRUSH brush=CreateSolidBrush((d->itemState&ODS_SELECTED)?RGB(59,70,70):RGB(32,40,43));FillRect(d->hDC,&d->rcItem,brush);DeleteObject(brush);
                SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,RGB(211,223,218));
                HGDIOBJ old=SelectObject(d->hDC,self->presetFont?self->presetFont:GetStockObject(DEFAULT_GUI_FONT));
                wchar_t title[4096]{};GetWindowTextW(d->hwndItem,title,4096);RECT text=d->rcItem;text.left+=8;text.right-=8;
                DrawTextW(d->hDC,title,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
                SelectObject(d->hDC,old);return TRUE;
            }
        }
        if (message == loadDroppedSampleMessage) {
            std::wstring path; path.swap(self->droppedPath);
            if (!path.empty()) self->loadSamplePath(path.c_str());
            return 0;
        }
        if (message == WM_TIMER && wp == 1) { self->tick(); return 0; }
        if (message == WM_NCDESTROY) SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

class Factory final : public IPluginFactory3 {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (sameId(iid, FUnknown_iid) || sameId(iid, IPluginFactory_iid) ||
            sameId(iid, IPluginFactory2_iid) || sameId(iid, IPluginFactory3_iid)) {
            *obj = static_cast<IPluginFactory3*>(this); addRef(); return kResultOk;
        }
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { auto n = --refs; if (!n) delete this; return n; }
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override {
        if (!info) return kInvalidArgument;
        *info = {}; ascii(info->vendor, vendorName); info->flags = PFactoryInfo::kUnicode;
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override { return configuration().valid ? 1 : 0; }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override {
        if (index || !info || !configuration().valid) return kInvalidArgument;
        *info = {}; std::memcpy(info->cid, componentId(), 16); info->cardinality = PClassInfo::kManyInstances;
        ascii(info->category, kVstAudioEffectClass); ascii(info->name, pluginName()); return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override {
        if (index || !info || !configuration().valid) return kInvalidArgument;
        *info = {}; std::memcpy(info->cid, componentId(), 16); info->cardinality = PClassInfo::kManyInstances;
        ascii(info->category, kVstAudioEffectClass); ascii(info->name, pluginName());
        ascii(info->subCategories, instrument() ? "Instrument|Synth" : "Fx"); ascii(info->vendor, vendorName);
        ascii(info->version, "0.6.1"); ascii(info->sdkVersion, kVstVersionString);
        info->classFlags = kSimpleModeSupported; return kResultOk;
    }
    tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW* info) override {
        if (index || !info || !configuration().valid) return kInvalidArgument;
        *info = {}; std::memcpy(info->cid, componentId(), 16); info->cardinality = PClassInfo::kManyInstances;
        ascii(info->category, kVstAudioEffectClass); utf16(info->name, PClassInfo::kNameSize, pluginName());
        ascii(info->subCategories, instrument() ? "Instrument|Synth" : "Fx"); utf16(info->vendor, PClassInfo2::kVendorSize, vendorName);
        utf16(info->version, PClassInfo2::kVersionSize, "0.6.1"); utf16(info->sdkVersion, PClassInfo2::kVersionSize, kVstVersionString);
        info->classFlags = kSimpleModeSupported; return kResultOk;
    }
    tresult PLUGIN_API setHostContext(FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (!configuration().valid || !sameId(cid, componentId())) return kNoInterface;
        auto* plugin = new (std::nothrow) Plugin;
        if (!plugin) return kOutOfMemory;
        const auto result = plugin->queryInterface(iid, obj); plugin->release(); return result;
    }
private:
    std::atomic<uint32> refs{1};
};
} // namespace

extern "C" __declspec(dllexport) IPluginFactory* PLUGIN_API GetPluginFactory() {
    return new (std::nothrow) Factory;
}
extern "C" __declspec(dllexport) bool InitDll() { return true; }
extern "C" __declspec(dllexport) bool ExitDll() {
    UnregisterClassW(windowClassName, moduleHandle); return true;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { moduleHandle = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}
