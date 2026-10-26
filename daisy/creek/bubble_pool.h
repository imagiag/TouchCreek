// BubblePool: fixed slots of damped sinusoids y = A exp(-t/tau) sin(phi(t))
// (van den Doel). Each voice is a decaying complex phasor rotated once per
// sample; pitch glides update the rotation at micro ticks, so phase stays
// continuous. VOICES is the budget; spare slots up to VOICE_SLOTS carry the
// 2 ms fade of stolen voices.
#pragma once
#include <stdint.h>
#include "config.h"
#include "dsp.h"

namespace creek {

constexpr int kLog = 64; // bubble birth log (displays, LED)

class BubblePool {
public:
    void Init(float sr);
    // budget = voices sounding at once; slots (<= VOICE_SLOTS) = budget + room for steal fades
    void Configure(int budget, int slots, bool log) { budget_ = budget; n_ = slots < N ? slots : N; log_ = log; }
    void Reset();
    bool Spawn(float f0, float amp, float tau, float attack, float rise, float pan, int off, bool user = false);
    void FadeCreek(float k);   // scene gate closing: the creek's own voices fade (not drops / stones)
    void Micro();
    void Render(float* L, float* R, int n);
    int Active() const;
    int ActiveUser() const;    // played voices (drops, stones)

    // stats
    uint32_t births = 0, drops = 0, steals = 0;
    int maxActive = 0;
    float tauMul = 1.f;      // Decay knob
    float sumLog2F = 0;
    // birth log
    float logF[kLog], logA[kLog], logP[kLog];
    volatile uint32_t logW = 0;

private:
    static constexpr int N = ck::VOICE_SLOTS;
    int budget_ = ck::VOICES, n_ = N;
    bool log_ = true;
    float sr_ = 48000, wMax_ = 0, fadeInc_ = 0, lifeLn_ = 0, retireAbs_ = 0;
    uint8_t on_[N], fading_[N], user_[N];
    float zr_[N], zi_[N], c_[N], s_[N], damp_[N], w0_[N], rise_[N], riseInc_[N];
    float att_[N], ka_[N], gl_[N], gr_[N], fade_[N], fadeStep_[N];
    int32_t life_[N], age_[N], start_[N];
    int ord_[N], nOrd_ = 0;    // voices in birth order: summing order doesn't depend on slots / block size
    void compact() { int k = 0; for (int i = 0; i < nOrd_; i++) if (on_[ord_[i]]) ord_[k++] = ord_[i]; nOrd_ = k; }
};

} // namespace creek
