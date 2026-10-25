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
    over = 0; nBorn = 0; ampScale_ = 0; scaleMul_ = 1;
}

void BubbleScheduler::Update(const FlowState& fs, float S) {
    float total = 0;
    for (int z = 0; z < ck::ZONES; z++) {
        const float B = fs.B[z];
        const float lam = B > ck::B_MIN ? ck::RATE_MAX * ck::ZONE_RATE_W[z] * powf(B, ck::RATE_EXP) * fs.gate : 0.f;
        rate[z] = lam; inc_[z] = lam / sr_; total += lam;
    }
    scaleMul_ = ck::SCALE_MUL_MIN * powf(ck::SCALE_MUL_MAX / ck::SCALE_MUL_MIN, S);
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

bool BubbleScheduler::Make(Rng& rngP, int z, BubblePool& pool, int off, const Event* ev) {
    const float u0 = rngP.Uni(), u1 = rngP.Uni(), u2 = rngP.Uni(), u3 = rngP.Uni(), u4 = rngP.Uni();
    (void)rngP.Uni(); // u5 reserved (keeps the stream layout stable for later parameters)
    const float u6 = rngP.Bip(), u7 = rngP.Bip();
    const float logr = lnRmin_ + lnRspan_ * powf(u0, ck::R_SKEW);
    float rMm = expf(logr) * scaleMul_;
    if (ev && ev->radiusMm > 0) rMm = ev->radiusMm;
    const float f0 = fmaxf(ck::F_MIN_HZ, 3000.f / rMm);                 // f0 = 3 / r
    const float x = Clamp((logf(rMm) - lnRmin_) / lnRspan_, 0.f, 1.f);
    float tau = ck::TAU_MIN * expf(lnTauSpan_ * x) * (1.f + ck::TAU_JITTER * u6);
    if (ev && ev->tauSec > 0) tau = ev->tauSec;
    float amp = powf(10.f, -ck::AMP_DB_RANGE * powf(u1, ck::AMP_SKEW) / 20.f) *
                fminf(ck::AMP_R_MAX, powf(rMm, ck::AMP_R_EXP)) * ampScale_;
    if (ev && ev->amp > 0) amp = ev->amp * ck::BUBBLE_GAIN;
    const float attack = ck::ATTACK_MIN + (ck::ATTACK_MAX - ck::ATTACK_MIN) * u2;
    float rise = u3 < ck::RISE_PROB ? ck::RISE_MAX * u4 : 0.f;
    if (ev && ev->rise >= 0) rise = ev->rise;
    float pan = ck::ZONE_PAN[z] + ck::ZONE_JITTER[z] * u7;
    if (ev && ev->pan >= -1) pan = ev->pan;
    return pool.Spawn(f0, amp, tau, attack, rise, pan, off);
}

} // namespace creek
