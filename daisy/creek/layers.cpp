#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
#include "layers.h"

namespace creek {

// ---------------------------------------------------------------- eddies
void Eddies::Init(Rng& rng) {
    for (int k = 0; k < N; k++) { active_[k] = false; t_[k] = 0; dur_[k] = 1; peak_[k] = 0; wait_[k] = ck::EDDY_GAP_SEC[1] * rng.Uni(); }
    value = 0;
}

float Eddies::Tick(Rng& rng, float dt, float mul, float depth) {
    float sum = 0;
    for (int k = 0; k < N; k++) {
        if (!active_[k]) {
            wait_[k] -= dt;
            if (wait_[k] <= 0.f) {
                const float u0 = rng.Uni(), u1 = rng.Uni();
                active_[k] = true; t_[k] = 0;
                dur_[k] = Lerp(ck::EDDY_DUR_SEC[0], ck::EDDY_DUR_SEC[1], u0) * mul;
                peak_[k] = 0.4f + 0.6f * u1;
            }
        }
        if (active_[k]) {
            t_[k] += dt;
            if (t_[k] >= dur_[k]) {
                active_[k] = false;
                wait_[k] = Lerp(ck::EDDY_GAP_SEC[0], ck::EDDY_GAP_SEC[1], rng.Uni()) * mul;
            } else {
                const float s = sinf(kPi * t_[k] / dur_[k]);
                sum += peak_[k] * s * s;
            }
        }
    }
    value = Clamp(sum, 0.f, 1.f) * depth;
    return value;
}

// ---------------------------------------------------------------- falls
void Falls::Init(float sr, Rng& rng) {
    sr_ = sr; hz_.Init(rng);
    pool.Configure(ck::FALLS_VOICES, ck::FALLS_VOICES + 2, false);
    pool.Init(sr);
    energy_ = 0; count = 0; rate = 0;
    kE_ = expf(-1.f / (ck::CONTROL_HZ * ck::FALLS_ENERGY_SEC));
}

void Falls::Update(const FlowState& fs) {
    const float E = fs.E[2];
    const float lam = E > 0.002f ? Lerp(ck::FALLS_RATE[0], ck::FALLS_RATE[1], powf(E, ck::FALLS_RATE_EXP)) * fs.gate : 0.f;
    hz_.SetRate(lam, sr_); rate = lam;
    energy_ *= kE_;
}

void Falls::Run(Rng& rng, int n, FlowState& fs, float scaleMul, EventQueue& q, uint64_t sample, Surface& surf) {
    hz_.Run(rng, n);
    const float A = fs.m[3], E = fs.E[2], R = fs.m[1];
    for (int b = 0; b < hz_.n; b++) {
        const int off = hz_.off[b];
        const float u0 = rng.Uni(), u1 = rng.Uni(), u2 = rng.Uni(), u3 = rng.Uni(), u4 = rng.Uni(), u5 = rng.Bip();
        float a = Lerp(0.55f, 0.15f, R) + Lerp(0.45f, 0.85f, R) * u0;   // rougher water: more uneven splashes
        a *= 0.5f + 0.5f * E;
        a /= 1.f + ck::FALLS_ENERGY_K * energy_;
        energy_ += a;
        count++;
        const float pan = ck::ZONE_PAN[2] + ck::ZONE_JITTER[2] * u5;
        const float f = Lerp(ck::FALLS_PLOP_HZ[0], ck::FALLS_PLOP_HZ[1], u1) / scaleMul;
        pool.Spawn(f, a * ck::FALLS_PLOP_GAIN, Lerp(ck::FALLS_TAU_SEC[0], ck::FALLS_TAU_SEC[1], u2),
                   Lerp(ck::FALLS_ATTACK_SEC[0], ck::FALLS_ATTACK_SEC[1], u3), ck::FALLS_PLOP_GLIDE, pan, off);
        fs.AddSplash(2, a * ck::FALLS_SPLASH_J);
        surf.boost += a * ck::SURF_SPLASH;
        const int nc = A > 0.02f ? (int)fminf(ck::FALLS_CHILDREN[1], roundf(Lerp(ck::FALLS_CHILDREN[0], ck::FALLS_CHILDREN[1], A * u4))) : 0;
        for (int k = 0; k < (int)ck::FALLS_CHILDREN[1]; k++) {
            const float d = rng.Uni(), ca = rng.Uni(), pj = rng.Bip();
            if (k >= nc) continue;
            Event c;
            c.type = EV_BUBBLE; c.zone = 2; c.radiusMm = 0; c.tauSec = 0; c.rise = -1;
            c.amp = a * ck::FALLS_CHILD_GAIN * Lerp(ck::FALLS_CHILD_AMP[0], ck::FALLS_CHILD_AMP[1], ca);
            c.pan = Clamp(pan + 0.12f * pj, -1.f, 1.f);
            q.Push(c, sample + (uint64_t)off + (uint64_t)lroundf(Lerp(ck::FALLS_CHILD_MS[0], ck::FALLS_CHILD_MS[1], d) * 0.001f * sr_));
        }
    }
}

// ---------------------------------------------------------------- surface
void Surface::Init(float sr, Rng& rng) {
    sr_ = sr; hz_.Init(rng);
    pool.Configure(ck::SURF_VOICES, ck::SURF_VOICES + 2, false);
    pool.Init(sr);
    boost = 0; count = 0; rate = 0;
    drift_.Init(rng, 0.3f, 2.5f);
    kB_ = expf(-1.f / (ck::CONTROL_HZ * ck::SURF_BOOST_SEC));
    lnHz_ = logf(ck::SURF_HZ[1] / ck::SURF_HZ[0]);
}

void Surface::Update(const FlowState& fs, Rng& rng, float dt, float eddy) {
    const float d = drift_.Tick(rng, dt, 1.f);
    const float Tm = ck::SURF_ZONE_W[0] * fs.T[0] + ck::SURF_ZONE_W[1] * fs.T[1] + ck::SURF_ZONE_W[2] * fs.T[2];
    const float x = Clamp(1.6f * Tm + 0.3f * eddy + boost, 0.f, 1.f);
    const float lam = fs.gate > 0.f ? Lerp(ck::SURF_RATE[0], ck::SURF_RATE[1], x) * (0.35f + 0.65f * 0.5f * (d + 1.f)) * fs.gate : 0.f;
    hz_.SetRate(lam, sr_); rate = lam;
    boost *= kB_;
}

void Surface::Run(Rng& rng, int n) {
    hz_.Run(rng, n);
    for (int b = 0; b < hz_.n; b++) {
        const float u0 = rng.Uni(), u1 = rng.Uni(), u2 = rng.Uni(), u3 = rng.Uni(), u4 = rng.Uni(), u5 = rng.Bip(), u6 = rng.Bip();
        const int z = u0 < ck::SURF_ZONE_W[0] ? 0 : (u0 < ck::SURF_ZONE_W[0] + ck::SURF_ZONE_W[1] ? 1 : 2);
        const float f = ck::SURF_HZ[0] * expf(lnHz_ * u1);
        const float amp = ck::SURF_GAIN * powf(10.f, -ck::SURF_DB_RANGE * powf(u2, ck::SURF_SKEW) / 20.f);
        pool.Spawn(f, amp, Lerp(ck::SURF_TAU_SEC[0], ck::SURF_TAU_SEC[1], u3), Lerp(ck::SURF_ATTACK_SEC[0], ck::SURF_ATTACK_SEC[1], u4),
                   0.08f * u5, ck::ZONE_PAN[z] + ck::ZONE_JITTER[z] * u6, hz_.off[b]);
        count++;
    }
}

} // namespace creek
