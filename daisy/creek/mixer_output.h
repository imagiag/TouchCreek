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

// OutputStage: DC blocker (~25 Hz), smoothed gain, linked peak limiter with
// instant attack and smooth release; output never exceeds LIMIT_CEIL.
class OutputStage {
public:
    float g = 1, gTarget = 1;
    uint32_t limited = 0;   // samples where the limiter reduced gain
    float peak = 0;

    void Init(float sr) {
        rDc_ = expf(-kTwoPi * ck::DC_HZ / sr);
        kG_ = 1.f - expf(-1.f / (ck::UTIL_SMOOTH_SEC * sr));
        rel_ = expf(-1.f / (ck::LIMIT_RELEASE_SEC * sr));
        Reset();
    }
    void Reset() { xl_ = xr_ = yl_ = yr_ = 0; env_ = 0; g = gTarget; limited = 0; peak = 0; }
    void Process(float* L, float* R, int n) {
        const float ceil = ck::LIMIT_CEIL;
        float xl = xl_, xr = xr_, yl = yl_, yr = yr_, gg = g, env = env_;
        for (int i = 0; i < n; i++) {
            gg += kG_ * (gTarget - gg);
            const float a = L[i] * gg, b = R[i] * gg;
            yl = a - xl + rDc_ * yl; xl = a;
            yr = b - xr + rDc_ * yr; xr = b;
            const float p = fmaxf(fabsf(yl), fabsf(yr));
            env = p > env ? p : env * rel_ + p * (1.f - rel_);
            float k = 1.f;
            if (env > ceil) { k = ceil / env; limited++; }
            L[i] = yl * k; R[i] = yr * k;
            if (p * k > peak) peak = p * k;
        }
        xl_ = xl; xr_ = xr; yl_ = yl; yr_ = yr; g = gg; env_ = env;
    }

private:
    float rDc_ = 0, kG_ = 0, rel_ = 0;
    float xl_ = 0, xr_ = 0, yl_ = 0, yr_ = 0, env_ = 0;
};

} // namespace creek
