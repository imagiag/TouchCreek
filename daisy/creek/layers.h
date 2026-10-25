// Phase 2 layers (mirrors creek-engine.js): eddies, small waterfalls, surface
// detail, plus the Hazard event stream they use. All noise-free for now.
#pragma once
#include "config.h"
#include "dsp.h"
#include "flow_state.h"
#include "bubble_pool.h"
#include "event_queue.h"

namespace creek {

// One Poisson event stream with a time-varying rate (same scheme as the
// bubble scheduler: float accumulator, exponential thresholds, exact samples).
class Hazard {
public:
    static constexpr int kMax = 4;
    void Init(Rng& rng) { acc_ = 0; thr_ = -logf(rng.UniPos()); inc_ = 0; n = 0; over = 0; }
    void SetRate(float lam, float sr) { inc_ = lam / sr; }
    void Run(Rng& rng, int len) {
        n = 0;
        if (inc_ <= 0.f) return;
        float acc = acc_;
        for (int i = 0; i < len; i++) {
            acc += inc_;
            if (acc >= thr_) {
                acc -= thr_;
                thr_ = -logf(rng.UniPos());
                if (n < kMax) off[n++] = i; else over++;
            }
        }
        acc_ = acc;
    }
    int off[kMax];
    int n = 0;
    uint32_t over = 0;
private:
    float acc_ = 0, thr_ = 1, inc_ = 0;
};

// Eddies: 2-4 overlapping slow swells (bounded random, not an LFO). No audio
// of their own: they make the rock wake surge. Scale slows them down.
class Eddies {
public:
    void Init(Rng& rng);
    float Tick(Rng& rng, float dt, float mul, float depth);
    float value = 0;
private:
    static constexpr int N = ck::EDDY_SLOTS;
    float t_[N], dur_[N], peak_[N], wait_[N];
    bool active_[N];
};

class Surface;

// Small waterfalls at the drop: a soft low "plop" (falling tone), an
// entrainment impulse into the drop zone, and 2-8 bubble children (rendered
// by the bubble bank) when Aeration permits. Bounded shared splash energy.
class Falls {
public:
    void Init(float sr, Rng& rng);
    void Update(const FlowState& fs);                       // control tick
    void Run(Rng& rng, int n, FlowState& fs, float scaleMul, EventQueue& q, uint64_t sample, Surface& surf);
    BubblePool pool;
    float rate = 0;
    uint32_t count = 0;
private:
    Hazard hz_;
    float sr_ = 48000, energy_ = 0, kE_ = 0;
};

// Surface detail: sparse, quiet, very short high ticks (2-9 kHz), more near
// the drop, at turbulence peaks and just after splashes; loose bursts.
class Surface {
public:
    void Init(float sr, Rng& rng);
    void Update(const FlowState& fs, Rng& rng, float dt, float eddy);  // control tick
    void Run(Rng& rng, int n);
    BubblePool pool;
    float rate = 0, boost = 0;
    uint32_t count = 0;
private:
    Hazard hz_;
    Drift drift_;
    float sr_ = 48000, kB_ = 0, lnHz_ = 1;
};

} // namespace creek
