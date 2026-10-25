#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
#include "bubble_scheduler.h"

namespace creek {

void BubbleScheduler::Init(float sr, Rng& rngT) {
    sr_ = sr;
    lnRmin_ = logf(ck::R_MIN_MM); lnRspan_ = logf(ck::R_MAX_MM) - logf(ck::R_MIN_MM);
    lnTauSpan_ = logf(ck::TAU_MAX / ck::TAU_MIN);
    for (int z = 0; z < ck::ZONES; z++) { acc_[z] = 0; thr_[z] = -logf(rngT.UniPos()); inc_[z] = 0; rate[z] = 0; }
    over = 0; nBorn = 0; ampScale_ = 0; scaleMul_ = 1; nTune = 0; tuneAmt = 0; freeFlow = 1.f; activity = 1.f;
    for (int z = 0; z < ck::ZONES; z++) { rateMul[z] = 1; radMul[z] = 1; }
}

void BubbleScheduler::Update(const FlowState& fs, float S) {
    float total = 0;
    for (int z = 0; z < ck::ZONES; z++) {
        const float B = fs.B[z];
        const float lam = B > ck::B_MIN ? ck::RATE_MAX * ck::ZONE_RATE_W[z] * powf(B, ck::RATE_EXP) * fs.gate * rateMul[z] * activity : 0.f;
        rate[z] = lam; inc_[z] = lam / sr_; total += lam;
    }
    // 0.6x .. 1.8x, bending up to 4x at the very top of the knob (bass)
    scaleMul_ = ck::SCALE_MUL_MIN * powf(ck::SCALE_MUL_MAX / ck::SCALE_MUL_MIN, S) *
                powf(ck::SCALE_MUL_TOP / ck::SCALE_MUL_MAX, powf(S, ck::SCALE_TOP_EXP));
    ampScale_ = ck::BUBBLE_GAIN / sqrtf(1.f + total / ck::DENSITY_REF);
}

void BubbleScheduler::Run(Rng& rngT, int n) {
    nBorn = 0;
    for (int z = 0; z < ck::ZONES; z++) {
        const float inc = inc_[z];
        if (inc <= 0.f) continue;
        float acc = acc_[z];
        for (int i = 0; i < n; i++) {
            acc += inc;
            if (acc >= thr_[z]) {
                acc -= thr_[z];
                thr_[z] = -logf(rngT.UniPos());
                if (nBorn < ck::MAX_BIRTHS_PER_MICRO) { bornZone[nBorn] = z; bornOff[nBorn] = i; nBorn++; }
                else over++;
            }
        }
        acc_[z] = acc;
    }
}

float BubbleScheduler::TauFor(float rMm) const {
    const float x = Clamp((logf(rMm) - lnRmin_) / lnRspan_, 0.f, ck::TAU_X_MAX);
    return ck::TAU_MIN * expf(lnTauSpan_ * x);
}

// Move d steps through a scale (12-bit pitch-class mask, bit 0 = C) from MIDI note n.
static int stepScale(int n, int d, uint32_t mask) {
    const int dir = d > 0 ? 1 : -1, steps = d < 0 ? -d : d;
    for (int k = 0; k < steps; k++) {
        int g = 0;
        do { n += dir; g++; } while (!((mask >> (((n % 12) + 12) % 12)) & 1u) && g < 12);
    }
    return n;
}

// Pitch for a tuned bubble (MIDI). Default: the held note in the octave nearest
// the bubble's own pitch. Variety left of noon pulls it onto the held note
// itself (fully left: one note); right of noon moves it up to +-7 scale steps.
float BubbleScheduler::tunedMidi(float m0, float uA, float uB) const {
    int best = 0; float bd = 1e9f, bc = 0;
    for (int k = 0; k < nTune; k++) {
        const float t = tuneMidi[k], c = t + 12.f * roundf((m0 - t) / 12.f), d = fabsf(c - m0);
        if (d < bd) { bd = d; best = k; bc = c; }
    }
    const float v = variety;
    if (v < 0.5f) {
        if (uA < (0.5f - v) * 2.f) bc = tuneMidi[best];
    } else if (v > 0.5f) {
        const float r = (v - 0.5f) * 2.f;
        if (uA < r) {
            const int D = 1 + (int)roundf(r * (ck::VARIETY_STEPS - 1.f));
            int d = (int)floorf(uB * (2 * D + 1)) - D; if (d > D) d = D;
            bc = (float)stepScale((int)roundf(bc), d, scaleMask);
        }
    }
    while (bc > ck::TUNE_MIDI_RANGE[1]) bc -= 12.f;
    while (bc < ck::TUNE_MIDI_RANGE[0]) bc += 12.f;
    return bc;
}

bool BubbleScheduler::Make(Rng& rngP, int z, BubblePool& pool, int off, const Event* ev) {
    const float u0 = rngP.Uni(), u1 = rngP.Uni(), u2 = rngP.Uni(), u3 = rngP.Uni(), u4 = rngP.Uni(), u5 = rngP.Uni();
    const float u6 = rngP.Bip(), u7 = rngP.Bip();
    const float u8 = rngP.Uni(), u9 = rngP.Uni();
    const float logr = lnRmin_ + lnRspan_ * powf(u0, rSkew);
    float rMm = expf(logr) * scaleMul_ * radMul[z] * toneMul;
    if (ev && ev->radiusMm > 0) rMm = ev->radiusMm;
    float f0 = fmaxf(ck::F_MIN_HZ, 3000.f / rMm);                       // f0 = 3 / r
    const float tauBase = TauFor(rMm);
    float tau = tauBase * (1.f + ck::TAU_JITTER * u6);
    if (ev && ev->tauSec > 0) tau = ev->tauSec;
    // note mode: held notes gather the creek's bubbles; with Free flow down every bubble is poured, so tuned
    const float tuneP = tuneAmt > 0.01f ? fminf(1.f, tuneAmt * ck::TUNE_MAX + (1.f - freeFlow)) : 0.f;
    const bool tuned = !(ev && ev->radiusMm > 0) && nTune > 0 && u5 < tuneP;   // splash bubbles too; tuned drops keep their pitch
    if (tuned) {
        const float m = tunedMidi(69.f + 12.f * log2f(f0 / 440.f), u8, u9);
        f0 = fmaxf(ck::F_MIN_HZ, 440.f * exp2f((m - 69.f) / 12.f)); tau *= ck::TUNE_TAU_MUL;
    }
    float amp = powf(10.f, -ck::AMP_DB_RANGE * powf(u1, ck::AMP_SKEW) / 20.f) *
                fminf(ck::AMP_R_MAX, powf(rMm, ck::AMP_R_EXP)) * ampScale_;
    if (tauBase > ck::TAU_MAX) amp *= sqrtf(ck::TAU_MAX / tauBase);   // deep bubbles ring longer: same energy
    if (ev && ev->amp > 0) amp = ev->amp * ck::BUBBLE_GAIN;
    const float attack = ck::ATTACK_MIN + (ck::ATTACK_MAX - ck::ATTACK_MIN) * u2;
    float rise = u3 < ck::RISE_PROB && !tuned ? ck::RISE_MAX * u4 : 0.f;
    if (ev && ev->rise >= 0) rise = ev->rise;
    float pan = ck::ZONE_PAN[z] + ck::ZONE_JITTER[z] * u7;
    if (ev && ev->pan >= -1) pan = ev->pan;
    return pool.Spawn(f0, amp, tau, attack, rise, pan, off);
}

} // namespace creek
