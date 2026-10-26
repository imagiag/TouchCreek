// Phase 2 layers (mirrors creek-engine.js): eddies, small waterfalls, surface
// detail, plus the Hazard event stream they use. All noise-free for now.
#pragma once
#include <stddef.h>
#include "config.h"
#include "dsp.h"
#include "flow_state.h"
#include "bubble_pool.h"
#include "event_queue.h"
#include "bubble_scheduler.h"

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

// Bubble cloud: dense bubbling also rings as one body, a low, heavily damped
// collective mode (a cheap approximation motivated by Xue et al. 2023, not their
// solver). One mode for the rock wake, one for the drop; every bubble born in the
// zone kicks it with a random sign (and splashes kick the drop's harder), so it is
// a soft low rumble that grows with density. Lower with denser water and Scale.
class Cloud {
public:
    void Init(float sr, Rng& rng);
    void Update(Rng& rng, const BubbleScheduler& sched, float dt);   // control tick
    void AddKick(int zone, int off, float amp);
    void Render(Rng& rng, const BubbleScheduler& sched, float* L, float* R, int n);   // adds to L / R
    float target = 0, g = 0;    // mute (mask bit 5)
    float d[2] = {0, 0}, kick[2] = {0, 0};
    float Hz(int k) const { return expf(lf_[k]); }
private:
    static constexpr int kMaxKicks = 32;
    float zr_[2], zi_[2], c_[2], s_[2], lf_[2];
    Drift drift_[2];
    int kOff_[kMaxKicks], kZ_[kMaxKicks];
    float kA_[kMaxKicks];
    int nK_ = 0;
    float sr_ = 48000, kG_ = 0, kF_ = 0;
};

// Small waterfalls at the drop: a soft low "plop" (falling tone), an
// entrainment impulse into the drop zone, and 2-8 bubble children (rendered
// by the bubble bank) when Aeration permits. Bounded shared splash energy.
class Falls {
public:
    void Init(float sr, Rng& rng);
    void Update(const FlowState& fs, float activity);       // control tick
    // tuneOn: Notes mode with notes played: the impact fizz follows the last-played notes
    void Run(Rng& rng, int n, FlowState& fs, const BubbleScheduler& sched, EventQueue& q, uint64_t sample, Surface& surf, Cloud& cloud, bool tuneOn);
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
    void Update(const FlowState& fs, Rng& rng, float dt, float eddy, float activity);  // control tick
    void Run(Rng& rng, int n, const BubbleScheduler& sched, bool tuneOn);   // tuneOn: ticks follow the last-played notes
    BubblePool pool;
    float rate = 0, boost = 0;
    uint32_t count = 0;
private:
    Hazard hz_;
    Drift drift_;
    float sr_ = 48000, kB_ = 0, lnHz_ = 1;
};

// Ping-pong delay (TouchPulsar's): the mix (mono) goes into the left line, each
// line feeds the other through a gentle low-pass, so echoes alternate L / R and
// get darker. Time glides when changed. Lines are owned by the caller (SDRAM on
// the Daisy); without them the delay is skipped.
class Delay {
public:
    void Init(float sr) { sr_ = sr; dK_ = 1.0 - (double)expf(-1.f / (ck::DELAY_GLIDE_SEC * sr)); lpK_ = 1.f - expf(-2.f * kPi * ck::DELAY_LP_HZ / sr); }   // float expf: no double libm (flash)
    void SetBuffers(float* l, float* r, size_t len);
    void Reset();
    void Snap() { len_ = target(); }
    void Process(float* L, float* R, int n);
    float time = ck::DELAY_DEFAULT_SEC, amt = 0;
private:
    double target() const { const double t = (double)fmaxf(time, 0.01f) * sr_; return t < (double)(n_ - 4) ? t : (double)(n_ - 4); }
    float tap(const float* b, double d) const;
    float* l_ = nullptr; float* r_ = nullptr;
    size_t n_ = 0, w_ = 0;
    double len_ = 0, dK_ = 0;
    float sr_ = 48000, lpK_ = 0, lpL_ = 0, lpR_ = 0;
};

} // namespace creek
