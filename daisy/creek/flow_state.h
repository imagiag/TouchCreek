// FlowState: shared slow causes, updated at 250 Hz. Zones 0 = flow bed (centre),
// 1 = rock wake (left), 2 = drop (right). E energy, T turbulence, B entrained
// air, J splash entrainment impulse, all in [0, 1].
#pragma once
#include "config.h"
#include "dsp.h"

namespace creek {

class FlowState {
public:
    float E[ck::ZONES], T[ck::ZONES], B[ck::ZONES], J[ck::ZONES];
    float m[4];          // smoothed Flow, Roughness, Scale, Aeration
    float gate = 0;      // scene gate (0 = silent)
    bool hold = false;
    float eddy = 0;      // eddy swell in the wake (0..1), set by Eddies
    float dt = 0;
    Drift eta0, etaZ[ck::ZONES];

    void Init(Rng& rng);
    void SetMacros(float F, float R, float S, float A, bool first);
    void AddSplash(int z, float amount) { J[z] = fminf(ck::J_CAP, J[z] + amount); }
    void Tick(Rng& rng);

private:
    float target_[4] = {0, 0, 0, 0};
    float kE_ = 0, kT_ = 0, kB_ = 0, kM_ = 0, kJ_ = 0;
};

} // namespace creek
