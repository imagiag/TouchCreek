// Creek controls: panel state -> engine macros + note-mode drops (C++ port of
// creek-controls.js). Hardware details (pickup, debouncing, LED) live in
// ui/creek_ui.*, not here.
//
//   left fader S36 = Level, right fader S37 = Roughness,
//   S31 = Scale (P11 + S31 = Tone), S32 = Aeration, S33 = Flow (P11 + S33 = Attack),
//   S34 = Decay (P11 + S34 = Release), S30 = Free flow (P10 + S30 = Width), S35 = Variety,
//   P11 + S37 = filter cutoff, P11 + S36 = resonance (layers as TouchPulsar)
//   left switch  (S09/S10): up = Presets, centre = Notes, down = Diagnostics
//   right switch (S07/S08): down = Hold slow state; in Notes = Latch (touched notes keep pouring,
//                tap a latched note again to release it)
//   Presets:     P03..P06 recall the four presets (250 ms glide)
//   Notes:       P03..P09 play the scale (tap = tuned drop, hold = the creek's
//                bubbles gather onto the held notes), P00 / P02 octave,
//                P11 + P00 / P02 / P03..P09 = scale, P10 + note pad = root
//   MIDI (any channel): notes play like note pads (drop + pour, chromatic), velocity = drop level,
//                sustain pedal holds notes, pitch bend +-2, mod wheel / aftertouch = Surge,
//                CCs for every knob, CC 14/15 scale/root, Program Change 1-4 = presets
//   Diagnostics: P03..P07 toggle flow / bubbles / eddies / falls / surface, P08 = all on,
//                P09 = stress test (worst-case settings, rapid jumps, note bursts) - only in
//                builds with CREEK_STRESS=1 (the monitor build and the host tests)
#pragma once
#include <stdint.h>
#ifndef CREEK_STRESS
#define CREEK_STRESS 0
#endif
#include "creek_engine.h"

namespace creek {

struct Preset { const char* key; const char* name; float flow, roughness, scale, aeration; };
constexpr int kNumPresets = 4;
extern const Preset kPresets[kNumPresets];

constexpr int kNumScales = 7;
struct Scale { const char* name; int8_t semis[7]; };
extern const Scale kScales[kNumScales];

enum MacroIndex { M_FLOW = 0, M_ROUGH, M_SCALE, M_AER, M_LEVEL, M_WIDTH, M_FREE, M_VARIETY, M_DECAY, M_CUTOFF, M_RES, M_TONE, M_ATTACK, M_RELEASE, M_COUNT };
enum Mode { MODE_PRESETS = 0, MODE_NOTES, MODE_DIAG };
enum ControlEvent : uint8_t { CE_NONE = 0, CE_PRESET, CE_LAYER, CE_ALL, CE_MODE, CE_SCALE, CE_OCT, CE_ROOT, CE_NOTE, CE_STRESS, CE_CC };
constexpr int kMidiKey = 128;        // held keys: pads 0..11, MIDI notes 128 + note
constexpr float kBendSemis = 2.f;
int CcToMacro(int cc);               // -1 if the CC is not a knob

constexpr int kPadOctDn = 0, kPadOctUp = 2, kPadRoot = 10, kPadScale = 11;
constexpr int kPadPreset0 = 3, kPadNote0 = 3, kPadLayer0 = 3, kPadAll = 8, kPadStress = 9;
constexpr int kBaseMidi = 72;        // P03, root C, octave 0 = C5
constexpr int kMaxHeld = 4;
constexpr float kPresetGlideSec = 0.25f;

float LevelToGain(float v);          // 2 v^2, as TouchPulsar
float KnobToWidth(float v);
float MidiToHz(int midi);
float KnobToSec(float v, const float* range);     // exponential (Attack / Release)
float SecToKnob(float s, const float* range);

struct Drop { float hz, vel; };

class Controls {
public:
    Controls();
    void SetKnob(int k, float v);
    void RecallPreset(int i);
    void SetMode(Mode m);
    void PadDown(int pad, float vel);
    void PadUp(int pad);
    void SetRightSwitch(bool down);   // Latch in Notes, Hold (slow state) elsewhere
    void Midi(uint8_t status, uint8_t d1, uint8_t d2);   // raw MIDI (channel messages)
    int KeyMidi(int key) const { return key >= kMidiKey ? key - kMidiKey : PadMidi(key); }
    void Tick(double t);
    bool Gliding() const { return glideT_ >= 0; }
    void Compute(Macros& m) const;
    bool PopEvent(ControlEvent& type, int8_t& arg);
    bool PopDrop(Drop& d);
    int PadMidi(int pad) const;
    uint32_t ScaleMask() const;

    float value[M_COUNT];
    int preset = 0;
    Mode mode = MODE_PRESETS;
    uint32_t diagMask = 0x1F;
    bool hold = false;
    bool touched[12];
    int scale = 4, root = 0, oct = 0;
    int held[kMaxHeld];
    int nHeld = 0;
    bool stress = false;        // diagnostics P09: worst-case stress test
    bool latch = false;         // Notes + right switch down: notes stay held after release
    bool midiDown[128];
    bool sustain = false;
    float surge = 0, wheel = 0, pressure = 0, bend = 0;   // MIDI performance controls

private:
    bool chromatic() const { return scale == 0; }
    void octRange(int& lo, int& hi) const { if (chromatic()) { lo = -2; hi = 4; } else { lo = -1; hi = 2; } }
    void setScale(int s);
    void removeHeld(int pad);
    bool isHeld(int pad) const;
    void addHeld(int key, float hz, float vel);
    void keepHeld(bool (*keep)(const Controls&, int));
    void push(ControlEvent t, int8_t a);
    void pushDrop(float hz, float vel);
    float rnd();
    void stressTick(double t);
    float stressV_[4] = {1, 1, 1, 1};
    double stressNext_ = 0, stressDrop_ = 0;
    uint32_t rs_ = 0x12345678u;
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
