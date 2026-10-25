#include "creek_ui.h"

using namespace synthux;
using namespace daisy;
using namespace creek;

void CreekUI::Init(DaisySeed& hw) {
    lastUs_ = System::GetUs();
    // Pads -> controls (callbacks fire inside touch_.Process()). Pressure and velocity are not used.
    touch_.pads().SetOnTouch([this](uint16_t pad) { ctl_.PadDown(pad); });
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
// Knobs: S36 Flow, S37 Roughness, S31 Scale, S32 Aeration, S33 Gain, S34 Width.
void CreekUI::readKnobs() {
    Knobs& k = touch_.knobs();
    AnalogControl* phys[M_COUNT] = {&k.s36(), &k.s37(), &k.s31(), &k.s32(), &k.s33(), &k.s34()};
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
    ctl_.SetMode(touch_.switches().B() == Switch3::POS_DOWN);   // left  S09/S10: down = diagnostics
    ctl_.hold = touch_.switches().A() == Switch3::POS_DOWN;     // right S07/S08: down = hold slow state
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

void CreekUI::AudioBlock(float* left, float* right, size_t n) {
    if (mailSeq_ != mailSeen_) { mailSeen_ = mailSeq_; engine_.SetMacros(mailbox_[mailRead_]); }
    engine_.Process(left, right, n);
}

void CreekUI::blink(uint8_t count, uint32_t onMs, uint32_t offMs) {
    blinkLeft_ = count; blinkOn_ = onMs; blinkOff_ = offMs;
    blinkLit_ = count > 0;
    blinkPhaseEnd_ = now() + onMs * 1e-3;
}

void CreekUI::handleEvents() {
    ControlEvent type; uint8_t arg;
    Knobs& k = touch_.knobs();
    AnalogControl* phys[4] = {&k.s36(), &k.s37(), &k.s31(), &k.s32()};
    while (ctl_.PopEvent(type, arg)) {
        switch (type) {
            case CE_PRESET:
                // soft pickup: the four macro knobs wait until they are moved
                for (int i = 0; i < 4; i++) knob_[i].Hold(ctl_.value[i], phys[i]->Value());
                blink(arg + 1, 70, 90);      // 1..4 blinks = preset number
                break;
            case CE_LAYER: blink(arg ? 2 : 1, 60, 80); break; // 2 = layer on, 1 = muted
            case CE_ALL:   blink(3, 40, 40); break;
            case CE_MODE:  blink(arg ? 2 : 1, 150, 100); break;
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
    } else if (ctl_.diag) {
        led = t >= flickerUntil_;             // diagnostics: lit, dips on bubbles
    } else {
        led = t < flickerUntil_;
    }
    hw.SetLed(led);
}
