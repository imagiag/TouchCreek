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
}

double CreekUI::now() {
    const uint32_t us = System::GetUs();
    usAcc_ += (uint32_t)(us - lastUs_);
    lastUs_ = us;
    return usAcc_ * 1e-6;
}

// ---------------------------------------------------------------------------
// Physical control per macro (M_*): S33 Flow, S37 Roughness, S31 Scale,
// S32 Aeration, S36 Level, S34 Width.
void CreekUI::knobsPhys(AnalogControl** p) {
    Knobs& k = touch_.knobs();
    p[M_FLOW] = &k.s33(); p[M_ROUGH] = &k.s37(); p[M_SCALE] = &k.s31();
    p[M_AER] = &k.s32(); p[M_LEVEL] = &k.s36(); p[M_WIDTH] = &k.s34();
}

void CreekUI::readKnobs() {
    AnalogControl* phys[M_COUNT];
    knobsPhys(phys);
    for (int i = 0; i < M_COUNT; i++) {
        const float v = phys[i]->Process();
        if (firstRun_) { knob_[i].Track(v); ctl_.SetKnob(i, v); continue; } // power-up: the panel is the state
        if (ctl_.Gliding() && i < 4 && !knob_[i].Tracking()) continue;     // preset glide owns the macro
        const float out = knob_[i].Process(v);
        if (knob_[i].Tracking() && out != ctl_.value[i]) ctl_.SetKnob(i, out);
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
    ctl_.hold = touch_.switches().A() == Switch3::POS_DOWN;     // right S07/S08: down = hold slow state
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
    AnalogControl* phys[M_COUNT];
    knobsPhys(phys);
    while (ctl_.PopEvent(type, arg)) {
        switch (type) {
            case CE_PRESET:
                // soft pickup: the four macro knobs wait until they are moved
                for (int i = 0; i < 4; i++) knob_[i].Hold(ctl_.value[i], phys[i]->Value());
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
