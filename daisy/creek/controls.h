// Creek controls: panel state -> engine macros + note-mode drops (C++ port of
// creek-controls.js). Hardware details (pickup, debouncing, LED) live in
// ui/creek_ui.*, not here.
//
//   left fader S36 = Level, right fader S37 = Roughness,
//   S31 = Scale, S32 = Aeration, S33 = Flow, S34 = Width (S30, S35 reserved)
//   left switch  (S09/S10): up = Presets, centre = Notes, down = Diagnostics
//   right switch (S07/S08): down = Hold slow state
//   Presets:     P03..P06 recall the four presets (250 ms glide)
//   Notes:       P03..P09 play the scale (tap = tuned drop, hold = the creek's
//                bubbles gather onto the held notes), P00 / P02 octave,
//                P11 + P00 / P02 / P03..P09 = scale, P10 + note pad = root
//   Diagnostics: P03..P07 toggle flow / bubbles / eddies / falls / surface, P08 = all on
#pragma once
#include <stdint.h>
#include "creek_engine.h"

namespace creek {

struct Preset { const char* key; const char* name; float flow, roughness, scale, aeration; };
constexpr int kNumPresets = 4;
extern const Preset kPresets[kNumPresets];

constexpr int kNumScales = 7;
struct Scale { const char* name; int8_t semis[7]; };
extern const Scale kScales[kNumScales];

enum MacroIndex { M_FLOW = 0, M_ROUGH, M_SCALE, M_AER, M_LEVEL, M_WIDTH, M_COUNT };
enum Mode { MODE_PRESETS = 0, MODE_NOTES, MODE_DIAG };
enum ControlEvent : uint8_t { CE_NONE = 0, CE_PRESET, CE_LAYER, CE_ALL, CE_MODE, CE_SCALE, CE_OCT, CE_ROOT, CE_NOTE };

constexpr int kPadOctDn = 0, kPadOctUp = 2, kPadRoot = 10, kPadScale = 11;
constexpr int kPadPreset0 = 3, kPadNote0 = 3, kPadLayer0 = 3, kPadAll = 8;
constexpr int kBaseMidi = 72;        // P03, root C, octave 0 = C5
constexpr int kMaxHeld = 4;
constexpr float kPresetGlideSec = 0.25f;

float LevelToGain(float v);          // 2 v^2, as TouchPulsar
float KnobToWidth(float v);
float MidiToHz(int midi);

struct Drop { float hz, vel; };

class Controls {
public:
    Controls();
    void SetKnob(int k, float v);
    void RecallPreset(int i);
    void SetMode(Mode m);
    void PadDown(int pad, float vel);
    void PadUp(int pad);
    void Tick(double t);
    bool Gliding() const { return glideT_ >= 0; }
    void Compute(Macros& m) const;
    bool PopEvent(ControlEvent& type, int8_t& arg);
    bool PopDrop(Drop& d);
    int PadMidi(int pad) const;

    float value[M_COUNT];
    int preset = 0;
    Mode mode = MODE_PRESETS;
    uint32_t diagMask = 0x1F;
    bool hold = false;
    bool touched[12];
    int scale = 4, root = 0, oct = 0;
    int held[kMaxHeld];
    int nHeld = 0;

private:
    bool chromatic() const { return scale == 0; }
    void octRange(int& lo, int& hi) const { if (chromatic()) { lo = -2; hi = 4; } else { lo = -1; hi = 2; } }
    void setScale(int s);
    void removeHeld(int pad);
    void push(ControlEvent t, int8_t a);
    float glideFrom_[4], glideTo_[4];
    float glideT_ = -1;
    double lastT_ = 0;
    static constexpr int kEv = 16;
    ControlEvent evT_[kEv]; int8_t evA_[kEv];
    int evR_ = 0, evW_ = 0;
    static constexpr int kDrops = 8;
    Drop drops_[kDrops];
    int dR_ = 0, dW_ = 0;
};

} // namespace creek
