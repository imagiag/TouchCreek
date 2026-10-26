#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
#include "creek_engine.h"

namespace creek {

CREEK_SMALL void CreekEngine::Init(float sampleRate, uint32_t seed) {
    sr_ = sampleRate;
    srInt_ = (uint32_t)lroundf(sampleRate);
    for (int l = 0; l < ck::LAYERS; l++) { pL_[l] = bufL_[l]; pR_[l] = bufR_[l]; }
    pool_.Init(sr_); mixer_.Init(sr_); out_.Init(sr_); delay_.Init(sr_);
    kStone_ = expf(-1.f / (ck::CONTROL_HZ * ck::STONE_SURGE_SEC));
    kTuneA_ = 1.f - expf(-1.f / (ck::CONTROL_HZ * ck::TUNE_ATTACK_SEC));
    kTuneR_ = 1.f - expf(-1.f / (ck::CONTROL_HZ * ck::TUNE_RELEASE_SEC));
    Reset(seed);
}

CREEK_SMALL void CreekEngine::Reset(uint32_t seed) {
    seed_ = seed;
    for (int i = 0; i < S_COUNT; i++) rng_[i].Seed(StreamSeed(seed, (uint32_t)i));
    for (int z = 0; z < 3; z++) { rngT_[z] = &rng_[kZoneBTime[z]]; rngP_[z] = &rng_[kZoneBParam[z]]; }
    sample_ = 0; tickIndex_ = 0; untilTick_ = 0;
    flow_.Init(rng_[S_DRIFT]);
    queue_.Reset(); pool_.Reset();
    sched_.Init(sr_, rngT_);   // (clears the held notes too)
    freeS_ = 1.f; activity_ = 1.f; stoneS_ = 0.f; stones_ = 0; creekFade_ = 1.f; tuneDetail_ = false;
    eddies_.Init(rng_[S_EDDY]);
    falls_.Init(sr_, rng_[S_FALLS]);
    surface_.Init(sr_, rng_[S_SURFACE]);
    cloud_.Init(sr_, rng_[S_CLOUD]);
    mixer_.Init(sr_); out_.Reset(); delay_.Reset();
    applyMacros(true);
    snapNext_ = true;   // the first SetMacros() after a reset jumps instead of gliding
}

static inline float fin(float x, float lo, float hi, float d) { return isfinite(x) ? Clamp(x, lo, hi) : d; }

CREEK_SMALL void CreekEngine::SetMacros(const Macros& v) {
    Macros& m = macros_;
    m.flow = fin(v.flow, 0.f, 1.f, m.flow); m.roughness = fin(v.roughness, 0.f, 1.f, m.roughness);
    m.scale = fin(v.scale, 0.f, 1.f, m.scale); m.aeration = fin(v.aeration, 0.f, 1.f, m.aeration);
    m.gain = fin(v.gain, 0.f, 4.f, m.gain); m.width = fin(v.width, 0.f, ck::WIDTH_MAX, m.width);
    m.mask = v.mask & kAllLayers; m.hold = v.hold;
    for (int k = 0; k < 4; k++) { const float h = v.tuneHz[k]; m.tuneHz[k] = isfinite(h) && h > 20.f && h < 20000.f ? h : 0.f; }
    m.tune = v.tune; m.tuneDetail = v.tuneDetail;
    m.freeFlow = fin(v.freeFlow, 0.f, 1.f, m.freeFlow); m.variety = fin(v.variety, 0.f, 1.f, m.variety); m.decay = fin(v.decay, 0.f, 1.f, m.decay);
    m.tone = fin(v.tone, 0.f, 1.f, m.tone);
    m.attack = fin(v.attack, 0.001f, 10.f, m.attack); m.release = fin(v.release, 0.005f, 20.f, m.release);
    m.cutoff = fin(v.cutoff, 0.f, 1.f, m.cutoff); m.resonance = fin(v.resonance, 0.f, 1.f, m.resonance);
    m.delayTime = fin(v.delayTime, 0.01f, ck::DELAY_BUFFER_SEC, m.delayTime); m.delayAmt = fin(v.delayAmt, 0.f, 1.f, m.delayAmt);
    m.scaleMask = (v.scaleMask & 0xFFFu) ? (v.scaleMask & 0xFFFu) : 0xFFFu;
    applyMacros(snapNext_);
    snapNext_ = false;
}

CREEK_SMALL void CreekEngine::applyMacros(bool snap) {
    const Macros& m = macros_;
    flow_.SetMacros(m.flow, m.roughness, m.scale, m.aeration, snap);
    flow_.hold = m.hold;
    mixer_.SetMask(m.mask, m.width);
    cloud_.target = (m.mask & kCloudBit) ? 1.f : 0.f;
    if (snap) cloud_.g = cloud_.target;
    out_.gTarget = m.gain;
    out_.SetFilter(m.cutoff, m.resonance);
    if (snap) out_.SnapFilter();
    delay_.time = m.delayTime; delay_.amt = m.delayAmt;
    if (snap) delay_.Snap();
    if (snap) { mixer_.w = m.width; out_.g = m.gain; for (int l = 0; l < ck::LAYERS; l++) mixer_.g[l] = mixer_.target[l]; }
    // held notes; after release the last ones stay, so the release tail keeps their pitches
    bool anyNote = false;
    for (int k = 0; k < 4; k++) if (m.tuneHz[k] > 0) anyNote = true;
    if (m.tune && anyNote) {
        sched_.nTune = 0;
        for (int k = 0; k < 4; k++) if (m.tuneHz[k] > 0) {
            sched_.tuneLog2[sched_.nTune] = log2f(m.tuneHz[k]);
            sched_.tuneMidi[sched_.nTune++] = 69.f + 12.f * log2f(m.tuneHz[k] / 440.f);
        }
    }
    kTuneA_ = 1.f - expf(-1.f / (ck::CONTROL_HZ * m.attack));
    kTuneR_ = 1.f - expf(-1.f / (ck::CONTROL_HZ * m.release));
    sched_.variety = m.variety; sched_.scaleMask = m.scaleMask;
    const float tb = 2.f * m.tone - 1.f;                         // -1 dark .. +1 bright
    sched_.rSkew = ck::R_SKEW * powf(ck::TONE_SKEW_RANGE, tb);
    sched_.toneMul = exp2f((tb < 0.f ? ck::TONE_OCT[0] : -ck::TONE_OCT[1]) * powf(fabsf(tb), ck::TONE_OCT_EXP));
    const float tm = exp2f(ck::DECAY_OCT * (2.f * m.decay - 1.f));
    pool_.tauMul = tm; falls_.pool.tauMul = tm; surface_.pool.tauMul = tm;
    if (snap) freeS_ = m.freeFlow;
}

CREEK_SMALL bool CreekEngine::Trigger(const Event& src) {
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
    cloud_.Update(rng_[S_CLOUD], sched_, flow_.dt);
    const float tt = macros_.tune ? 1.f : 0.f;
    sched_.tuneAmt += (tt > sched_.tuneAmt ? kTuneA_ : kTuneR_) * (tt - sched_.tuneAmt);
    // Free flow: the creek plays by itself only this much; holding notes pours
    freeS_ += flow_.KM() * (macros_.freeFlow - freeS_);
    sched_.freeFlow = freeS_;
    const float g = flow_.gate;
    creekFade_ = g >= 1.f ? 1.f : expf(-(1.f - g) * ck::MICRO / (ck::GATE_FADE_SEC * sr_));
    tuneDetail_ = macros_.tuneDetail && sched_.nTune > 0;
    stoneS_ *= kStone_;                                            // a thrown stone's disturbance settles
    const float st = stoneS_ * stoneS_;                            // squared: settles fully to still water
    // the surge matters in quiet water; a full creek needs little extra (keeps headroom)
    activity_ = fmaxf(fmaxf(freeS_, sched_.tuneAmt), st) * (1.f + ck::STONE_SURGE_RATE * st * (1.f - 0.8f * flow_.m[0]));
}

void CreekEngine::Process(float* left, float* right, size_t frames) {
    size_t pos = 0;
    while (pos < frames) {
        if (untilTick_ == 0) controlTick();
        const int intoMicro = (int)(sample_ % ck::MICRO);
        if (intoMicro == 0) {
            pool_.Micro(); falls_.pool.Micro(); surface_.pool.Micro(); out_.Micro();
            if (creekFade_ < 1.f) { pool_.FadeCreek(creekFade_); falls_.pool.FadeCreek(creekFade_); surface_.pool.FadeCreek(creekFade_); }
        }
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
    sched_.Run(rngT_, n);
    for (int b = 0; b < sched_.nBorn; b++) sched_.Make(*rngP_[sched_.bornZone[b]], sched_.bornZone[b], pool_, sched_.bornOff[b], nullptr);
    falls_.Run(rng_[S_FALLS], n, flow_, sched_, queue_, sample_, surface_, cloud_, tuneDetail_);
    surface_.Run(rng_[S_SURFACE], n, sched_, tuneDetail_);
    const uint64_t end = sample_ + (uint64_t)n;
    // due events in time order (ties: queue order), so the result doesn't depend on the block size;
    // events they queue for this same segment are handled in the next round
    for (;;) {
      int nd = 0;
      for (int i = 0; i < ck::QUEUE_SIZE; i++) if (queue_.used[i] && queue_.ev[i].at < end) {
          int j = nd++;
          while (j > 0) {
              const Event& p = queue_.ev[queue_.due[j - 1]]; const Event& c = queue_.ev[i];
              if (p.at < c.at || (p.at == c.at && p.seq < c.seq)) break;
              queue_.due[j] = queue_.due[j - 1]; j--;
          }
          queue_.due[j] = i;
      }
      if (!nd) break;
      for (int k = 0; k < nd; k++) {
        const int i = queue_.due[k];
        const Event e = queue_.ev[i];
        queue_.used[i] = false;
        const int o = e.at > sample_ ? (int)(e.at - sample_) : 0;
        // creek events (splash settling bubbles: no explicit size) have their own stream; played ones use TRIGGER
        if (e.type == EV_BUBBLE) sched_.Make(rng_[e.radiusMm > 0 ? S_TRIGGER : S_FCHILD], e.zone, pool_, o, &e);
        else if (e.type == EV_DROP) { const Event d = e; drop(d, o); }
        else if (e.type == EV_STONE) { const Event d = e; stone(d, o); }
        else if (e.type == EV_FIZZ)
            falls_.pool.Spawn(fmaxf(ck::F_MIN_HZ, 3000.f / e.radiusMm), e.amp, sched_.TauFor(e.radiusMm) * ck::FALLS_FIZZ_TAU_MUL, ck::ATTACK_MIN, 0.f, e.pan, o);
      }
    }
    pool_.Render(bufL_[L_BUBBLES], bufR_[L_BUBBLES], n);
    falls_.pool.Render(bufL_[L_FALLS], bufR_[L_FALLS], n);
    surface_.pool.Render(bufL_[L_SURFACE], bufR_[L_SURFACE], n);
    mixer_.Mix(pL_, pR_, L, R, n);
    cloud_.Render(rng_[S_CLOUD], sched_, L, R, n);
    delay_.Process(L, R, n);
    out_.Process(L, R, n);
}

// note mode: a tuned drop = one bubble at the note plus a few quieter
// follow-up bubbles (same pitch or an octave up) over the next 12-90 ms
CREEK_SMALL void CreekEngine::drop(const Event& e, int o) {
    Rng& r = rng_[S_TRIGGER];
    const float rMm = e.radiusMm > 0 ? e.radiusMm : 3.f;
    const float u0 = r.Uni(), u1 = r.Bip();
    const float pan = e.pan >= -1 ? e.pan : ck::DROP_PAN * u1, amp = e.amp > 0 ? e.amp : 0.7f;
    pool_.Spawn(fmaxf(ck::F_MIN_HZ, 3000.f / rMm), amp * ck::DROP_GAIN * ck::BUBBLE_GAIN, sched_.TauFor(rMm) * ck::DROP_TAU_MUL,
                ck::ATTACK_MIN, ck::DROP_RISE, pan, o, true);
    const int nc = 1 + (int)((double)u0 * (double)ck::DROP_CHILDREN * (double)fminf(1.f, amp));
    for (int k = 0; k < (int)ck::DROP_CHILDREN; k++) {
        const float d = r.Uni(), a = r.Uni(), oc = r.Uni(), pj = r.Bip();
        if (k >= nc) continue;
        Event c;
        c.type = EV_BUBBLE; c.zone = 0; c.radiusMm = oc < 0.35f ? rMm * 0.5f : rMm;
        c.amp = amp * ck::DROP_GAIN * Lerp(ck::DROP_CHILD_AMP[0], ck::DROP_CHILD_AMP[1], a);
        c.rise = 0; c.tauSec = 0; c.pan = Clamp(pan + 0.15f * pj, -1.f, 1.f);
        queue_.Push(c, sample_ + (uint64_t)o + DelaySamples(d, ck::DROP_CHILD_MS[0], ck::DROP_CHILD_MS[1], sr_));
    }
}

// throw a stone: a deep falling plop, a burst of larger bubbles spreading from the
// centre over ~0.35 s, and a disturbance (air in every zone, more events) that settles
CREEK_SMALL void CreekEngine::stone(const Event& e, int o) {
    Rng& r = rng_[S_TRIGGER];
    const float amp = e.amp > 0 ? fminf(1.f, e.amp) : 0.8f;
    const float u0 = r.Uni(), u1 = r.Uni(), u2 = r.Uni(), u3 = r.Bip();
    const float sm = sched_.ScaleMul();
    falls_.pool.Spawn(Lerp(ck::STONE_PLOP_HZ[0], ck::STONE_PLOP_HZ[1], u0) / sm, amp * ck::STONE_PLOP_GAIN,
                      Lerp(ck::STONE_PLOP_TAU_SEC[0], ck::STONE_PLOP_TAU_SEC[1], u1), ck::FALLS_ATTACK_SEC[1], ck::STONE_PLOP_GLIDE, 0.15f * u3, o, true);
    const int n = CountLerp(ck::STONE_BUBBLES[0], ck::STONE_BUBBLES[1], (double)amp * (0.7 + 0.3 * (double)u2));
    const float lnR = logf(ck::STONE_RADIUS_MM[1] / ck::STONE_RADIUS_MM[0]);
    for (int k = 0; k < (int)ck::STONE_BUBBLES[1]; k++) {
        const float d = r.Uni(), rr = r.Uni(), a = r.Uni(), pj = r.Bip();
        if (k >= n) continue;
        Event c;
        c.type = EV_BUBBLE; c.zone = 0; c.tauSec = 0; c.rise = -1;
        c.radiusMm = ck::STONE_RADIUS_MM[0] * expf(lnR * rr) * sm;
        c.amp = amp * Lerp(ck::STONE_BUBBLE_AMP[0], ck::STONE_BUBBLE_AMP[1], a) * (1.f - 0.6f * d);   // later = quieter
        c.pan = Clamp(0.5f * pj * (0.3f + d), -1.f, 1.f);                                              // spreading out
        queue_.Push(c, sample_ + (uint64_t)o + DelaySamples((double)d * (double)d, 0.0, (double)ck::STONE_SPREAD_SEC * 1000.0, sr_));
    }
    for (int z = 0; z < ck::ZONES; z++) flow_.AddSplash(z, amp * ck::STONE_J);
    stoneS_ = fminf(1.f, stoneS_ + amp);
    surface_.boost += amp * ck::STONE_SURFACE;
    stones_++;
}

CREEK_SMALL void CreekEngine::GetStats(Stats& s) const {
    s.births = pool_.births; s.drops = pool_.drops + sched_.over; s.steals = pool_.steals;
    s.queueDrops = queue_.drops; s.limited = out_.limited; s.peak = out_.peak;
    s.active = pool_.Active(); s.maxActive = pool_.maxActive;
    s.sumLog2F = pool_.sumLog2F;
    s.meanF = pool_.births ? exp2f(pool_.sumLog2F / pool_.births) : 0.f;
    s.rate = sched_.rate[0] + sched_.rate[1] + sched_.rate[2]; s.gate = flow_.gate;
    s.eddy = flow_.eddy; s.splashes = falls_.count; s.surface = surface_.count;
    s.splashRate = falls_.rate; s.surfaceRate = surface_.rate; s.stones = stones_;
    s.activeUser = pool_.ActiveUser(); s.activeFalls = falls_.pool.Active(); s.activeSurface = surface_.pool.Active();
    s.activity = activity_;
    for (int k = 0; k < 2; k++) { s.cloud[k] = cloud_.d[k]; s.cloudHz[k] = cloud_.Hz(k); }
    s.cloudOn = cloud_.target > 0.f;
    for (int z = 0; z < ck::ZONES; z++) { s.E[z] = flow_.E[z]; s.T[z] = flow_.T[z]; s.B[z] = flow_.B[z]; }
}

} // namespace creek
