#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
#include "flow_bed.h"

namespace creek {

void FlowBed::Init(float sr, Rng& rngD) {
    sr_ = sr;
    aM_ = sqrtf(ck::MID_CORR); bM_ = sqrtf(1.f - ck::MID_CORR);
    aH_ = sqrtf(ck::HIGH_CORR); bH_ = sqrtf(1.f - ck::HIGH_CORR);
    hp_.Reset(); body_.Reset(); midL_.Reset(); midR_.Reset(); hiL_.Reset(); hiR_.Reset();
    hp_.Set(ck::BODY_HP_HZ, sr);
    dBody_.Init(rngD, 0.4f, 3.0f); dMid_.Init(rngD, 0.3f, 2.5f); dHi_.Init(rngD, 0.3f, 2.0f); dCut_.Init(rngD, 0.5f, 4.0f);
    flMid_.Init(rngD, ck::FLUTTER_MIN_SEC, ck::FLUTTER_MAX_SEC); flHi_.Init(rngD, ck::FLUTTER_MIN_SEC, ck::FLUTTER_MAX_SEC);
    for (int i = 0; i < 3; i++) g_[i] = gStep_[i] = 0;
    body_.Set(ck::BODY_LP_HZ[0], ck::BODY_Q, sr); midL_.Set(ck::MID_HZ[0], ck::MID_Q, sr); midR_.Set(ck::MID_HZ[0], ck::MID_Q, sr);
    hiL_.Set(ck::HIGH_HZ[0], ck::HIGH_Q, sr); hiR_.Set(ck::HIGH_HZ[0], ck::HIGH_Q, sr);
}

void FlowBed::Update(const FlowState& fs, Rng& rngD, float dt, int span) {
    const float R = fs.m[1], S = fs.m[2], E = fs.E[0], T = fs.T[0], e0 = fs.eta0.v;
    const float db = dBody_.Tick(rngD, dt, 1.f), dm = dMid_.Tick(rngD, dt, 1.f), dh = dHi_.Tick(rngD, dt, 1.f), dc = dCut_.Tick(rngD, dt, 1.f);
    const float fm = flMid_.Tick(rngD, dt, 1.f), fh = flHi_.Tick(rngD, dt, 1.f);
    const float level = ck::BED_GAIN * powf(E, ck::BED_E_EXP) * fs.gate / (1.f + ck::BED_TOP_SQUASH * E * E);
    const float depth = Lerp(ck::MOD_DEPTH[0], ck::MOD_DEPTH[1], Clamp(0.6f * R + 0.8f * T, 0.f, 1.f));
    const float flut = depth * Clamp(0.3f + 1.5f * T, 0.f, 1.f);
    const float tilt = powf(2.f, -ck::SCALE_TILT * (2.f * S - 1.f));          // larger Scale -> darker
    const float gB = ck::BODY_GAIN * (0.8f + 0.4f * S) * (1.f + depth * (0.6f * e0 + 0.4f * db));
    const float gM = ck::MID_GAIN * (0.5f + 0.7f * R + 1.2f * T) * (1.f + depth * (0.5f * e0 + 0.5f * dm)) * (1.f + flut * fm);
    const float gH = ck::HIGH_GAIN * (0.3f + 1.0f * R + 1.5f * T) * (1.2f - 0.4f * S) * (1.f + depth * (0.4f * e0 + 0.6f * dh)) * (1.f + flut * fh);
    gStep_[0] = (level * fmaxf(0.f, gB) - g_[0]) / span;
    gStep_[1] = (level * fmaxf(0.f, gM) - g_[1]) / span;
    gStep_[2] = (level * fmaxf(0.f, gH) - g_[2]) / span;
    const float cm = powf(2.f, ck::CUT_DEPTH * (0.5f * dc + 0.5f * e0)) * tilt;
    const float tt = Clamp(0.5f * T + 0.5f * R * E, 0.f, 1.f);
    body_.Set(Lerp(ck::BODY_LP_HZ[0], ck::BODY_LP_HZ[1], tt) * cm, ck::BODY_Q, sr_);
    const float fM = Lerp(ck::MID_HZ[0], ck::MID_HZ[1], tt) * cm, fH = Lerp(ck::HIGH_HZ[0], ck::HIGH_HZ[1], tt) * cm;
    midL_.Set(fM, ck::MID_Q, sr_); midR_.Set(fM * 1.03f, ck::MID_Q, sr_);
    hiL_.Set(fH, ck::HIGH_Q, sr_); hiR_.Set(fH * 0.97f, ck::HIGH_Q, sr_);
}

void FlowBed::Render(float* L, float* R, int n, Rng& rc, Rng& rl, Rng& rr) {
    float g0 = g_[0], g1 = g_[1], g2 = g_[2];
    const float s0 = gStep_[0], s1 = gStep_[1], s2 = gStep_[2];
    for (int i = 0; i < n; i++) {
        const float c = rc.Bip(), l = rl.Bip(), r = rr.Bip();
        g0 += s0; g1 += s1; g2 += s2;
        const float yb = body_.Lp(hp_.Process(c)) * g0;
        const float ym1 = midL_.BpN(aM_ * c + bM_ * l) * g1, ym2 = midR_.BpN(aM_ * c + bM_ * r) * g1;
        const float yh1 = hiL_.BpN(aH_ * c + bH_ * l) * g2, yh2 = hiR_.BpN(aH_ * c + bH_ * r) * g2;
        L[i] = yb + ym1 + yh1; R[i] = yb + ym2 + yh2;
    }
    g_[0] = g0; g_[1] = g1; g_[2] = g2;
}

} // namespace creek
