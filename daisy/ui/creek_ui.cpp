#include "creek_ui.h"

using namespace synthux;
using namespace daisy;
using namespace creek;

void CreekUI::Init(DaisySeed& hw) {
    lastUs_ = System::GetUs();
    // Pads -> controls (callbacks fire inside touch_.Process()). Velocity sets the drop level in Notes.
    touch_.pads().SetOnTouch([this](uint16_t pad) { ctl_.PadDown(pad, touch_.pads().Velocity(pad)); });
    touch_.pads().SetOnRelease([this](uint16_t pad) { ctl_.PadUp(pad); });
    ctl_.Compute(macros_);
    post();
#if !CREEK_MONITOR
    MidiUsbHandler::Config midiCfg;
    midi_.Init(midiCfg);
    midi_.StartReceive();
#endif
}

// USB MIDI -> raw bytes -> Controls::Midi (same entry point as the emulator's Web MIDI)
void CreekUI::PollMidi() {
#if !CREEK_MONITOR
    midi_.Listen();
    while (midi_.HasEvents()) {
        MidiEvent m = midi_.PopEvent();
        const uint8_t ch = (uint8_t)(m.channel & 0x0F);
        switch (m.type) {
            case NoteOff: case NoteOn: case ControlChange: case ProgramChange: case ChannelPressure: case PitchBend:
                ctl_.Midi((uint8_t)(0x80 | (m.type << 4) | ch), m.data[0], m.data[1]);
                break;
            case ChannelMode: // CC 120..127 (all notes off etc.)
                ctl_.Midi((uint8_t)(0xB0 | ch), m.data[0], m.data[1]);
                break;
            default: break;
        }
    }
#endif
}

double CreekUI::now() {
    const uint32_t us = System::GetUs();
    usAcc_ += (uint32_t)(us - lastUs_);
    lastUs_ = us;
    return usAcc_ * 1e-6;
}

// ---------------------------------------------------------------------------
// Physical controls S30..S37 (0..7) and their macro per layer: normal,
// while P10 is held, while P11 is held (-1 = same as normal).
struct KnobLayers { int8_t main, p10, p11; };
static constexpr KnobLayers kLayers[8] = {
    {M_FREE, M_WIDTH, -1},     // S30 Free flow | P10: Width (as TouchPulsar)
    {M_SCALE, M_TONE, -1},     // S31 Scale | P10: Tone
    {M_AER, -1, -1},           // S32 Aeration
    {M_FLOW, -1, M_ATTACK},    // S33 Flow | P11: Attack (as TouchPulsar)
    {M_DECAY, M_RELEASE, -1},  // S34 Decay | P10: Release (as TouchPulsar)
    {M_VARIETY, -1, -1},       // S35 Variety
    {M_LEVEL, -1, M_RES},      // S36 Level | P11: Resonance
    {M_ROUGH, -1, M_CUTOFF},   // S37 Roughness | P11: Cutoff
};
// physical control of each macro (for pickup after presets / MIDI CCs)
static int physOf(int m) {
    for (int p = 0; p < 8; p++) if (kLayers[p].main == m || kLayers[p].p10 == m || kLayers[p].p11 == m) return p;
    return 0;
}

int CreekUI::activeMacro(int p) const {
    if (ctl_.touched[kPadScale] && kLayers[p].p11 >= 0) return kLayers[p].p11;
    if (ctl_.touched[kPadRoot] && kLayers[p].p10 >= 0) return kLayers[p].p10;
    return kLayers[p].main;
}

void CreekUI::readKnobs() {
    Knobs& k = touch_.knobs();
    AnalogControl* phys[8] = {&k.s30(), &k.s31(), &k.s32(), &k.s33(), &k.s34(), &k.s35(), &k.s36(), &k.s37()};
    for (int p = 0; p < 8; p++) {
        const float v = phys[p]->Process();                   // each knob read once per tick
        const int m = activeMacro(p);
        if (firstRun_) {                                       // power-up: the panel is the state ...
            knob_[kLayers[p].main].Track(v); ctl_.SetKnob(kLayers[p].main, v);
            if (kLayers[p].p10 >= 0) knob_[kLayers[p].p10].Hold(ctl_.value[kLayers[p].p10], v);   // ... layers start at defaults
            if (kLayers[p].p11 >= 0) knob_[kLayers[p].p11].Hold(ctl_.value[kLayers[p].p11], v);
            active_[p] = m;
            continue;
        }
        if (m != active_[p]) { knob_[m].Hold(ctl_.value[m], v); active_[p] = m; }   // layer switch: wait for movement
        if (ctl_.Gliding() && m < 4 && !knob_[m].Tracking()) continue;            // preset glide owns the macro
        const float out = knob_[m].Process(v);
        if (knob_[m].Tracking() && out != ctl_.value[m]) ctl_.SetKnob(m, out);
    }
    firstRun_ = false;
}

void CreekUI::Process(DaisySeed& hw) {
    const double t = now();
    ctl_.Tick(t);
    touch_.Process();                       // fires PadDown callbacks
    // switches: libDaisy Switch3 POS_UP / POS_CENTER / POS_DOWN
    const int left = touch_.switches().B();                     // left S09/S10: presets / notes / diagnostics
    ctl_.SetMode(left == Switch3::POS_UP ? MODE_PRESETS : (left == Switch3::POS_DOWN ? MODE_DIAG : MODE_NOTES));
    ctl_.SetRightSwitch(touch_.switches().A() == Switch3::POS_DOWN);  // right S07/S08: down = Latch (Notes) / Hold
    Drop d;
    while (ctl_.PopDrop(d)) {                                   // note pads -> tuned drops
        Event e; e.type = EV_DROP; e.radiusMm = 3000.f / d.hz; e.amp = 0.25f + 0.75f * d.vel;
        pushTrigger(e);
    }
    handleEvents();                          // before knobs: a recall must arm pickup first
    readKnobs();
    ctl_.Compute(macros_);
    post();
    updateLed(hw);
}

void CreekUI::post() {
    const int w = 1 - mailRead_;
    mailbox_[w] = macros_;
    mailRead_ = w;
    mailSeq_ = mailSeq_ + 1;
}

void CreekUI::pushTrigger(const Event& e) {
    const uint32_t w = trigW_;
    if (w - trigR_ >= kTrig) { trigDrops_++; return; }        // full: drop (never block audio)
    trig_[w % kTrig] = e;
    asm volatile("" ::: "memory");
    trigW_ = w + 1;
}

void CreekUI::AudioBlock(float* left, float* right, size_t n) {
    if (mailSeq_ != mailSeen_) { mailSeen_ = mailSeq_; engine_.SetMacros(mailbox_[mailRead_]); }
    while (trigR_ != trigW_) {
        asm volatile("" ::: "memory");
        engine_.Trigger(trig_[trigR_ % kTrig]);
        trigR_ = trigR_ + 1;
    }
    engine_.Process(left, right, n);
}

void CreekUI::blink(uint8_t count, uint32_t onMs, uint32_t offMs) {
    blinkLeft_ = count; blinkOn_ = onMs; blinkOff_ = offMs;
    blinkLit_ = count > 0;
    blinkPhaseEnd_ = now() + onMs * 1e-3;
}

void CreekUI::handleEvents() {
    ControlEvent type; int8_t arg;
    Knobs& k = touch_.knobs();
    AnalogControl* phys[8] = {&k.s30(), &k.s31(), &k.s32(), &k.s33(), &k.s34(), &k.s35(), &k.s36(), &k.s37()};
    while (ctl_.PopEvent(type, arg)) {
        switch (type) {
            case CE_PRESET:
                // soft pickup: the four macro knobs wait until they are moved
                for (int i = 0; i < 4; i++) knob_[i].Hold(ctl_.value[i], phys[physOf(i)]->Value());
                blink((uint8_t)(arg + 1), 70, 90);      // 1..4 blinks = preset number
                break;
            case CE_LAYER: blink(arg ? 2 : 1, 60, 80); break; // 2 = layer on, 1 = muted
            case CE_ALL:   blink(3, 40, 40); break;
            case CE_MODE:  blink(arg + 1, 150, 100); break;            // 1 presets, 2 notes, 3 diagnostics
            case CE_SCALE: blink(arg + 1, 30, 30); break;              // 1..7 quick blinks = scale number
            case CE_OCT:   blink(arg < 0 ? 1 - arg : arg + 1, 25, 25); break;
            case CE_ROOT:  blink(1, 120, 0); break;
            case CE_NOTE:  flickerUntil_ = now() + 0.04; break;
            case CE_STRESS: blink(arg ? 4 : 1, 40, 40); break;
            case CE_CC:    // a MIDI CC moved this value: its knob waits until it's moved (as TouchPulsar)
                if (arg >= 0 && arg < M_COUNT) knob_[arg].Hold(ctl_.value[arg], phys[physOf(arg)]->Value());
                break;
            default: break;
        }
    }
}

// LED: event blinks > short flicker on louder bubble births; steady glow in diagnostics.
void CreekUI::updateLed(DaisySeed& hw) {
    const double t = now();
    const BubblePool& p = engine_.Pool();
    const uint32_t w = p.logW;
    if (w - lastLog_ > (uint32_t)kLog) lastLog_ = w - kLog;
    for (; lastLog_ != w; lastLog_++)
        if (p.logA[lastLog_ & (kLog - 1)] > 0.06f) flickerUntil_ = t + 0.02;

    bool led;
    if (blinkLeft_) {
        if (t >= blinkPhaseEnd_) {
            if (blinkLit_) { blinkLit_ = false; blinkPhaseEnd_ = t + blinkOff_ * 1e-3; blinkLeft_--; }
            else { blinkLit_ = true; blinkPhaseEnd_ = t + blinkOn_ * 1e-3; }
        }
        led = blinkLit_;
    } else if (ctl_.stress) {
        led = (usAcc_ / 62500u) % 2 == 0;     // stress test running: 8 Hz blink
    } else if (ctl_.mode == MODE_DIAG) {
        led = t >= flickerUntil_;             // diagnostics: lit, dips on bubbles
    } else {
        led = t < flickerUntil_;
    }
    hw.SetLed(led);
}
