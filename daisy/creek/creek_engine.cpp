#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
#include "creek_engine.h"

namespace creek {

void CreekEngine::Init(float sampleRate, uint32_t seed) {
    sr_ = sampleRate;
    srInt_ = (uint32_t)lroundf(sampleRate);
    for (int l = 0; l < ck::LAYERS; l++) { pL_[l] = bufL_[l]; pR_[l] = bufR_[l]; }
    pool_.Init(sr_); mixer_.Init(sr_); out_.Init(sr_);
    kTuneA_ = 1.f - expf(-1.f / (ck::CONTROL_HZ * ck::TUNE_ATTACK_SEC));
    kTuneR_ = 1.f - expf(-1.f / (ck::CONTROL_HZ * ck::TUNE_RELEASE_SEC));
    Reset(seed);
}

void CreekEngine::Reset(uint32_t seed) {
    seed_ = seed;
    for (int i = 0; i < S_COUNT; i++) rng_[i].Seed(StreamSeed(seed, (uint32_t)i));
    sample_ = 0; tickIndex_ = 0; untilTick_ = 0;
    flow_.Init(rng_[S_DRIFT]);
    queue_.Reset(); pool_.Reset();
    sched_.Init(sr_, rng_[S_BTIME]);
    freeS_ = 1.f; activity_ = 1.f;
    if (ck::BED_ON) bed_.Init(sr_, rng_[S_BED_DRIFT]);   // wash off: not even compiled in (flash)
    eddies_.Init(rng_[S_EDDY]);
    falls_.Init(sr_, rng_[S_FALLS]);
    surface_.Init(sr_, rng_[S_SURFACE]);
    mixer_.Init(sr_); out_.Reset();
    applyMacros(true);
    snapNext_ = true;   // the first SetMacros() after a reset jumps instead of gliding
}

static inline float fin(float x, float lo, float hi, float d) { return isfinite(x) ? Clamp(x, lo, hi) : d; }

void CreekEngine::SetMacros(const Macros& v) {
    Macros& m = macros_;
    m.flow = fin(v.flow, 0.f, 1.f, m.flow); m.roughness = fin(v.roughness, 0.f, 1.f, m.roughness);
    m.scale = fin(v.scale, 0.f, 1.f, m.scale); m.aeration = fin(v.aeration, 0.f, 1.f, m.aeration);
    m.gain = fin(v.gain, 0.f, 4.f, m.gain); m.width = fin(v.width, 0.f, ck::WIDTH_MAX, m.width);
    m.mask = v.mask & 0x1Fu; m.hold = v.hold;
    for (int k = 0; k < 4; k++) { const float h = v.tuneHz[k]; m.tuneHz[k] = isfinite(h) && h > 20.f && h < 20000.f ? h : 0.f; }
    m.tune = v.tune;
    m.freeFlow = fin(v.freeFlow, 0.f, 1.f, m.freeFlow); m.variety = fin(v.variety, 0.f, 1.f, m.variety); m.decay = fin(v.decay, 0.f, 1.f, m.decay);
    m.cutoff = fin(v.cutoff, 0.f, 1.f, m.cutoff); m.resonance = fin(v.resonance, 0.f, 1.f, m.resonance);
    m.scaleMask = (v.scaleMask & 0xFFFu) ? (v.scaleMask & 0xFFFu) : 0xFFFu;
    applyMacros(snapNext_);
    snapNext_ = false;
}

void CreekEngine::applyMacros(bool snap) {
    const Macros& m = macros_;
    flow_.SetMacros(m.flow, m.roughness, m.scale, m.aeration, snap);
    flow_.hold = m.hold;
    mixer_.SetMask(m.mask, m.width);
    out_.gTarget = m.gain;
    out_.SetFilter(m.cutoff, m.resonance);
    if (snap) out_.SnapFilter();
    if (snap) { mixer_.w = m.width; out_.g = m.gain; for (int l = 0; l < ck::LAYERS; l++) mixer_.g[l] = mixer_.target[l]; }
    sched_.nTune = 0;
    for (int k = 0; k < 4; k++) if (m.tuneHz[k] > 0) {
        sched_.tuneLog2[sched_.nTune] = log2f(m.tuneHz[k]);
        sched_.tuneMidi[sched_.nTune++] = 69.f + 12.f * log2f(m.tuneHz[k] / 440.f);
    }
    sched_.variety = m.variety; sched_.scaleMask = m.scaleMask;
    const float tm = exp2f(ck::DECAY_OCT * (2.f * m.decay - 1.f));
    pool_.tauMul = tm; falls_.pool.tauMul = tm; surface_.pool.tauMul = tm;
    if (snap) freeS_ = m.freeFlow;
}

bool CreekEngine::Trigger(const Event& src) {
    Event e;
    e.type = src.type; e.zone = src.zone > 2 ? 2 : src.zone;
    e.radiusMm = src.radiusMm > 0 ? Clamp(src.radiusMm, 0.05f, 50.f) : 0.f;
    e.amp = src.amp > 0 ? fminf(src.amp, 4.f) : 0.f;
    e.tauSec = src.tauSec > 0 ? Clamp(src.tauSec, 0.002f, 1.f) : 0.f;
    e.rise = src.rise >= 0 ? fminf(src.rise, 1.f) : -1.f;
    e.pan = src.pan >= -1 ? fminf(src.pan, 1.f) : -2.f;
    const float d = src.delaySec > 0 && isfinite(src.delaySec) ? src.delaySec : 0.f;
    return queue_.Push(e, sample_ + (uint64_t)lroundf(d * sr_));
}

void CreekEngine::controlTick() {
    // eddies first: they shape this tick's wake energy (muting the layer removes them)
    const float R = flow_.m[1], T1 = flow_.T[1];
    const float eMul = Lerp(ck::EDDY_TIME_MUL[0], ck::EDDY_TIME_MUL[1], flow_.m[2]);
    const float depth = Lerp(ck::EDDY_DEPTH[0], ck::EDDY_DEPTH[1], Clamp(0.7f * R + 0.6f * T1, 0.f, 1.f)) * flow_.gate * mixer_.target[L_EDDIES];
    const float ed = eddies_.Tick(rng_[S_EDDY], flow_.dt, eMul, depth);
    flow_.eddy = ed;
    flow_.Tick(rng_[S_DRIFT]);
    const uint64_t k = tickIndex_;
    const int next = (int)(((k + 1) * srInt_) / ck::CONTROL_HZ - (k * srInt_) / ck::CONTROL_HZ);
    untilTick_ = next; tickIndex_++;
    sched_.rateMul[1] = 1.f + ck::EDDY_RATE * ed; sched_.radMul[1] = 1.f + ck::EDDY_RADIUS * ed;
    sched_.activity = activity_;
    sched_.Update(flow_, flow_.m[2]);
    falls_.Update(flow_, activity_);
    surface_.Update(flow_, rng_[S_SURFACE], flow_.dt, ed, activity_);
    const float tt = macros_.tune && sched_.nTune > 0 ? 1.f : 0.f;
    sched_.tuneAmt += (tt > sched_.tuneAmt ? kTuneA_ : kTuneR_) * (tt - sched_.tuneAmt);
    // Free flow: the creek plays by itself only this much; holding notes pours
    freeS_ += flow_.KM() * (macros_.freeFlow - freeS_);
    sched_.freeFlow = freeS_;
    activity_ = fmaxf(freeS_, sched_.tuneAmt);
    if (ck::BED_ON) bed_.Update(flow_, rng_[S_BED_DRIFT], flow_.dt, next);
}

void CreekEngine::Process(float* left, float* right, size_t frames) {
    size_t pos = 0;
    while (pos < frames) {
        if (untilTick_ == 0) controlTick();
        const int intoMicro = (int)(sample_ % ck::MICRO);
        if (intoMicro == 0) { pool_.Micro(); falls_.pool.Micro(); surface_.pool.Micro(); out_.Micro(); }
        int seg = ck::MICRO - intoMicro;
        if (seg > untilTick_) seg = untilTick_;
        if ((size_t)seg > frames - pos) seg = (int)(frames - pos);
        renderSegment(left + pos, right + pos, seg);
        pos += seg; sample_ += seg; untilTick_ -= seg;
    }
}

void CreekEngine::renderSegment(float* L, float* R, int n) {
    for (int l = 0; l < ck::LAYERS; l++) for (int i = 0; i < n; i++) { bufL_[l][i] = 0; bufR_[l][i] = 0; }
    // events: scheduled births, then queued events due in this segment
    sched_.Run(rng_[S_BTIME], n);
    for (int b = 0; b < sched_.nBorn; b++) sched_.Make(rng_[S_BPARAM], sched_.bornZone[b], pool_, sched_.bornOff[b], nullptr);
    falls_.Run(rng_[S_FALLS], n, flow_, sched_.ScaleMul(), queue_, sample_, surface_);
    surface_.Run(rng_[S_SURFACE], n);
    const uint64_t end = sample_ + (uint64_t)n;
    for (int i = 0; i < ck::QUEUE_SIZE; i++) {
        if (!queue_.used[i]) continue;
        const Event& e = queue_.ev[i];
        if (e.at >= end) continue;
        queue_.used[i] = false;
        const int o = e.at > sample_ ? (int)(e.at - sample_) : 0;
        if (e.type == EV_BUBBLE) sched_.Make(rng_[S_TRIGGER], e.zone, pool_, o, &e);
        else if (e.type == EV_DROP) { const Event d = e; drop(d, o); }
    }
    if (ck::BED_ON) bed_.Render(bufL_[L_FLOW], bufR_[L_FLOW], n, rng_[S_NOISE_C], rng_[S_NOISE_L], rng_[S_NOISE_R]);
    pool_.Render(bufL_[L_BUBBLES], bufR_[L_BUBBLES], n);
    falls_.pool.Render(bufL_[L_FALLS], bufR_[L_FALLS], n);
    surface_.pool.Render(bufL_[L_SURFACE], bufR_[L_SURFACE], n);
    mixer_.Mix(pL_, pR_, L, R, n);
    out_.Process(L, R, n);
}

// note mode: a tuned drop = one bubble at the note plus a few quieter
// follow-up bubbles (same pitch or an octave up) over the next 12-90 ms
void CreekEngine::drop(const Event& e, int o) {
    Rng& r = rng_[S_TRIGGER];
    const float rMm = e.radiusMm > 0 ? e.radiusMm : 3.f;
    const float u0 = r.Uni(), u1 = r.Bip();
    const float pan = e.pan >= -1 ? e.pan : ck::DROP_PAN * u1, amp = e.amp > 0 ? e.amp : 0.7f;
    pool_.Spawn(fmaxf(ck::F_MIN_HZ, 3000.f / rMm), amp * ck::DROP_GAIN * ck::BUBBLE_GAIN, sched_.TauFor(rMm) * ck::DROP_TAU_MUL,
                ck::ATTACK_MIN, ck::DROP_RISE, pan, o);
    const int nc = 1 + (int)floorf(u0 * ck::DROP_CHILDREN * fminf(1.f, amp));
    for (int k = 0; k < (int)ck::DROP_CHILDREN; k++) {
        const float d = r.Uni(), a = r.Uni(), oc = r.Uni(), pj = r.Bip();
        if (k >= nc) continue;
        Event c;
        c.type = EV_BUBBLE; c.zone = 0; c.radiusMm = oc < 0.35f ? rMm * 0.5f : rMm;
        c.amp = amp * ck::DROP_GAIN * Lerp(ck::DROP_CHILD_AMP[0], ck::DROP_CHILD_AMP[1], a);
        c.rise = 0; c.tauSec = 0; c.pan = Clamp(pan + 0.15f * pj, -1.f, 1.f);
        queue_.Push(c, sample_ + (uint64_t)o + (uint64_t)lroundf(Lerp(ck::DROP_CHILD_MS[0], ck::DROP_CHILD_MS[1], d) * 0.001f * sr_));
    }
}

void CreekEngine::GetStats(Stats& s) const {
    s.births = pool_.births; s.drops = pool_.drops + sched_.over; s.steals = pool_.steals;
    s.queueDrops = queue_.drops; s.limited = out_.limited; s.peak = out_.peak;
    s.active = pool_.Active(); s.maxActive = pool_.maxActive;
    s.sumLog2F = pool_.sumLog2F;
    s.meanF = pool_.births ? exp2f(pool_.sumLog2F / pool_.births) : 0.f;
    s.rate = sched_.rate[0] + sched_.rate[1] + sched_.rate[2]; s.gate = flow_.gate;
    s.eddy = flow_.eddy; s.splashes = falls_.count; s.surface = surface_.count;
    s.splashRate = falls_.rate; s.surfaceRate = surface_.rate;
    for (int z = 0; z < ck::ZONES; z++) { s.E[z] = flow_.E[z]; s.T[z] = flow_.T[z]; s.B[z] = flow_.B[z]; }
}

} // namespace creek
