// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>

// Bridge-owned channel controls. Defaults preserve pre-channel-settings playback.
// Slide time is milliseconds; it is not FL Studio's undocumented knob curve.
struct ChannelSettings {
    int maxPolyphony=256;
    bool mono=false, portamento=false;
    int slideMs=100, rootNote=60, transpose=0, fineCents=0;
    float gain=1, pan=0;
    void sanitize() {
        maxPolyphony=std::clamp(maxPolyphony,1,256);
        slideMs=std::clamp(slideMs,0,5000);
        rootNote=std::clamp(rootNote,0,127);
        transpose=std::clamp(transpose,-48,48);
        fineCents=std::clamp(fineCents,-100,100);
        gain=std::isfinite(gain)?std::clamp(gain,0.f,2.f):1.f;
        pan=std::isfinite(pan)?std::clamp(pan,-1.f,1.f):0.f;
    }
};
