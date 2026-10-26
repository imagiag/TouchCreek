// Small DSP building blocks shared by the Creek modules (mirrors creek-engine.js).
#pragma once
#include <stdint.h>
#include <math.h>

namespace creek {

// setup and control-rate code is compiled for size (flash is tight); per-sample loops keep -O3
#if defined(__GNUC__) && !defined(__clang__)
#define CREEK_SMALL __attribute__((optimize("Os")))
#else
#define CREEK_SMALL
#endif

constexpr float kTwoPi = 6.283185307179586f;
constexpr float kPi = 3.14159265358979f;

// Delay in samples for a random u in [0, 1) across [ms0, ms1): integer-exact, same double
// maths as crDelay() in the JS engine, so both queue delayed events at the same sample.
// (all values are >= 0: casts do the rounding / flooring, no double libm needed on the Daisy)
inline uint32_t DelaySamples(double u, double ms0, double ms1, double sr) {
    const uint32_t a = (uint32_t)(ms0 * sr / 1000.0 + 0.5), span = (uint32_t)((ms1 - ms0) * sr / 1000.0 + 0.5);
    return a + (uint32_t)(u * (double)span);
}
// Event counts: lo + (hi - lo) x rounded, in double maths identical to crCount() in the JS engine
inline int CountLerp(double lo, double hi, double x) { return (int)(lo + (hi - lo) * x + 0.5); }
inline float Clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// mulberry32: 32-bit integer ops, identical to the JS version
struct Rng {
    uint32_t s = 0;
    void Seed(uint32_t v) { s = v; }
    uint32_t U32() {
        uint32_t t = (s += 0x6D2B79F5u);
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return t ^ (t >> 14);
    }
    float Uni() { return (float)(U32() >> 8) * 5.9604644775390625e-8f; }                 // [0, 1)
    float UniPos() { return (float)((U32() >> 9) * 2u + 1u) * 5.9604644775390625e-8f; } // (0, 1)
    float Bip() { return (float)(U32() >> 8) * 1.1920928955078125e-7f - 1.f; }          // [-1, 1)
};

inline uint32_t Hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
inline uint32_t StreamSeed(uint32_t seed, uint32_t stream) { return Hash(seed + (stream + 1u) * 0x9E3779B9u); }

// stream ids (fixed: muting a layer never shifts another layer's randomness;
// S_NOISE_* / S_BED_DRIFT are unused since the noise wash was removed, kept so ids don't move)
enum Stream { S_DRIFT, S_BTIME, S_BPARAM, S_NOISE_C, S_NOISE_L, S_NOISE_R, S_BED_DRIFT,
              S_EDDY, S_FALLS, S_SURFACE, S_TRIGGER,
              S_BTIME1, S_BTIME2, S_BPARAM1, S_BPARAM2, S_FCHILD, S_CLOUD, S_COUNT };
// each zone has its own bubble timing and parameter streams, so a one-sample timing
// difference in one zone can't reshuffle the others' random draws
constexpr int kZoneBTime[3] = {S_BTIME, S_BTIME1, S_BTIME2};
constexpr int kZoneBParam[3] = {S_BPARAM, S_BPARAM1, S_BPARAM2};

// Smooth bounded random drift in [-1, 1]: random targets, smoothstep between them.
struct Drift {
    float from = 0, to = 0, t = 0, dur = 1, v = 0, minS = 1, maxS = 1;
    void Init(Rng& rng, float minSec, float maxSec) {
        minS = minSec; maxS = maxSec;
        from = rng.Bip(); to = rng.Bip();
        dur = minSec + (maxSec - minSec) * rng.Uni();
        t = 0; v = from;
    }
    float Tick(Rng& rng, float dt, float timeMul) {
        t += dt;
        if (t >= dur) {
            t -= dur;
            from = to; to = rng.Bip();
            dur = (minS + (maxS - minS) * rng.Uni()) * timeMul;
            if (t > dur) t = dur;
        }
        const float x = t / dur, s = x * x * (3.f - 2.f * x);
        v = from + (to - from) * s;
        return v;
    }
};

} // namespace creek
