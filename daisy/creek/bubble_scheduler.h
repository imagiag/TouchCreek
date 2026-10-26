// BubbleScheduler (event scheduler): per-zone hazard accumulators. Each sample
// adds lambda/Fs; a bubble is born when the sum crosses an exponential
// threshold -ln(U), at that exact sample. Parameters come from their own
// random stream, so timing and timbre are reproducible independently.
#pragma once
#include "config.h"
#include "dsp.h"
#include "flow_state.h"
#include "bubble_pool.h"
#include "event_queue.h"

namespace creek {

class BubbleScheduler {
public:
    void Init(float sr, Rng* const* rngT);             // rngT[z]: per-zone timing streams
    void Update(const FlowState& fs, float S);   // control tick
    void Run(Rng* const* rngT, int n);                 // births in the next n samples
    bool Make(Rng& rngP, int z, BubblePool& pool, int off, const Event* ev);
    float TauFor(float rMm) const;     // decay time of a bubble of this radius

    // note mode: held notes gather some creek bubbles onto their pitches
    float tuneLog2[4], tuneMidi[4];
    float variety = 0.5f, freeFlow = 1.f, activity = 1.f;
    uint32_t scaleMask = 0xFFF;
    float rSkew = ck::R_SKEW;   // Tone: radius distribution skew ...
    float toneMul = 1.f;        // ... and radius multiplier at the extremes
    int nTune = 0;
    float tuneAmt = 0;
    float rateMul[ck::ZONES] = {1, 1, 1}, radMul[ck::ZONES] = {1, 1, 1};  // eddies (wake)
    float ScaleMul() const { return scaleMul_; }
    float AmpScale() const { return ampScale_; }

    float rate[ck::ZONES];
    int nBorn = 0;
    int bornZone[ck::MAX_BIRTHS_PER_MICRO], bornOff[ck::MAX_BIRTHS_PER_MICRO];
    uint32_t over = 0;

    // pitch (MIDI) for a tuned event: held / last-played notes, shaped by Variety
    // keepOctave (surface ticks, splash fizz): stay in their own register, so Variety's
    // "one note" side doesn't pull them down onto the held note's octave
    float tunedMidi(float m0, float uA, float uB, bool keepOctave = false) const;

private:
    float sr_ = 48000;
    float acc_[ck::ZONES], thr_[ck::ZONES], inc_[ck::ZONES];
    float lnRmin_ = 0, lnRspan_ = 1, lnTauSpan_ = 1;
    float ampScale_ = 0, scaleMul_ = 1;
};

} // namespace creek
