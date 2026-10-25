// Creek controls: panel state -> engine macros (C++ port of creek-controls.js).
// Hardware details (pickup, debouncing, LED) live in ui/creek_ui.*, not here.
//
//   left fader S36 = Flow, right fader S37 = Roughness,
//   S31 = Scale, S32 = Aeration, S33 = Gain, S34 = Width (S30, S35 reserved)
//   left switch  (S09/S10): up/centre = Performance, down = Diagnostics
//   right switch (S07/S08): up/centre = live, down = Hold slow state
//   Performance: P03..P06 recall the four presets (250 ms glide)
//   Diagnostics: P03..P07 toggle flow / bubbles / eddies / falls / surface, P08 = all on
#pragma once
#include <stdint.h>
#include "creek_engine.h"

namespace creek {

struct Preset { const char* key; const char* name; float flow, roughness, scale, aeration; };
constexpr int kNumPresets = 4;
extern const Preset kPresets[kNumPresets];

enum MacroIndex { M_FLOW = 0, M_ROUGH, M_SCALE, M_AER, M_GAIN, M_WIDTH, M_COUNT };
enum ControlEvent : uint8_t { CE_NONE = 0, CE_PRESET, CE_LAYER, CE_ALL, CE_MODE };

constexpr int kPadPreset0 = 3, kPadLayer0 = 3, kPadAll = 8;
constexpr float kPresetGlideSec = 0.25f;

float KnobToGain(float v);
float KnobToWidth(float v);

class Controls {
public:
    Controls();
    void SetKnob(int k, float v);
    void RecallPreset(int i);
    void SetMode(bool diag);
    void PadDown(int pad);
    void Tick(double t);
    bool Gliding() const { return glideT_ >= 0; }
    void Compute(Macros& m) const;
    bool PopEvent(ControlEvent& type, uint8_t& arg);

    float value[M_COUNT];
    int preset = 0;
    bool diag = false;
    uint32_t diagMask = 0x1F;
    bool hold = false;

private:
    void push(ControlEvent t, uint8_t a);
    float glideFrom_[4], glideTo_[4];
    float glideT_ = -1;
    double lastT_ = 0;
    static constexpr int kEv = 8;
    ControlEvent evT_[kEv]; uint8_t evA_[kEv];
    int evR_ = 0, evW_ = 0;
};

} // namespace creek
