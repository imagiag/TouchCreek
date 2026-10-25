// FlowBed (noise layers): continuous wash in three overlapping bands.
//   body  ~90 Hz – 1 kHz, mono (keeps mono playback solid)
//   mid   ~0.7 – 1.8 kHz, partly independent L/R
//   sheen ~2 – 4 kHz,     mostly independent L/R
// Gains and cutoffs drift slowly with the shared state; a faster "flutter"
// follows turbulence. Gains ramp linearly between control ticks.
#pragma once
#include "config.h"
#include "dsp.h"
#include "flow_state.h"

namespace creek {

class FlowBed {
public:
    void Init(float sr, Rng& rngD);
    void Update(const FlowState& fs, Rng& rngD, float dt, int span);  // control tick
    void Render(float* L, float* R, int n, Rng& rc, Rng& rl, Rng& rr);

private:
    float sr_ = 48000;
    Hp1 hp_;
    Svf body_, midL_, midR_, hiL_, hiR_;
    Drift dBody_, dMid_, dHi_, dCut_, flMid_, flHi_;
    float g_[3] = {0, 0, 0}, gStep_[3] = {0, 0, 0};
    float aM_ = 0, bM_ = 0, aH_ = 0, bH_ = 0;
};

} // namespace creek
