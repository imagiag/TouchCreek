#include "flow_state.h"

namespace creek {

void FlowState::Init(Rng& rng) {
    dt = 1.f / ck::CONTROL_HZ;
    kE_ = 1.f - expf(-dt / ck::TAU_E); kT_ = 1.f - expf(-dt / ck::TAU_T); kB_ = 1.f - expf(-dt / ck::TAU_B);
    kM_ = 1.f - expf(-dt / ck::MACRO_SMOOTH_SEC);
    kJ_ = expf(-dt / ck::J_DECAY_SEC);
    for (int z = 0; z < ck::ZONES; z++) E[z] = T[z] = B[z] = J[z] = 0;
    gate = 0;
    eta0.Init(rng, ck::ETA_MIN_SEC, ck::ETA_MAX_SEC);
    for (int z = 0; z < ck::ZONES; z++) etaZ[z].Init(rng, ck::ETA_MIN_SEC, ck::ETA_MAX_SEC);
}

void FlowState::SetMacros(float F, float R, float S, float A, bool first) {
    target_[0] = F; target_[1] = R; target_[2] = S; target_[3] = A;
    if (first) for (int i = 0; i < 4; i++) m[i] = target_[i];
}

void FlowState::Tick(Rng& rng) {
    for (int i = 0; i < 4; i++) m[i] += kM_ * (target_[i] - m[i]);
    const float F = m[0], R = m[1], A = m[3];
    for (int z = 0; z < ck::ZONES; z++) J[z] *= kJ_;
    if (hold) return;
    const float e0 = eta0.Tick(rng, dt, 1.f);
    float eMax = 0;
    for (int z = 0; z < ck::ZONES; z++) {
        const float ez = etaZ[z].Tick(rng, dt, 1.f);
        const float Es = Clamp(F * F * (0.85f + 0.10f * e0 + 0.05f * ez), 0.f, 1.f);
        E[z] += kE_ * (Es - E[z]);
        const float e = E[z];
        const float Ts = Clamp(e * (0.15f + 0.85f * R) * ck::KZ[z] + 0.10f * ez * e, 0.f, 1.f);
        T[z] += kT_ * (Ts - T[z]);
        const float Bs = Clamp(A * (0.20f * e + 0.80f * T[z] + J[z]), 0.f, 1.f);
        B[z] += kB_ * (Bs - B[z]);
        if (e > eMax) eMax = e;
    }
    gate = Clamp((eMax - ck::E_GATE) / (ck::E_GATE_FULL - ck::E_GATE), 0.f, 1.f);
}

} // namespace creek
