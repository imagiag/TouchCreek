#include "controls.h"
#include <math.h>

namespace creek {

const Preset kPresets[kNumPresets] = {
    {"trickle", "Quiet trickle", 0.20f, 0.25f, 0.20f, 0.30f},
    {"rocky",   "Rocky creek",   0.50f, 0.60f, 0.45f, 0.55f},
    {"cascade", "Small cascade", 0.75f, 0.80f, 0.50f, 0.85f},
    {"deeper",  "Deeper run",    0.65f, 0.35f, 0.80f, 0.40f},
};

// knob 0..1 -> output gain: silent at the bottom, 0 dB at noon, +6 dB fully up
float KnobToGain(float v) {
    if (v < 0.01f) return 0.f;
    const float dB = v < 0.5f ? -30.f + 30.f * (v / 0.5f) : 6.f * ((v - 0.5f) / 0.5f);
    return powf(10.f, dB / 20.f);
}
// knob 0..1 -> width: 0 = mono, noon = 1 (natural), fully up = WIDTH_MAX
float KnobToWidth(float v) { return v < 0.5f ? v * 2.f : 1.f + (ck::WIDTH_MAX - 1.f) * ((v - 0.5f) / 0.5f); }

Controls::Controls() {
    const Preset& p = kPresets[0];
    value[M_FLOW] = p.flow; value[M_ROUGH] = p.roughness; value[M_SCALE] = p.scale; value[M_AER] = p.aeration;
    value[M_GAIN] = 0.5f; value[M_WIDTH] = 0.5f;
    for (int k = 0; k < 4; k++) glideFrom_[k] = glideTo_[k] = 0;
}

void Controls::push(ControlEvent t, uint8_t a) {
    const int n = (evW_ + 1) % kEv;
    if (n == evR_) return; // full: drop (display only)
    evT_[evW_] = t; evA_[evW_] = a; evW_ = n;
}

bool Controls::PopEvent(ControlEvent& type, uint8_t& arg) {
    if (evR_ == evW_) return false;
    type = evT_[evR_]; arg = evA_[evR_]; evR_ = (evR_ + 1) % kEv;
    return true;
}

void Controls::SetKnob(int k, float v) {
    value[k] = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    if (k < 4 && glideT_ >= 0) glideFrom_[k] = glideTo_[k] = value[k];
}

void Controls::RecallPreset(int i) {
    const Preset& p = kPresets[i];
    for (int k = 0; k < 4; k++) glideFrom_[k] = value[k];
    glideTo_[0] = p.flow; glideTo_[1] = p.roughness; glideTo_[2] = p.scale; glideTo_[3] = p.aeration;
    glideT_ = 0; preset = i;
    push(CE_PRESET, (uint8_t)i);
}

void Controls::SetMode(bool d) {
    if (d == diag) return;
    diag = d;
    push(CE_MODE, d ? 1 : 0);
}

void Controls::PadDown(int pad) {
    if (!diag) {
        const int i = pad - kPadPreset0;
        if (i >= 0 && i < kNumPresets) RecallPreset(i);
    } else {
        const int l = pad - kPadLayer0;
        if (l >= 0 && l < ck::LAYERS) { diagMask ^= 1u << l; push(CE_LAYER, (uint8_t)((diagMask >> l) & 1u)); }
        else if (pad == kPadAll) { diagMask = 0x1F; push(CE_ALL, 0); }
    }
}

void Controls::Tick(double t) {
    const double dt = t > lastT_ ? t - lastT_ : 0.0;
    lastT_ = t;
    if (glideT_ >= 0) {
        glideT_ += (float)dt;
        const float x = fminf(1.f, glideT_ / kPresetGlideSec), s = x * x * (3.f - 2.f * x);
        for (int k = 0; k < 4; k++) value[k] = glideFrom_[k] + (glideTo_[k] - glideFrom_[k]) * s;
        if (x >= 1.f) glideT_ = -1;
    }
}

void Controls::Compute(Macros& m) const {
    m.flow = value[M_FLOW]; m.roughness = value[M_ROUGH]; m.scale = value[M_SCALE]; m.aeration = value[M_AER];
    m.gain = KnobToGain(value[M_GAIN]); m.width = KnobToWidth(value[M_WIDTH]);
    m.mask = diag ? diagMask : 0x1Fu;
    m.hold = hold;
}

} // namespace creek
