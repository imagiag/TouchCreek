#include "controls.h"
#include <math.h>

namespace creek {

const Preset kPresets[kNumPresets] = {
    {"trickle", "Quiet trickle", 0.20f, 0.25f, 0.20f, 0.30f},
    {"rocky",   "Rocky creek",   0.50f, 0.60f, 0.45f, 0.55f},
    {"cascade", "Small cascade", 0.75f, 0.80f, 0.50f, 0.85f},
    {"deeper",  "Deeper run",    0.65f, 0.35f, 0.80f, 0.40f},
};

// same scales as TouchPulsar (semitones above the root for P03..P09)
const Scale kScales[kNumScales] = {
    {"Chromatic", {0, 1, 2, 3, 4, 5, 6}},
    {"Major", {0, 2, 4, 5, 7, 9, 11}},
    {"Minor", {0, 2, 3, 5, 7, 8, 10}},
    {"Min Pent", {0, 3, 5, 7, 10, 12, 15}},
    {"Maj Pent", {0, 2, 4, 7, 9, 12, 14}},
    {"Dorian", {0, 2, 3, 5, 7, 9, 10}},
    {"Harm Minor", {0, 2, 3, 5, 7, 8, 11}},
};

float LevelToGain(float v) { return 2.f * v * v; }
float KnobToWidth(float v) { return v < 0.5f ? v * 2.f : 1.f + (ck::WIDTH_MAX - 1.f) * ((v - 0.5f) / 0.5f); }
float MidiToHz(int midi) { return 440.f * powf(2.f, (midi - 69) / 12.f); }

float KnobToSec(float v, const float* r) { return r[0] * powf(r[1] / r[0], v); }
float SecToKnob(float s, const float* r) { return logf(s / r[0]) / logf(r[1] / r[0]); }

int CcToMacro(int cc) {
    switch (cc) {
        case 7: return M_LEVEL;  case 20: return M_FLOW;    case 21: return M_ROUGH;
        case 22: return M_SCALE; case 23: return M_AER;     case 24: return M_FREE;
        case 25: return M_VARIETY; case 26: return M_DECAY; case 27: return M_WIDTH;
        case 28: return M_TONE;
        case 102: return M_CUTOFF; case 103: return M_RES;   // as TouchPulsar
        case 107: return M_ATTACK; case 72: return M_RELEASE; // as TouchPulsar
        default: return -1;
    }
}

Controls::Controls() {
    const Preset& p = kPresets[0];
    value[M_FLOW] = p.flow; value[M_ROUGH] = p.roughness; value[M_SCALE] = p.scale; value[M_AER] = p.aeration;
    value[M_LEVEL] = 0.7071f; value[M_WIDTH] = 0.5f;
    value[M_FREE] = 1.f; value[M_VARIETY] = 0.5f; value[M_DECAY] = 0.5f;
    value[M_CUTOFF] = 1.f; value[M_RES] = 0.f;   // filter open at power-up
    value[M_TONE] = 0.5f;
    value[M_ATTACK] = SecToKnob(ck::TUNE_ATTACK_SEC, ck::ATTACK_RANGE_SEC);    // default pour feel
    value[M_RELEASE] = SecToKnob(ck::TUNE_RELEASE_SEC, ck::RELEASE_RANGE_SEC);
    for (int k = 0; k < 4; k++) glideFrom_[k] = glideTo_[k] = 0;
    for (int i = 0; i < 12; i++) touched[i] = false;
    for (int i = 0; i < 128; i++) midiDown[i] = false;
}

void Controls::push(ControlEvent t, int8_t a) {
    const int n = (evW_ + 1) % kEv;
    if (n == evR_) return; // full: drop (display only)
    evT_[evW_] = t; evA_[evW_] = a; evW_ = n;
}
bool Controls::PopEvent(ControlEvent& type, int8_t& arg) {
    if (evR_ == evW_) return false;
    type = evT_[evR_]; arg = evA_[evR_]; evR_ = (evR_ + 1) % kEv;
    return true;
}
void Controls::throwStone(float vel) {
    const int n = (sW_ + 1) % kStones;
    if (n != sR_) { stones_[sW_] = fminf(1.f, vel); sW_ = n; }
    push(CE_STONE, 0);
}
bool Controls::PopStone(float& vel) {
    if (sR_ == sW_) return false;
    vel = stones_[sR_]; sR_ = (sR_ + 1) % kStones;
    return true;
}

bool Controls::PopDrop(Drop& d) {
    if (dR_ == dW_) return false;
    d = drops_[dR_]; dR_ = (dR_ + 1) % kDrops;
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
    push(CE_PRESET, (int8_t)i);
}

void Controls::SetMode(Mode m) {
    if (m == mode) return;
    mode = m;
    if (m != MODE_NOTES) keepHeld([](const Controls&, int k) { return k >= kMidiKey; });   // MIDI notes play in every mode
    if (m != MODE_DIAG && stress) { stress = false; push(CE_STRESS, 0); }
    push(CE_MODE, (int8_t)m);
}

int Controls::PadMidi(int pad) const {
    return kBaseMidi + root + kScales[scale].semis[pad - kPadNote0] + oct * (chromatic() ? 7 : 12);
}

// active scale as a 12-bit pitch-class mask (bit 0 = C); Chromatic = all 12
uint32_t Controls::ScaleMask() const {
    if (chromatic()) return 0xFFFu;
    uint32_t m = 0;
    for (int i = 0; i < 7; i++) m |= 1u << ((root + kScales[scale].semis[i]) % 12);
    return m;
}

void Controls::setScale(int s) {
    scale = s;
    int lo, hi; octRange(lo, hi);
    oct = oct < lo ? lo : (oct > hi ? hi : oct);
    push(CE_SCALE, (int8_t)s);
}

void Controls::removeHeld(int pad) {
    for (int i = 0; i < nHeld; i++) if (held[i] == pad) {
        for (int j = i; j + 1 < nHeld; j++) held[j] = held[j + 1];
        nHeld--;
        return;
    }
}

void Controls::PadDown(int pad, float vel) {
    if (pad < 0 || pad > 11) return;
    touched[pad] = true;
    if (pad == kPadStone) { throwStone(vel > 0.f ? vel : 0.7f); return; }   // every mode
    if (mode == MODE_PRESETS) {
        const int i = pad - kPadPreset0;
        if (i >= 0 && i < kNumPresets) RecallPreset(i);
    } else if (mode == MODE_DIAG) {
        const int l = pad - kPadLayer0;
        if (l >= 0 && l < ck::LAYERS) { diagMask ^= 1u << l; push(CE_LAYER, (int8_t)((diagMask >> l) & 1u)); }
        else if (pad == kPadAll) { diagMask = 0x1F; push(CE_ALL, 0); }
#if CREEK_STRESS
        else if (pad == kPadStress) { stress = !stress; stressNext_ = 0; push(CE_STRESS, stress ? 1 : 0); }
#endif
    } else {
        const bool note = pad >= kPadNote0 && pad <= kPadNote0 + 6;
        if (touched[kPadScale] && pad != kPadScale) {                 // P11 + pad: scale
            if (pad == kPadOctDn || pad == kPadOctUp) setScale((scale + (pad == kPadOctUp ? 1 : 6)) % 7);
            else if (note) setScale(pad - kPadNote0);
        } else if (touched[kPadRoot] && pad != kPadRoot) {            // P10 + note pad: root
            if (note) { root = ((PadMidi(pad) - kBaseMidi) % 12 + 12) % 12; push(CE_ROOT, (int8_t)root); }
        } else if (pad == kPadOctDn || pad == kPadOctUp) {
            int lo, hi; octRange(lo, hi);
            oct += pad == kPadOctUp ? 1 : -1;
            oct = oct < lo ? lo : (oct > hi ? hi : oct);
            push(CE_OCT, (int8_t)oct);
        } else if (note && latch && isHeld(pad)) {
            removeHeld(pad);                                          // latched note tapped again: release it
            push(CE_NOTE, (int8_t)pad);
        } else if (note) {
            addHeld(pad, MidiToHz(PadMidi(pad)), vel > 0.f ? fminf(1.f, vel) : 0.7f);
        }
    }
}

void Controls::PadUp(int pad) {
    if (pad < 0 || pad > 11) return;
    touched[pad] = false;
    if (latch && mode == MODE_NOTES) return;                          // latched: the note keeps pouring
    removeHeld(pad);
}

void Controls::keepHeld(bool (*keep)(const Controls&, int)) {
    int n = 0;
    for (int i = 0; i < nHeld; i++) if (keep(*this, held[i])) held[n++] = held[i];
    nHeld = n;
}

void Controls::addHeld(int key, float hz, float vel) {
    pushDrop(hz, vel);
    removeHeld(key);
    if (nHeld == kMaxHeld) { for (int j = 0; j + 1 < nHeld; j++) held[j] = held[j + 1]; nHeld--; }
    held[nHeld++] = key;
    push(CE_NOTE, (int8_t)(key & 0x7F));
}

// ---------------- MIDI (raw bytes, same entry point as the emulator) ----------------
void Controls::Midi(uint8_t st, uint8_t d1, uint8_t d2) {
    const uint8_t t = st & 0xF0;
    d1 &= 0x7F; d2 &= 0x7F;
    if (t == 0x90 && d2 > 0) {                                        // note on
        const int key = kMidiKey + d1;
        midiDown[d1] = true;
        if (latch && isHeld(key)) { removeHeld(key); return; }
        addHeld(key, MidiToHz(d1) * exp2f(bend / 12.f), d2 / 127.f);
    } else if (t == 0x80 || t == 0x90) {                              // note off
        midiDown[d1] = false;
        if (latch || sustain) return;
        removeHeld(kMidiKey + d1);
    } else if (t == 0xB0) {
        const float x = d2 / 127.f;
        const int m = CcToMacro(d1);
        if (m >= 0) { SetKnob(m, x); push(CE_CC, (int8_t)m); }
        else if (d1 == 1) { wheel = x; surge = fmaxf(wheel, pressure); }
        else if (d1 == 64) {
            sustain = d2 >= 64;
            if (!sustain && !latch) keepHeld([](const Controls& c, int k) { return k < kMidiKey || c.midiDown[k - kMidiKey]; });
        }
        else if (d1 == 29) { if (d2 > 0) throwStone(d2 / 127.f); }
        else if (d1 == 14) setScale(d2 * 7 / 128);
        else if (d1 == 15) { root = d2 * 12 / 128; push(CE_ROOT, (int8_t)root); }
        else if (d1 == 120 || d1 == 123) {
            keepHeld([](const Controls&, int k) { return k < kMidiKey; });
            for (int i = 0; i < 128; i++) midiDown[i] = false;
            sustain = false;
        }
    } else if (t == 0xC0) {
        if (d1 < kNumPresets) RecallPreset(d1);
    } else if (t == 0xD0) {
        pressure = d1 / 127.f; surge = fmaxf(wheel, pressure);
    } else if (t == 0xE0) {
        bend = (float)(((int)d2 << 7 | d1) - 8192) / 8192.f * kBendSemis;
    }
}

bool Controls::isHeld(int pad) const {
    for (int i = 0; i < nHeld; i++) if (held[i] == pad) return true;
    return false;
}

void Controls::SetRightSwitch(bool down) {
    const bool l = down && mode == MODE_NOTES;
    if (latch && !l)                                                  // release what isn't touched / still down
        keepHeld([](const Controls& c, int k) { return k >= kMidiKey ? (c.midiDown[k - kMidiKey] || c.sustain) : c.touched[k]; });
    latch = l;
    hold = down && mode != MODE_NOTES;
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
#if CREEK_STRESS
    if (stress) stressTick(t);
#endif
}

void Controls::pushDrop(float hz, float vel) {
    const int n = (dW_ + 1) % kDrops;
    if (n != dR_) { drops_[dW_] = {hz, vel}; dW_ = n; }
}

float Controls::rnd() {
    uint32_t x = rs_; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rs_ = x;
    return (float)(x >> 8) * 5.9604645e-8f;
}

// Worst case for the engine: mostly maximal settings with sudden jumps every
// 50-400 ms (incl. Flow 0 <-> 1), plus bursts of loud drops at ~20 per second.
#if CREEK_STRESS
void Controls::stressTick(double t) {
    if (t >= stressNext_) {
        for (int k = 0; k < 4; k++) { const float u = rnd(); stressV_[k] = u < 0.65f ? 1.f : (u < 0.8f ? 0.f : rnd()); }
        stressV_[M_FLOW] = rnd() < 0.85f ? 1.f : 0.f;
        stressNext_ = t + 0.05 + 0.35 * rnd();
    }
    while (t >= stressDrop_) {
        pushDrop(150.f * exp2f(6.f * rnd()), 1.f);
        stressDrop_ = (stressDrop_ > t - 0.1 ? stressDrop_ : t - 0.1) + 0.05;
    }
}
#endif

void Controls::Compute(Macros& m) const {
    m.flow = value[M_FLOW]; m.roughness = value[M_ROUGH]; m.scale = value[M_SCALE]; m.aeration = value[M_AER];
    m.gain = LevelToGain(value[M_LEVEL]); m.width = KnobToWidth(value[M_WIDTH]);
    // Surge (mod wheel / aftertouch): the water rushes on top of the knob settings
    if (surge > 0.f) {
        m.flow += surge * (1.f - m.flow); m.roughness += 0.6f * surge * (1.f - m.roughness); m.aeration += 0.4f * surge * (1.f - m.aeration);
    }
    m.mask = mode == MODE_DIAG ? diagMask : 0x1Fu;
    if (CREEK_STRESS && stress) { m.flow = stressV_[0]; m.roughness = stressV_[1]; m.scale = stressV_[2]; m.aeration = stressV_[3]; m.mask = 0x1Fu; }
    m.hold = hold;
    const float bendMul = exp2f(bend / 12.f);
    for (int k = 0; k < 4; k++) m.tuneHz[k] = k < nHeld ? MidiToHz(KeyMidi(held[k])) * bendMul : 0.f;
    m.tune = nHeld > 0;
    m.tuneDetail = mode == MODE_NOTES;   // ticks + splash fizz follow the last-played notes
    m.freeFlow = mode == MODE_NOTES ? value[M_FREE] : 1.f;   // presets / diagnostics always flow
    m.variety = value[M_VARIETY]; m.decay = value[M_DECAY];
    m.scaleMask = ScaleMask();
    m.cutoff = value[M_CUTOFF]; m.resonance = value[M_RES];
    m.tone = value[M_TONE];
    m.attack = KnobToSec(value[M_ATTACK], ck::ATTACK_RANGE_SEC); m.release = KnobToSec(value[M_RELEASE], ck::RELEASE_RANGE_SEC);
}

} // namespace creek
