// =============================================================================
// CreekUI — Touch 2 hardware glue for creek::Controls / creek::CreekEngine.
// Pads, knobs (soft pickup after preset recall), switches, LED, the
// double-buffered macro mailbox and the note-drop queue read by the audio
// callback.
// Runs in the main loop; the audio callback only calls AudioBlock().
// =============================================================================
#pragma once
#include <math.h>
#include "daisy_seed.h"
#include "../touch/touch.h"
#include "../creek/creek_engine.h"
#include "../creek/controls.h"

namespace synthux {

// Soft pickup (same behaviour as TouchPulsar / TouchString's MValue): after a
// preset recall a knob only starts writing again once it moves more than
// kThreshold from where it was, so recalling a preset never jumps back.
class Takeover {
public:
    float Process(float v) {
        if (!tracking_ && fabsf(v - init_) < kThreshold) return value_;
        tracking_ = true;
        value_ = v;
        return value_;
    }
    void Hold(float value, float knobNow) { value_ = value; init_ = knobNow; tracking_ = false; }
    void Track(float v) { value_ = init_ = v; tracking_ = true; }
    bool Tracking() const { return tracking_; }
private:
    static constexpr float kThreshold = 0.03f;
    float value_ = 0.f, init_ = 0.f;
    bool tracking_ = false;
};

class CreekUI {
public:
    CreekUI(Touch& touch, creek::CreekEngine& engine) : touch_(touch), engine_(engine) {}

    void Init(daisy::DaisySeed& hw);
    void Process(daisy::DaisySeed& hw);   // UI tick, every ~4 ms (main loop)

    // audio callback side: newest macros from the mailbox, then render
    void AudioBlock(float* left, float* right, size_t n);

    bool Stress() const { return ctl_.stress; }
    uint32_t QueueDrops() const { return trigDrops_; } // note drops lost to a full queue (the mailbox itself coalesces)

private:
    double now();
    void readKnobs();
    void handleEvents();
    void updateLed(daisy::DaisySeed& hw);
    void blink(uint8_t count, uint32_t onMs, uint32_t offMs);
    void post();
    void pushTrigger(const creek::Event& e);

    Touch& touch_;
    creek::CreekEngine& engine_;
    creek::Controls ctl_;
    creek::Macros macros_;

    Takeover knob_[creek::M_COUNT];
    bool firstRun_ = true, altPrev_ = false;

    // mailbox (main loop writes the slot the audio side is not reading)
    creek::Macros mailbox_[2];
    volatile int mailRead_ = 0;
    volatile uint32_t mailSeq_ = 0;
    uint32_t mailSeen_ = 0;

    // note drops: single-producer (main loop) single-consumer (audio) ring
    static constexpr uint32_t kTrig = 16;
    creek::Event trig_[kTrig];
    volatile uint32_t trigW_ = 0, trigR_ = 0;
    uint32_t trigDrops_ = 0;

    // time (64-bit microsecond accumulator; System::GetUs wraps after ~71 min)
    uint32_t lastUs_ = 0;
    uint64_t usAcc_ = 0;

    // LED
    uint32_t lastLog_ = 0;
    double flickerUntil_ = 0;
    uint8_t blinkLeft_ = 0;
    uint32_t blinkOn_ = 0, blinkOff_ = 0;
    double blinkPhaseEnd_ = 0;
    bool blinkLit_ = false;
};

} // namespace synthux
