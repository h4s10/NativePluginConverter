// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "channel_settings.h"

// All parameter values use VST's normalized [0, 1] domain.
// Engine serializes native DLL calls originating from audio and editor threads.
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    bool open(const wchar_t* dllPath);
    bool openConfigured(const wchar_t* iniPath);
    bool isInstrument() const;
    int nativeFlags() const;
    ChannelSettings channelSettings() const;
    void setChannelSettings(const ChannelSettings& settings);
    bool supportsPortamento() const;
    bool supportsSampleLoad() const;
    bool canLoadFile() const;
    bool loadFile(const wchar_t* path);
    int presetCount() const;
    void presetName(int index, char* name, int capacity) const;
    bool loadPreset(int index);
    int latencySamples() const;
    void close();
    bool ready() const;
    bool canAutomate(int index) const;
    const char* error() const;
    int parameterCount() const;
    void parameterName(int index, char* name, int capacity);
    double getParameter(int index);
    void setParameter(int index, double normalized);
    void setParameterEditCallback(void (*callback)(void*, int, double), void* context);
    void configure(double sampleRate, int maxBlock);
    void setTransport(double tempo, double ppq, bool playing);
    void noteOn(int noteId, int pitch, int channel, float velocity);
    void noteOff(int noteId, int pitch, int channel, float velocity);
    void allNotesOff();
    void previewNoteOn(int pitch,int channel,float velocity);
    void previewNoteOff(int pitch);
    void render(float* left, float* right, int frames, const float* inputLeft=nullptr, const float* inputRight=nullptr);
    bool save(std::vector<uint8_t>& bytes);
    bool restore(const uint8_t* bytes, size_t size);
    bool showEditor(void* parent);
    void hideEditor();
    void* dialogOwner() const;
    void* nativeModule() const;
    void editorSize(int& width, int& height);
    void idle();
    bool loadSample(const wchar_t* path);
private:
    void noteOnInternal(int noteId,int pitch,int channel,float velocity,bool preview);
    void noteOffInternal(int noteId,int pitch,int channel,float velocity,bool preview);
    void clearPreviewNotes();
    struct Impl;
    Impl* p;
};
