// =============================================================================
// CreekEngine — procedural stereo creek synthesis, independent of hardware/UI.
// C++ port of web/engine/creek-engine.js (daisy/test compares the two).
//
//   Init(sampleRate, seed)  Reset(seed)  SetMacros(values)  Trigger(event)
//   Process(left, right, frames)
//
// 32-bit float, fixed-size arrays, no allocation. Everything that changes over
// time is tied to the absolute sample counter (control tick every Fs/250
// samples, micro tick every ck::MICRO), so output does not depend on how the
// host splits it into blocks.
// =============================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "config.h"
#include "dsp.h"
#include "flow_state.h"
#include "event_queue.h"
#include "bubble_pool.h"
#include "bubble_scheduler.h"
#include "mixer_output.h"
#include "layers.h"

namespace creek {

enum Layer { L_FREE = 0, L_BUBBLES,   // layer 0: free (was the noise wash)
             L_EDDIES, L_FALLS, L_SURFACE };
constexpr uint32_t kCloudBit = 0x20, kAllLayers = 0x3F;   // mask bit 5 = bubble cloud (after the mixer)

struct Macros {
    float flow = 0.20f, roughness = 0.25f, scale = 0.20f, aeration = 0.30f; // "Quiet trickle"
    float gain = 1.f;      // linear output gain
    float width = 1.f;     // 0 = mono .. ck::WIDTH_MAX
    uint32_t mask = kAllLayers;  // diagnostic layer mask (bit = layer on)
    bool hold = false;     // hold the slow state (events keep going)
    float tuneHz[4] = {0, 0, 0, 0}; // note mode: held note pitches (0 = none)
    bool tune = false;     // true while notes are held
    float freeFlow = 1.f;  // note mode: how much the creek plays by itself (0 = still; holding notes pours)
    float variety = 0.5f;  // tuned pitches: 0 one note .. 0.5 default .. 1 across the scale
    float decay = 0.5f;    // decay times: 0 = 1/4 .. 0.5 = 1x .. 1 = 4x
    uint32_t scaleMask = 0xFFF; // active scale as pitch classes (bit 0 = C)
    bool tuneDetail = false; // Notes mode: surface ticks + splash fizz follow the last-played notes
    float tone = 0.5f;     // 0 dark (more big bubbles) .. 0.5 default .. 1 bright (more small ones)
    float attack = ck::TUNE_ATTACK_SEC;   // note pour swell-in time (s)
    float release = ck::TUNE_RELEASE_SEC; // note pour tail (s)
    float cutoff = 1.f;    // low-pass: 0 = 30 Hz .. 1 = 18 kHz (open)
    float resonance = 0.f; // 0 = Q 0.7 .. 1 = Q 20
    float delayTime = ck::DELAY_DEFAULT_SEC;   // ping-pong delay time (s)
    float delayAmt = 0.f;  // 0 = no echoes .. 1 = long feedback
};

struct Stats {
    uint32_t births, drops, steals, queueDrops, limited;
    int active, maxActive;
    float meanF, sumLog2F, peak, rate, gate, eddy, splashRate, surfaceRate;
    uint32_t splashes, surface, stones;
    int activeUser, activeFalls, activeSurface;   // voices: played (drops / stones), plops, surface ticks
    float activity;                               // event-rate gate (Free flow / pour / stone)
    float cloud[2], cloudHz[2];                   // bubble cloud strength 0..1 and pitch (wake, drop)
    bool cloudOn;
    float E[ck::ZONES], T[ck::ZONES], B[ck::ZONES];
};

class CreekEngine {
public:
    void Init(float sampleRate, uint32_t seed);
    void Reset(uint32_t seed);
    void SetMacros(const Macros& m);   // nonfinite values are ignored
    bool Trigger(const Event& e);       // false if the queue is full
    void Process(float* left, float* right, size_t frames);

    void GetStats(Stats& s) const;
    // ping-pong delay lines (owned by the caller, e.g. SDRAM; DelayLen(sr) samples each).
    // Cleared here and on Reset; without them the delay is skipped.
    void SetDelayBuffers(float* l, float* r, size_t len) { delay_.SetBuffers(l, r, len); }
    static size_t DelayLen(float sr) { return (size_t)lroundf(ck::DELAY_BUFFER_SEC * sr); }
    const BubblePool& Pool() const { return pool_; }
    float SampleRate() const { return sr_; }
    uint32_t Seed() const { return seed_; }

private:
    void applyMacros(bool snap);
    void controlTick();
    void renderSegment(float* L, float* R, int n);
    void drop(const Event& e, int off);
    void stone(const Event& e, int off);
    float kStone_ = 0, stoneS_ = 0, creekFade_ = 1;
    bool tuneDetail_ = false;
    uint32_t stones_ = 0;
    float kTuneA_ = 0, kTuneR_ = 0, freeS_ = 1, activity_ = 1;

    float sr_ = 48000;
    uint32_t srInt_ = 48000;
    uint32_t seed_ = 1;
    uint64_t sample_ = 0, tickIndex_ = 0;
    int untilTick_ = 0;
    bool snapNext_ = true;

    Rng rng_[S_COUNT];
    Rng* rngT_[3];   // per-zone bubble timing streams
    Rng* rngP_[3];   // per-zone bubble parameter streams
    Macros macros_;
    FlowState flow_;
    EventQueue queue_;
    BubbleScheduler sched_;
    BubblePool pool_;
    Eddies eddies_;
    Falls falls_;
    Surface surface_;
    Cloud cloud_;
    Delay delay_;
    SpatialMixer mixer_;
    OutputStage out_;

    float bufL_[ck::LAYERS][ck::MICRO], bufR_[ck::LAYERS][ck::MICRO];
    float* pL_[ck::LAYERS];
    float* pR_[ck::LAYERS];
};

} // namespace creek
