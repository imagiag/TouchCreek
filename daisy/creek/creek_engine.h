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
#include "flow_bed.h"
#include "mixer_output.h"

namespace creek {

enum Layer { L_FLOW = 0, L_BUBBLES, L_EDDIES, L_FALLS, L_SURFACE };

struct Macros {
    float flow = 0.20f, roughness = 0.25f, scale = 0.20f, aeration = 0.30f; // "Quiet trickle"
    float gain = 1.f;      // linear output gain
    float width = 1.f;     // 0 = mono .. ck::WIDTH_MAX
    uint32_t mask = 0x1F;  // diagnostic layer mask (bit = layer on)
    bool hold = false;     // hold the slow state (events keep going)
};

struct Stats {
    uint32_t births, drops, steals, queueDrops, limited;
    int active, maxActive;
    float meanF, sumLog2F, peak, rate, gate;
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
    const BubblePool& Pool() const { return pool_; }
    float SampleRate() const { return sr_; }
    uint32_t Seed() const { return seed_; }

private:
    void applyMacros(bool snap);
    void controlTick();
    void renderSegment(float* L, float* R, int n);

    float sr_ = 48000;
    uint32_t srInt_ = 48000;
    uint32_t seed_ = 1;
    uint64_t sample_ = 0, tickIndex_ = 0;
    int untilTick_ = 0;
    bool snapNext_ = true;

    Rng rng_[S_COUNT];
    Macros macros_;
    FlowState flow_;
    EventQueue queue_;
    BubbleScheduler sched_;
    BubblePool pool_;
    FlowBed bed_;
    SpatialMixer mixer_;
    OutputStage out_;

    float bufL_[ck::LAYERS][ck::MICRO], bufR_[ck::LAYERS][ck::MICRO];
    float* pL_[ck::LAYERS];
    float* pR_[ck::LAYERS];
};

} // namespace creek
