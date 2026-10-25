// SpatialMixer and OutputStage (small, header-only).
#pragma once
#include "config.h"
#include "dsp.h"

namespace creek {

// SpatialMixer: sums the layer buffers with smooth diagnostic mutes, then
// applies stereo width (mid/side). Zone placement happens per event / band.
class SpatialMixer {
public:
    float g[ck::LAYERS], target[ck::LAYERS];
    float w = 1, wTarget = 1;

    void Init(float sr) {
        kMute_ = 1.f - expf(-1.f / (ck::MUTE_SMOOTH_SEC * sr));
        kW_ = 1.f - expf(-1.f / (ck::UTIL_SMOOTH_SEC * sr));
        for (int l = 0; l < ck::LAYERS; l++) g[l] = target[l] = 1.f;
        w = wTarget;
    }
    void SetMask(uint32_t mask, float width) {
        for (int l = 0; l < ck::LAYERS; l++) target[l] = (mask >> l) & 1u ? 1.f : 0.f;
        wTarget = width;
    }
    void Mix(float* const* bufL, float* const* bufR, float* outL, float* outR, int n) {
        for (int i = 0; i < n; i++) { outL[i] = 0; outR[i] = 0; }
        for (int l = 0; l < ck::LAYERS; l++) {
            const float* bl = bufL[l]; const float* br = bufR[l];
            const float t = target[l];
            float gg = g[l];
            if (gg == 0.f && t == 0.f) continue;
            for (int i = 0; i < n; i++) { gg += kMute_ * (t - gg); outL[i] += bl[i] * gg; outR[i] += br[i] * gg; }
            if (t == 0.f && gg < 1e-5f) gg = 0;
            if (t == 1.f && gg > 0.99999f) gg = 1;
            g[l] = gg;
        }
        float ww = w;
        for (int i = 0; i < n; i++) {
            ww += kW_ * (wTarget - ww);
            const float a = outL[i], b = outR[i], m = 0.5f * (a + b), s = 0.5f * (a - b) * ww;
            outL[i] = m + s; outR[i] = m - s;
        }
        w = ww;
    }

private:
    float kMute_ = 0, kW_ = 0;
};

// OutputStage: smoothed gain, DC blocker (~25 Hz), low-pass filter (TPT SVF as
// TouchPulsar, cutoff gliding ~5 ms), linked peak limiter with instant attack
// and smooth release; output never exceeds LIMIT_CEIL.
class OutputStage {
public:
    float g = 1, gTarget = 1;
    uint32_t limited = 0;   // samples where the limiter reduced gain
    float peak = 0;

    void Init(float sr) {
        sr_ = sr;
        kF_ = fminf(1.f, ck::MICRO / (ck::FILTER_SMOOTH_SEC * sr));
        rDc_ = expf(-kTwoPi * ck::DC_HZ / sr);
        kG_ = 1.f - expf(-1.f / (ck::UTIL_SMOOTH_SEC * sr));
        rel_ = expf(-1.f / (ck::LIMIT_RELEASE_SEC * sr));
        Reset();
    }
    void Reset() {
        xl_ = xr_ = yl_ = yr_ = 0; env_ = 0; g = gTarget; limited = 0; peak = 0;
        f1l_ = f2l_ = f1r_ = f2r_ = 0; fcLog_ = fcT_; Micro();
    }
    // cutoff / resonance 0..1 (cutoff 1 = open)
    void SetFilter(float cut, float res) {
        fcT_ = logf(Clamp(ck::FILTER_HZ[0] * powf(ck::FILTER_HZ[1] / ck::FILTER_HZ[0], cut), 20.f, 0.45f * sr_));
        fk_ = 1.f / (ck::FILTER_Q[0] + res * res * (ck::FILTER_Q[1] - ck::FILTER_Q[0]));
    }
    void SnapFilter() { fcLog_ = fcT_; Micro(); }
    // micro tick: cutoff glides, coefficients follow (tan via sin/cos, as TouchPulsar)
    void Micro() {
        fcLog_ += (fcT_ - fcLog_) * kF_;
        const float w = kPi * expf(fcLog_) / sr_, gg = sinf(w) / cosf(w);
        a1_ = 1.f / (1.f + gg * (gg + fk_)); a2_ = gg * a1_; a3_ = gg * a2_;
    }
    void Process(float* L, float* R, int n) {
        const float ceil = ck::LIMIT_CEIL;
        float xl = xl_, xr = xr_, yl = yl_, yr = yr_, gg = g, env = env_;
        for (int i = 0; i < n; i++) {
            gg += kG_ * (gTarget - gg);
            const float a = L[i] * gg, b = R[i] * gg;
            yl = a - xl + rDc_ * yl; xl = a;
            yr = b - xr + rDc_ * yr; xr = b;
            // low-pass
            float v3 = yl - f2l_, v1 = a1_ * f1l_ + a2_ * v3, v2 = f2l_ + a2_ * f1l_ + a3_ * v3;
            f1l_ = 2.f * v1 - f1l_; f2l_ = 2.f * v2 - f2l_; const float ol = v2;
            v3 = yr - f2r_; v1 = a1_ * f1r_ + a2_ * v3; v2 = f2r_ + a2_ * f1r_ + a3_ * v3;
            f1r_ = 2.f * v1 - f1r_; f2r_ = 2.f * v2 - f2r_; const float orr = v2;
            const float p = fmaxf(fabsf(ol), fabsf(orr));
            env = p > env ? p : env * rel_ + p * (1.f - rel_);
            float k = 1.f;
            if (env > ceil) { k = ceil / env; limited++; }
            L[i] = ol * k; R[i] = orr * k;
            if (p * k > peak) peak = p * k;
        }
        xl_ = xl; xr_ = xr; yl_ = yl; yr_ = yr; g = gg; env_ = env;
    }

private:
    float rDc_ = 0, kG_ = 0, rel_ = 0, sr_ = 48000, kF_ = 1;
    float fcT_ = 0, fcLog_ = 0, fk_ = 1.414f, a1_ = 0, a2_ = 0, a3_ = 0;
    float f1l_ = 0, f2l_ = 0, f1r_ = 0, f2r_ = 0;
    float xl_ = 0, xr_ = 0, yl_ = 0, yr_ = 0, env_ = 0;
};

} // namespace creek
