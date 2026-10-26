#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
#include "layers.h"

namespace creek {

// ---------------------------------------------------------------- eddies
CREEK_SMALL void Eddies::Init(Rng& rng) {
    for (int k = 0; k < N; k++) { active_[k] = false; t_[k] = 0; dur_[k] = 1; peak_[k] = 0; wait_[k] = ck::EDDY_GAP_SEC[1] * rng.Uni(); }
    value = 0;
}

CREEK_SMALL float Eddies::Tick(Rng& rng, float dt, float mul, float depth) {
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
CREEK_SMALL void Falls::Init(float sr, Rng& rng) {
    sr_ = sr; hz_.Init(rng);
    pool.Configure(ck::FALLS_VOICES, ck::FALLS_VOICES + 2, false);
    pool.Init(sr);
    energy_ = 0; count = 0; rate = 0;
    kE_ = expf(-1.f / (ck::CONTROL_HZ * ck::FALLS_ENERGY_SEC));
}

CREEK_SMALL void Falls::Update(const FlowState& fs, float activity) {
    const float E = fs.E[2];
    const float lam = E > 0.002f ? Lerp(ck::FALLS_RATE[0], ck::FALLS_RATE[1], powf(E, ck::FALLS_RATE_EXP)) * fs.gate * activity : 0.f;
    hz_.SetRate(lam, sr_); rate = lam;
    energy_ *= kE_;
}

void Falls::Run(Rng& rng, int n, FlowState& fs, const BubbleScheduler& sched, EventQueue& q, uint64_t sample, Surface& surf, Cloud& cloud, bool tuneOn) {
    const float scaleMul = sched.ScaleMul();
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
        (void)u3;
        // impact: a quick fizz of small bubbles (played by this layer's voices, so P06 mutes it)
        const int nf = CountLerp(ck::FALLS_FIZZ[0], ck::FALLS_FIZZ[1], u1);
        const float lnR = logf(ck::FALLS_FIZZ_RADIUS_MM[1] / ck::FALLS_FIZZ_RADIUS_MM[0]);
        for (int k = 0; k < (int)ck::FALLS_FIZZ[1]; k++) {
            const float d = rng.Uni(), rr = rng.Uni(), fa = rng.Uni(), pj = rng.Bip(), uA = rng.Uni(), uB = rng.Uni();
            if (k >= nf) continue;
            float rMm = ck::FALLS_FIZZ_RADIUS_MM[0] * expf(lnR * rr) * scaleMul;
            if (tuneOn) rMm = 3000.f / (440.f * exp2f((sched.tunedMidi(69.f + 12.f * log2f(3000.f / rMm / 440.f), uA, uB, true) - 69.f) / 12.f));
            Event c;
            c.type = EV_FIZZ; c.zone = 2; c.radiusMm = rMm; c.tauSec = 0; c.rise = 0;
            c.amp = a * ck::FALLS_FIZZ_GAIN * ck::BUBBLE_GAIN * Lerp(0.4f, 1.f, fa) * (1.f - 0.5f * d);
            c.pan = Clamp(pan + 0.1f * pj, -1.f, 1.f);
            q.Push(c, sample + (uint64_t)off + DelaySamples(d, 0.0, ck::FALLS_FIZZ_MS, sr_));
        }
        fs.AddSplash(2, a * ck::FALLS_SPLASH_J);
        cloud.AddKick(2, off, (u2 < 0.5f ? -1.f : 1.f) * a * ck::CLOUD_SPLASH * cloud.kick[1]);
        surf.boost += a * ck::SURF_SPLASH;
        const int nc = A > 0.02f ? (int)fminf(ck::FALLS_CHILDREN[1], (float)CountLerp(ck::FALLS_CHILDREN[0], ck::FALLS_CHILDREN[1], (double)A * (double)u4)) : 0;
        for (int k = 0; k < (int)ck::FALLS_CHILDREN[1]; k++) {
            const float d = rng.Uni(), ca = rng.Uni(), pj = rng.Bip();
            if (k >= nc) continue;
            Event c;
            c.type = EV_BUBBLE; c.zone = 2; c.radiusMm = 0; c.tauSec = 0; c.rise = -1;
            c.amp = a * ck::FALLS_CHILD_GAIN * Lerp(ck::FALLS_CHILD_AMP[0], ck::FALLS_CHILD_AMP[1], ca);
            c.pan = Clamp(pan + 0.12f * pj, -1.f, 1.f);
            q.Push(c, sample + (uint64_t)off + DelaySamples(d, ck::FALLS_CHILD_MS[0], ck::FALLS_CHILD_MS[1], sr_));
        }
    }
}

// ---------------------------------------------------------------- surface
CREEK_SMALL void Surface::Init(float sr, Rng& rng) {
    sr_ = sr; hz_.Init(rng);
    pool.Configure(ck::SURF_VOICES, ck::SURF_VOICES + 2, false);
    pool.Init(sr);
    boost = 0; count = 0; rate = 0;
    drift_.Init(rng, 0.3f, 2.5f);
    kB_ = expf(-1.f / (ck::CONTROL_HZ * ck::SURF_BOOST_SEC));
    lnHz_ = logf(ck::SURF_HZ[1] / ck::SURF_HZ[0]);
}

CREEK_SMALL void Surface::Update(const FlowState& fs, Rng& rng, float dt, float eddy, float activity) {
    const float d = drift_.Tick(rng, dt, 1.f);
    const float Tm = ck::SURF_ZONE_W[0] * fs.T[0] + ck::SURF_ZONE_W[1] * fs.T[1] + ck::SURF_ZONE_W[2] * fs.T[2];
    const float x = Clamp(1.6f * Tm + 0.3f * eddy + boost, 0.f, 1.f);
    const float lam = fs.gate > 0.f ? Lerp(ck::SURF_RATE[0], ck::SURF_RATE[1], x) * (0.35f + 0.65f * 0.5f * (d + 1.f)) * fs.gate * activity : 0.f;
    hz_.SetRate(lam, sr_); rate = lam;
    boost *= kB_;
}

void Surface::Run(Rng& rng, int n, const BubbleScheduler& sched, bool tuneOn) {
    hz_.Run(rng, n);
    for (int b = 0; b < hz_.n; b++) {
        const float u0 = rng.Uni(), u1 = rng.Uni(), u2 = rng.Uni(), u3 = rng.Uni(), u4 = rng.Uni(), u5 = rng.Bip(), u6 = rng.Bip();
        const float uA = rng.Uni(), uB = rng.Uni();
        const int z = u0 < ck::SURF_ZONE_W[0] ? 0 : (u0 < ck::SURF_ZONE_W[0] + ck::SURF_ZONE_W[1] ? 1 : 2);
        float f = ck::SURF_HZ[0] * expf(lnHz_ * u1);
        if (tuneOn) f = 440.f * exp2f((sched.tunedMidi(69.f + 12.f * log2f(f / 440.f), uA, uB, true) - 69.f) / 12.f);
        const float amp = ck::SURF_GAIN * powf(10.f, -ck::SURF_DB_RANGE * powf(u2, ck::SURF_SKEW) / 20.f);
        pool.Spawn(f, amp, Lerp(ck::SURF_TAU_SEC[0], ck::SURF_TAU_SEC[1], u3), Lerp(ck::SURF_ATTACK_SEC[0], ck::SURF_ATTACK_SEC[1], u4),
                   tuneOn ? 0.f : 0.08f * u5, ck::ZONE_PAN[z] + ck::ZONE_JITTER[z] * u6, hz_.off[b]);
        count++;
    }
}

// ---------------------------------------------------------------- bubble cloud
CREEK_SMALL void Cloud::Init(float sr, Rng& rng) {
    sr_ = sr; g = 0; nK_ = 0;
    for (int k = 0; k < 2; k++) {
        zr_[k] = zi_[k] = 0; c_[k] = s_[k] = 0; d[k] = kick[k] = 0;
        drift_[k].Init(rng, 0.5f, 3.f); lf_[k] = logf(ck::CLOUD_HZ);
    }
    kG_ = 1.f - expf(-1.f / (ck::MUTE_SMOOTH_SEC * sr));
    kF_ = 1.f - expf(-1.f / (ck::CONTROL_HZ * 0.15f));
}

CREEK_SMALL void Cloud::Update(Rng& rng, const BubbleScheduler& sched, float dt) {
    for (int k = 0; k < 2; k++) {
        const float x = Clamp((sched.rate[k + 1] / ck::ZONE_RATE_W[k + 1] - ck::CLOUD_RATE[0]) / (ck::CLOUD_RATE[1] - ck::CLOUD_RATE[0]), 0.f, 1.f);
        const float dd = x * x * (3.f - 2.f * x);
        d[k] = dd;
        kick[k] = dd * ck::CLOUD_GAIN * sched.AmpScale() / ck::BUBBLE_GAIN;
        const float dr = drift_[k].Tick(rng, dt, 1.f);
        const float f = Clamp(ck::CLOUD_HZ * (1.f - ck::CLOUD_DENSE_DROP * dd) * (1.f + ck::CLOUD_DRIFT * dr) / powf(sched.ScaleMul(), ck::CLOUD_SCALE_EXP),
                              ck::CLOUD_HZ_RANGE[0], ck::CLOUD_HZ_RANGE[1]);
        lf_[k] += kF_ * (logf(f) - lf_[k]);
        const float fc = expf(lf_[k]), w = kTwoPi * fc / sr_, r = expf(-kPi * fc / (ck::CLOUD_Q * sr_));
        c_[k] = r * cosf(w); s_[k] = r * sinf(w);
    }
}

CREEK_SMALL void Cloud::AddKick(int zone, int off, float amp) {
    if (zone < 1 || nK_ >= kMaxKicks) return;
    kOff_[nK_] = off; kZ_[nK_] = zone - 1; kA_[nK_] = amp; nK_++;
}

void Cloud::Render(Rng& rng, const BubbleScheduler& sched, float* L, float* R, int n) {
    for (int b = 0; b < sched.nBorn; b++) {
        const int z = sched.bornZone[b];
        const float u = rng.Bip();
        if (z >= 1) AddKick(z, sched.bornOff[b], u * kick[z - 1]);
    }
    if (g == 0.f && target == 0.f) { nK_ = 0; zr_[0] = zr_[1] = zi_[0] = zi_[1] = 0; return; }
    for (int i = 1; i < nK_; i++) {          // splash kicks came first: sort by time
        const int o = kOff_[i], z = kZ_[i]; const float a = kA_[i];
        int j = i;
        while (j > 0 && kOff_[j - 1] > o) { kOff_[j] = kOff_[j - 1]; kZ_[j] = kZ_[j - 1]; kA_[j] = kA_[j - 1]; j--; }
        kOff_[j] = o; kZ_[j] = z; kA_[j] = a;
    }
    float pl[2], pr[2];
    for (int k = 0; k < 2; k++) {
        const float p = (Clamp(ck::ZONE_PAN[k + 1] * ck::CLOUD_PAN, -1.f, 1.f) + 1.f) * 0.25f * kPi;
        pl[k] = cosf(p); pr[k] = sinf(p);
    }
    float gg = g;
    int q = 0;
    for (int i = 0; i < n; i++) {
        while (q < nK_ && kOff_[q] <= i) { zr_[kZ_[q]] += kA_[q]; q++; }
        gg += kG_ * (target - gg);
        float yl = 0, yr = 0;
        for (int k = 0; k < 2; k++) {
            const float zr = zr_[k], zi = zi_[k];
            zr_[k] = zr * c_[k] - zi * s_[k]; zi_[k] = zr * s_[k] + zi * c_[k];
            yl += zi_[k] * pl[k]; yr += zi_[k] * pr[k];
        }
        L[i] += yl * gg; R[i] += yr * gg;
    }
    if (target == 0.f && gg < 1e-5f) gg = 0;
    g = gg; nK_ = 0;
}

} // namespace creek
