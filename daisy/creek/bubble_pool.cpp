#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
#include "bubble_pool.h"

namespace creek {

void BubblePool::Init(float sr) {
    sr_ = sr;
    wMax_ = kTwoPi * ck::F_MAX_RATIO;
    fadeInc_ = 1.f / (ck::STEAL_FADE_SEC * sr);
    lifeLn_ = logf(powf(10.f, -ck::RETIRE_DB / 20.f));
    retireAbs_ = powf(10.f, ck::RETIRE_ABS_DB / 20.f);
    Reset();
}

void BubblePool::Reset() {
    for (int i = 0; i < n_; i++) { on_[i] = fading_[i] = 0; zr_[i] = zi_[i] = 0; }
    births = drops = steals = 0; maxActive = 0; sumLog2F = 0;
}

int BubblePool::Active() const {
    int a = 0;
    for (int i = 0; i < n_; i++) if (on_[i] && !fading_[i]) a++;
    return a;
}

bool BubblePool::Spawn(float f0, float amp, float tau, float attack, float rise, float pan, int off) {
    int act = 0, low = -1;
    float lowE = 1e30f;
    for (int i = 0; i < n_; i++) if (on_[i] && !fading_[i]) {
        act++;
        const float e = zr_[i] * zr_[i] + zi_[i] * zi_[i];
        if (e < lowE) { lowE = e; low = i; }
    }
    if (act >= budget_) {
        if (amp * amp <= lowE) { drops++; return false; }      // quieter than everything: drop it
        fading_[low] = 1; fade_[low] = 1.f; fadeStep_[low] = fadeInc_; steals++;
    }
    int v = -1;
    for (int i = 0; i < n_; i++) if (!on_[i]) { v = i; break; }
    if (v < 0) { drops++; return false; }
    tau *= tauMul;                         // Decay knob
    const float w = fminf(kTwoPi * f0 / sr_, wMax_ / (1.f + fmaxf(0.f, rise)));
    const float r = expf(-1.f / (tau * sr_));
    on_[v] = 1; fading_[v] = 0; fade_[v] = 1.f; fadeStep_[v] = 0;
    zr_[v] = amp; zi_[v] = 0;
    damp_[v] = r; w0_[v] = w;
    c_[v] = r * cosf(w); s_[v] = r * sinf(w);
    rise_[v] = rise; riseInc_[v] = rise != 0.f ? 1.f / (ck::RISE_TIME_TAUS * tau * sr_) : 0.f;
    att_[v] = 0; ka_[v] = 1.f - expf(-3.f / (attack * sr_));
    const float p = (Clamp(pan, -1.f, 1.f) + 1.f) * 0.25f * kPi;
    gl_[v] = cosf(p); gr_[v] = sinf(p);
    // retire at -80 dB re. the event, or earlier once below RETIRE_ABS_DB absolute
    const float lifeLn = fminf(lifeLn_, logf(fmaxf(amp, 1e-9f) / retireAbs_));
    const float lf = ceilf(tau * sr_ * lifeLn);
    life_[v] = lf < 1.f ? 1 : (int32_t)lf; age_[v] = 0; start_[v] = off;
    births++;
    const float fHz = w * sr_ / kTwoPi;
    sumLog2F += log2f(fHz);
    if (log_) {
        const uint32_t j = logW & (kLog - 1);
        logF[j] = fHz; logA[j] = amp; logP[j] = pan; logW = logW + 1;
    }
    const int nowActive = act >= budget_ ? act : act + 1;
    if (nowActive > maxActive) maxActive = nowActive;
    return true;
}

void BubblePool::Micro() {
    for (int v = 0; v < n_; v++) {
        if (!on_[v] || riseInc_[v] == 0.f) continue;
        const float prog = fminf(1.f, age_[v] * riseInc_[v]);
        const float w = w0_[v] * (1.f + rise_[v] * prog), r = damp_[v];
        c_[v] = r * cosf(w); s_[v] = r * sinf(w);
        if (prog >= 1.f) riseInc_[v] = 0;
    }
}

void BubblePool::Render(float* L, float* R, int n) {
    for (int v = 0; v < n_; v++) {
        if (!on_[v]) continue;
        float zr = zr_[v], zi = zi_[v], att = att_[v], fade = fade_[v];
        const float c = c_[v], s = s_[v], ka = ka_[v], gl = gl_[v], gr = gr_[v], fs = fadeStep_[v];
        const int i0 = start_[v];
        int end = i0 + life_[v]; if (end > n) end = n;
        for (int i = i0; i < end; i++) {
            const float nr = zr * c - zi * s; zi = zr * s + zi * c; zr = nr;
            att += ka * (1.f - att);
            fade -= fs;
            const float y = zi * att * (fade > 0.f ? fade : 0.f);
            L[i] += y * gl; R[i] += y * gr;
        }
        const int done = end - i0;
        zr_[v] = zr; zi_[v] = zi; att_[v] = att; fade_[v] = fade;
        life_[v] -= done; age_[v] += done; start_[v] = 0;
        if (life_[v] <= 0 || fade <= 0.f) { on_[v] = 0; fading_[v] = 0; }
    }
}

} // namespace creek
