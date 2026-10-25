// =============================================================================
// Creek — procedural stereo creek synthesis (portable engine, plain C-style JS).
// The C++ port in daisy/creek/ mirrors this file module by module; keep them in
// step (daisy/test compares their output).
//
// Modules (brief, "Portable DSP boundary"):
//   CrRng            seeded PRNG streams (one per purpose / layer)
//   CrFlowState      macros -> three zones of E / T / B, updated at 250 Hz
//   CrEventQueue     sample-timed pending events (Trigger(), splash children)
//   CrBubbleScheduler hazard-based bubble births per zone
//   CrBubblePool     fixed pool of damped sinusoidal voices (van den Doel)
//   CrFlowBed        noise layers: body / mid / sheen bands
//   CrSpatialMixer   layer sum, diagnostic mutes, stereo width
//   CrOutputStage    DC blocker, gain, linked limiter
//   CreekEngine      init / setMacros / trigger / reset / process
//
// Timing: everything that changes over time is tied to the absolute sample
// counter (control tick every Fs/250 samples, micro tick every CK.MICRO), so
// the output does not depend on how the host splits it into blocks.
// =============================================================================

const CR_TWO_PI = 6.283185307179586;
const CR_LOG = 64;                      // bubble birth log size (power of two)
const CR_U24 = 5.9604644775390625e-8;   // 2^-24
const CR_U23 = 1.1920928955078125e-7;   // 2^-23

// ---- PRNG: mulberry32 (32-bit integer ops: identical in JS and C++) --------
class CrRng {
  constructor(seed) { this.s = seed >>> 0; }
  seed(s) { this.s = s >>> 0; }
  u32() {
    let t = (this.s = (this.s + 0x6D2B79F5) >>> 0);
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return (t ^ (t >>> 14)) >>> 0;
  }
  uni() { return (this.u32() >>> 8) * CR_U24; }                 // [0, 1)
  uniPos() { return (((this.u32() >>> 9) * 2) + 1) * CR_U24; }  // (0, 1)
  bip() { return (this.u32() >>> 8) * CR_U23 - 1; }             // [-1, 1)
}
function crHash(x) {
  x = (x ^ (x >>> 16)) >>> 0; x = Math.imul(x, 0x7feb352d) >>> 0;
  x = (x ^ (x >>> 15)) >>> 0; x = Math.imul(x, 0x846ca68b) >>> 0;
  return (x ^ (x >>> 16)) >>> 0;
}
function crStreamSeed(seed, stream) { return crHash((seed + Math.imul(stream + 1, 0x9E3779B9)) >>> 0); }
// stream ids (fixed: muting a layer never shifts another layer's randomness)
const CR_S_DRIFT = 0, CR_S_BTIME = 1, CR_S_BPARAM = 2, CR_S_NOISE_C = 3, CR_S_NOISE_L = 4,
  CR_S_NOISE_R = 5, CR_S_BED_DRIFT = 6, CR_S_EDDY = 7, CR_S_FALLS = 8, CR_S_SURFACE = 9, CR_S_TRIGGER = 10;

function crClamp(x, lo, hi) { return x < lo ? lo : (x > hi ? hi : x); }
function crLerp(a, b, t) { return a + (b - a) * t; }

// ---- smooth bounded random drift in [-1, 1] -----------------------------
// Random targets, smoothstep-interpolated over random durations.
class CrDrift {
  constructor() { this.from = 0; this.to = 0; this.t = 0; this.dur = 1; this.v = 0; this.minS = 1; this.maxS = 1; }
  init(rng, minSec, maxSec) {
    this.minS = minSec; this.maxS = maxSec;
    this.from = rng.bip(); this.to = rng.bip();
    this.dur = minSec + (maxSec - minSec) * rng.uni();
    this.t = 0; this.v = this.from;
  }
  tick(rng, dt, timeMul) {
    this.t += dt;
    if (this.t >= this.dur) {
      this.t -= this.dur;
      this.from = this.to; this.to = rng.bip();
      this.dur = (this.minS + (this.maxS - this.minS) * rng.uni()) * timeMul;
      if (this.t > this.dur) this.t = this.dur;
    }
    const x = this.t / this.dur, s = x * x * (3 - 2 * x);
    this.v = this.from + (this.to - this.from) * s;
    return this.v;
  }
}

// ---- TPT state-variable filter (Cytomic), stable under modulation ----------
class CrSvf {
  constructor() { this.ic1 = 0; this.ic2 = 0; this.a1 = 0; this.a2 = 0; this.a3 = 0; this.k = 1; }
  set(fc, q, sr) {
    const g = Math.tan(Math.PI * crClamp(fc, 10, 0.45 * sr) / sr);
    this.k = 1 / q;
    this.a1 = 1 / (1 + g * (g + this.k)); this.a2 = g * this.a1; this.a3 = g * this.a2;
  }
  reset() { this.ic1 = 0; this.ic2 = 0; }
}
// one-pole TPT high-pass
class CrHp1 {
  constructor() { this.s = 0; this.G = 0; }
  set(fc, sr) { const g = Math.tan(Math.PI * fc / sr); this.G = g / (1 + g); }
  reset() { this.s = 0; }
}

// =============================================================================
// FlowState: shared slow causes. Zones 0 = flow bed (centre), 1 = rock wake
// (left), 2 = drop (right). E energy, T turbulence, B entrained air, J splash
// entrainment impulse, all in [0, 1].
// =============================================================================
class CrFlowState {
  constructor() {
    this.E = new Float32Array(3); this.T = new Float32Array(3); this.B = new Float32Array(3); this.J = new Float32Array(3);
    this.eta0 = new CrDrift(); this.etaZ = [new CrDrift(), new CrDrift(), new CrDrift()];
    this.m = new Float32Array(4);       // smoothed F R S A
    this.target = new Float32Array(4);
    this.hold = false;
    this.gate = 0;                      // scene gate (0 = silent)
    this.eddy = 0;                      // eddy swell in the wake (0..1), set by CrEddies
  }
  init(sr, rng) {
    const dt = 1 / CK.CONTROL_HZ;
    this.dt = dt;
    this.kE = 1 - Math.exp(-dt / CK.TAU_E); this.kT = 1 - Math.exp(-dt / CK.TAU_T); this.kB = 1 - Math.exp(-dt / CK.TAU_B);
    this.kM = 1 - Math.exp(-dt / CK.MACRO_SMOOTH_SEC);
    this.kJ = Math.exp(-dt / CK.J_DECAY_SEC);
    this.E.fill(0); this.T.fill(0); this.B.fill(0); this.J.fill(0); this.gate = 0;
    this.eta0.init(rng, CK.ETA_MIN_SEC, CK.ETA_MAX_SEC);
    for (let z = 0; z < 3; z++) this.etaZ[z].init(rng, CK.ETA_MIN_SEC, CK.ETA_MAX_SEC);
  }
  setMacros(F, R, S, A, first) {
    this.target[0] = F; this.target[1] = R; this.target[2] = S; this.target[3] = A;
    if (first) for (let i = 0; i < 4; i++) this.m[i] = this.target[i];
  }
  addSplash(z, amount) { this.J[z] = Math.min(CK.J_CAP, this.J[z] + amount); }
  tick(rng) {
    const m = this.m;
    for (let i = 0; i < 4; i++) m[i] += this.kM * (this.target[i] - m[i]);
    const F = m[0], R = m[1], A = m[3];
    for (let z = 0; z < 3; z++) this.J[z] *= this.kJ;
    if (this.hold) return;
    const e0 = this.eta0.tick(rng, this.dt, 1);
    let eMax = 0;
    for (let z = 0; z < 3; z++) {
      const ez = this.etaZ[z].tick(rng, this.dt, 1);
      const Es = crClamp(F * F * (0.85 + 0.10 * e0 + 0.05 * ez) * (z === 1 ? 1 + CK.EDDY_E * this.eddy : 1), 0, 1);
      this.E[z] += this.kE * (Es - this.E[z]);
      const E = this.E[z];
      const Ts = crClamp(E * (0.15 + 0.85 * R) * CK.KZ[z] + 0.10 * ez * E, 0, 1);
      this.T[z] += this.kT * (Ts - this.T[z]);
      const Bs = crClamp(A * (0.20 * E + 0.80 * this.T[z] + this.J[z]), 0, 1);
      this.B[z] += this.kB * (Bs - this.B[z]);
      if (E > eMax) eMax = E;
    }
    this.gate = crClamp((eMax - CK.E_GATE) / (CK.E_GATE_FULL - CK.E_GATE), 0, 1);
  }
}

// =============================================================================
// EventQueue: pending events at absolute sample times (bounded, no allocation).
// =============================================================================
const CR_EV_BUBBLE = 0, CR_EV_SPLASH = 1, CR_EV_DROP = 2, CR_EV_STONE = 3;
class CrEvent {
  constructor() { this.type = 0; this.zone = 1; this.radiusMm = 0; this.amp = 0; this.tauSec = 0; this.rise = -1; this.pan = -2; this.delaySec = 0; this.at = 0; }
}
class CrEventQueue {
  constructor(n) { this.ev = []; for (let i = 0; i < n; i++) this.ev.push(new CrEvent()); this.used = new Uint8Array(n); this.drops = 0; }
  reset() { this.used.fill(0); this.drops = 0; }
  push(src, at) {
    for (let i = 0; i < this.used.length; i++) if (!this.used[i]) {
      const e = this.ev[i];
      e.type = src.type; e.zone = src.zone; e.radiusMm = src.radiusMm; e.amp = src.amp; e.tauSec = src.tauSec;
      e.rise = src.rise; e.pan = src.pan; e.at = at; this.used[i] = 1; return true;
    }
    this.drops++; return false;
  }
}

// =============================================================================
// BubblePool: fixed slots of damped sinusoids y = A exp(-t/tau) sin(phi(t)).
// Each voice is a decaying complex phasor rotated once per sample; pitch glides
// update the rotation at micro ticks, so phase stays continuous.
// =============================================================================
class CrBubblePool {
  // budget = voices sounding at once; slots = budget + room for 2 ms steal fades
  constructor(budget, slots, log) {
    const N = slots || CK.VOICE_SLOTS;
    this.n = N; this.budget = budget || CK.VOICES; this.log = log !== false; this.tauMul = 1;
    this.on = new Uint8Array(N); this.fading = new Uint8Array(N);
    this.zr = new Float64Array(N); this.zi = new Float64Array(N);
    this.c = new Float32Array(N); this.s = new Float32Array(N); this.damp = new Float32Array(N);
    this.w0 = new Float32Array(N); this.rise = new Float32Array(N); this.riseInc = new Float32Array(N);
    this.att = new Float64Array(N); this.ka = new Float32Array(N);
    this.gl = new Float32Array(N); this.gr = new Float32Array(N);
    this.fade = new Float64Array(N); this.fadeStep = new Float32Array(N);
    this.life = new Int32Array(N); this.age = new Int32Array(N); this.start = new Int32Array(N);
    this.user = new Uint8Array(N);    // 1 = played (drop, stone): not faded with the creek
    this.wMax = 0;
    // birth log for displays / LED (not used by the audio path)
    this.logF = new Float32Array(CR_LOG); this.logA = new Float32Array(CR_LOG); this.logP = new Float32Array(CR_LOG);
    this.logW = 0;
    this.reset();
  }
  init(sr) {
    this.sr = sr;
    this.wMax = CR_TWO_PI * CK.F_MAX_RATIO;
    this.fadeInc = 1 / (CK.STEAL_FADE_SEC * sr);
    this.lifeLn = Math.log(Math.pow(10, -CK.RETIRE_DB / 20));
    this.retireAbs = Math.pow(10, CK.RETIRE_ABS_DB / 20);
  }
  reset() {
    this.on.fill(0); this.fading.fill(0); this.zr.fill(0); this.zi.fill(0);
    this.births = 0; this.drops = 0; this.steals = 0; this.maxActive = 0; this.sumLog2F = 0;
  }
  active() { let a = 0; for (let i = 0; i < this.n; i++) if (this.on[i] && !this.fading[i]) a++; return a; }
  // Start a voice at sample offset `off` of the current segment.
  spawn(f0, amp, tau, attack, rise, pan, off, user) {
    const N = this.n, sr = this.sr;
    let act = 0, low = -1, lowE = 1e30;
    for (let i = 0; i < N; i++) if (this.on[i] && !this.fading[i]) {
      act++;
      const e = this.zr[i] * this.zr[i] + this.zi[i] * this.zi[i];
      if (e < lowE) { lowE = e; low = i; }
    }
    if (act >= this.budget) {
      if (amp * amp <= lowE) { this.drops++; return false; }   // quieter than everything: drop it
      this.fading[low] = 1; this.fade[low] = 1; this.fadeStep[low] = this.fadeInc; this.steals++;
    }
    let v = -1;
    for (let i = 0; i < N; i++) if (!this.on[i]) { v = i; break; }
    if (v < 0) { this.drops++; return false; }
    tau *= this.tauMul;                 // Decay knob
    const w = Math.min(CR_TWO_PI * f0 / sr, this.wMax / (1 + Math.max(0, rise)));
    const r = Math.exp(-1 / (tau * sr));
    this.on[v] = 1; this.fading[v] = 0; this.fade[v] = 1; this.fadeStep[v] = 0; this.user[v] = user ? 1 : 0;
    this.zr[v] = amp; this.zi[v] = 0;
    this.damp[v] = r; this.w0[v] = w;
    this.c[v] = r * Math.cos(w); this.s[v] = r * Math.sin(w);
    this.rise[v] = rise; this.riseInc[v] = rise !== 0 ? 1 / (CK.RISE_TIME_TAUS * tau * sr) : 0;
    this.att[v] = 0; this.ka[v] = 1 - Math.exp(-3 / (attack * sr));  // ~95 % after `attack`
    const p = (crClamp(pan, -1, 1) + 1) * 0.25 * Math.PI;
    this.gl[v] = Math.cos(p); this.gr[v] = Math.sin(p);
    // retire at -80 dB re. the event, or earlier once it is below RETIRE_ABS_DB absolute
    const lifeLn = Math.min(this.lifeLn, Math.log(Math.max(amp, 1e-9) / this.retireAbs));
    this.life[v] = Math.max(1, Math.ceil(tau * sr * lifeLn)); this.age[v] = 0; this.start[v] = off;
    this.births++; this.sumLog2F += Math.log2(w * sr / CR_TWO_PI);
    if (this.log) {
      const j = this.logW & (CR_LOG - 1);
      this.logF[j] = w * sr / CR_TWO_PI; this.logA[j] = amp; this.logP[j] = pan; this.logW = (this.logW + 1) >>> 0;
    }
    const nowActive = act >= this.budget ? act : act + 1;
    if (nowActive > this.maxActive) this.maxActive = nowActive;
    return true;
  }
  // the scene gate is closing (Flow -> 0): the creek's own voices fade faster
  fadeCreek(k) {
    const t2 = this.retireAbs * this.retireAbs;
    for (let v = 0; v < this.n; v++) if (this.on[v] && !this.user[v]) {
      this.zr[v] *= k; this.zi[v] *= k;
      if (this.zr[v] * this.zr[v] + this.zi[v] * this.zi[v] < t2) { this.on[v] = 0; this.fading[v] = 0; }   // inaudible: free the voice
    }
  }
  // Micro tick: move gliding voices along their pitch rise.
  micro() {
    for (let v = 0; v < this.n; v++) {
      if (!this.on[v] || this.riseInc[v] === 0) continue;
      const prog = Math.min(1, this.age[v] * this.riseInc[v]);
      const w = this.w0[v] * (1 + this.rise[v] * prog), r = this.damp[v];
      this.c[v] = r * Math.cos(w); this.s[v] = r * Math.sin(w);
      if (prog >= 1) this.riseInc[v] = 0;
    }
  }
  render(L, R, n) {
    for (let v = 0; v < this.n; v++) {
      if (!this.on[v]) continue;
      let zr = this.zr[v], zi = this.zi[v], att = this.att[v], fade = this.fade[v];
      const c = this.c[v], s = this.s[v], ka = this.ka[v], gl = this.gl[v], gr = this.gr[v], fs = this.fadeStep[v];
      const i0 = this.start[v];
      let end = i0 + this.life[v]; if (end > n) end = n;
      for (let i = i0; i < end; i++) {
        const nr = zr * c - zi * s; zi = zr * s + zi * c; zr = nr;
        att += ka * (1 - att);
        fade -= fs;
        const y = zi * att * (fade > 0 ? fade : 0);
        L[i] += y * gl; R[i] += y * gr;
      }
      const done = end - i0;
      this.zr[v] = zr; this.zi[v] = zi; this.att[v] = att; this.fade[v] = fade;
      this.life[v] -= done; this.age[v] += done; this.start[v] = 0;
      if (this.life[v] <= 0 || fade <= 0) { this.on[v] = 0; this.fading[v] = 0; }
    }
  }
}

// =============================================================================
// BubbleScheduler: per-zone hazard accumulators. Each sample adds lambda/Fs;
// a bubble is born when the sum crosses an exponential threshold -ln(U).
// Bubble parameters come from their own stream, so timing and timbre are
// reproducible independently.
// =============================================================================
// Scale knob -> bubble radius multiplier: 0.6x .. 1.8x, bending up to 4x at the very top
function crScaleMul(S) {
  return CK.SCALE_MUL_MIN * Math.pow(CK.SCALE_MUL_MAX / CK.SCALE_MUL_MIN, S) * Math.pow(CK.SCALE_MUL_TOP / CK.SCALE_MUL_MAX, Math.pow(S, CK.SCALE_TOP_EXP));
}

// Move d steps through a scale (12-bit pitch-class mask, bit 0 = C) from MIDI note n.
function crStepScale(n, d, mask) {
  const dir = d > 0 ? 1 : -1;
  for (let k = 0; k < Math.abs(d); k++) {
    let g = 0;
    do { n += dir; g++; } while (!((mask >> (((n % 12) + 12) % 12)) & 1) && g < 12);
  }
  return n;
}

class CrBubbleScheduler {
  constructor() {
    this.acc = new Float32Array(3); this.thr = new Float32Array(3); this.inc = new Float32Array(3);
    this.rate = new Float32Array(3);
    this.bornZone = new Int32Array(CK.MAX_BIRTHS_PER_MICRO); this.bornOff = new Int32Array(CK.MAX_BIRTHS_PER_MICRO);
    this.nBorn = 0; this.over = 0;
    this.lnRmin = Math.log(CK.R_MIN_MM); this.lnRspan = Math.log(CK.R_MAX_MM) - Math.log(CK.R_MIN_MM);
    this.lnTauSpan = Math.log(CK.TAU_MAX / CK.TAU_MIN);
    this.ampScale = 0; this.scaleMul = 1;
    this.tuneLog2 = new Float32Array(4); this.nTune = 0; this.tuneAmt = 0;
    this.tuneMidi = new Float32Array(4);  // held notes as MIDI numbers
    this.variety = 0.5; this.scaleMask = 0xFFF; this.freeFlow = 1; this.activity = 1;
    this.rSkew = CK.R_SKEW;               // Tone: radius distribution skew ...
    this.toneMul = 1;                     // ... and radius multiplier at the extremes
    this.rateMul = new Float32Array([1, 1, 1]); this.radMul = new Float32Array([1, 1, 1]);   // eddies (wake)
    this.lastZone = 1;
  }
  // decay time of a bubble of radius rMm (longer for larger bubbles)
  tauFor(rMm) {
    const x = crClamp((Math.log(rMm) - this.lnRmin) / this.lnRspan, 0, CK.TAU_X_MAX);
    return CK.TAU_MIN * Math.exp(this.lnTauSpan * x);
  }
  // nearest pitch (any octave) of the held notes, in log2 Hz
  // Pitch for a tuned bubble (MIDI). Default: the held note in the octave
  // nearest the bubble's own pitch. Variety left of noon pulls it onto the held
  // note itself (fully left: one note); right of noon moves it up to +-7 steps
  // through the active scale.
  tunedMidi(m0, uA, uB) {
    let best = 0, bd = 1e9, bc = 0;
    for (let k = 0; k < this.nTune; k++) {
      const t = this.tuneMidi[k], c = t + 12 * Math.round((m0 - t) / 12), d = Math.abs(c - m0);
      if (d < bd) { bd = d; best = k; bc = c; }
    }
    const v = this.variety;
    if (v < 0.5) {
      if (uA < (0.5 - v) * 2) bc = this.tuneMidi[best];
    } else if (v > 0.5) {
      const r = (v - 0.5) * 2;
      if (uA < r) {
        const D = 1 + Math.round(r * (CK.VARIETY_STEPS - 1));
        let d = Math.min(D, Math.floor(uB * (2 * D + 1)) - D);
        bc = crStepScale(Math.round(bc), d, this.scaleMask);
      }
    }
    while (bc > CK.TUNE_MIDI_RANGE[1]) bc -= 12;
    while (bc < CK.TUNE_MIDI_RANGE[0]) bc += 12;
    return bc;
  }
  init(sr, rngT) {
    this.sr = sr;
    for (let z = 0; z < 3; z++) { this.acc[z] = 0; this.thr[z] = -Math.log(rngT.uniPos()); this.inc[z] = 0; this.rate[z] = 0; }
    this.over = 0;
  }
  // control tick: rates from the shared state
  update(fs, S) {
    let total = 0;
    for (let z = 0; z < 3; z++) {
      const B = fs.B[z];
      const lam = B > CK.B_MIN ? CK.RATE_MAX * CK.ZONE_RATE_W[z] * Math.pow(B, CK.RATE_EXP) * fs.gate * this.rateMul[z] * this.activity : 0;
      this.rate[z] = lam; this.inc[z] = lam / this.sr; total += lam;
    }
    this.scaleMul = crScaleMul(S);
    this.ampScale = CK.BUBBLE_GAIN / Math.sqrt(1 + total / CK.DENSITY_REF);
  }
  // collect births in the next n samples
  run(rngT, n) {
    this.nBorn = 0;
    for (let z = 0; z < 3; z++) {
      const inc = this.inc[z];
      if (inc <= 0) continue;
      let acc = this.acc[z];
      for (let i = 0; i < n; i++) {
        acc = Math.fround(acc + inc);            // float like the C++ engine: same birth samples
        if (acc >= this.thr[z]) {
          acc = Math.fround(acc - this.thr[z]);
          this.thr[z] = -Math.log(rngT.uniPos());
          if (this.nBorn < CK.MAX_BIRTHS_PER_MICRO) { this.bornZone[this.nBorn] = z; this.bornOff[this.nBorn] = i; this.nBorn++; }
          else this.over++;
        }
      }
      this.acc[z] = acc;
    }
  }
  // draw a bubble for zone z; ev (optional) overrides fields >= 0
  make(rngP, z, pool, off, ev) {
    const u0 = rngP.uni(), u1 = rngP.uni(), u2 = rngP.uni(), u3 = rngP.uni(), u4 = rngP.uni(), u5 = rngP.uni(), u6 = rngP.bip(), u7 = rngP.bip();
    const u8 = rngP.uni(), u9 = rngP.uni();
    const logr = this.lnRmin + this.lnRspan * Math.pow(u0, this.rSkew);
    let rMm = Math.exp(logr) * this.scaleMul * this.radMul[z] * this.toneMul;
    if (ev && ev.radiusMm > 0) rMm = ev.radiusMm;
    let f0 = Math.max(CK.F_MIN_HZ, 3000 / rMm);                   // f0 = 3 / r
    const tauBase = this.tauFor(rMm);
    let tau = tauBase * (1 + CK.TAU_JITTER * u6);
    if (ev && ev.tauSec > 0) tau = ev.tauSec;
    // note mode: held notes gather some of the creek's own bubbles onto their pitches
    // note mode: held notes gather the creek's bubbles onto their pitches; with
    // Free flow down, every bubble comes from pouring, so all of them are tuned
    const tuneP = this.tuneAmt > 0.01 ? Math.min(1, this.tuneAmt * CK.TUNE_MAX + (1 - this.freeFlow)) : 0;
    const tuned = !(ev && ev.radiusMm > 0) && this.nTune > 0 && u5 < tuneP;   // splash bubbles too; tuned drops keep their pitch
    if (tuned) {
      const m = this.tunedMidi(69 + 12 * Math.log2(f0 / 440), u8, u9);
      f0 = Math.max(CK.F_MIN_HZ, 440 * Math.pow(2, (m - 69) / 12)); tau *= CK.TUNE_TAU_MUL;
    }
    let amp = Math.pow(10, -CK.AMP_DB_RANGE * Math.pow(u1, CK.AMP_SKEW) / 20) *
      Math.min(CK.AMP_R_MAX, Math.pow(rMm, CK.AMP_R_EXP)) * this.ampScale;
    if (tauBase > CK.TAU_MAX) amp *= Math.sqrt(CK.TAU_MAX / tauBase);   // deep bubbles ring longer: same energy
    if (ev && ev.amp > 0) amp = ev.amp * CK.BUBBLE_GAIN;
    const attack = CK.ATTACK_MIN + (CK.ATTACK_MAX - CK.ATTACK_MIN) * u2;
    let rise = u3 < CK.RISE_PROB && !tuned ? CK.RISE_MAX * u4 : 0;
    if (ev && ev.rise >= 0) rise = ev.rise;
    let pan = CK.ZONE_PAN[z] + CK.ZONE_JITTER[z] * u7;
    if (ev && ev.pan >= -1) pan = ev.pan;
    return pool.spawn(f0, amp, tau, attack, rise, pan, off, !!(ev && ev.radiusMm > 0));
  }
}

// =============================================================================
// FlowBed (noise layers): continuous wash in three overlapping bands.
//   body  ~90 Hz – 1 kHz, mono (keeps mono playback solid)
//   mid   ~0.7 – 1.8 kHz, partly independent L/R
//   sheen ~2 – 4 kHz,     mostly independent L/R
// Gains and cutoffs drift slowly with the shared state; a faster "flutter"
// follows turbulence. Gains ramp linearly between control ticks.
// =============================================================================
class CrFlowBed {
  constructor() {
    this.hp = new CrHp1(); this.body = new CrSvf();
    this.midL = new CrSvf(); this.midR = new CrSvf(); this.hiL = new CrSvf(); this.hiR = new CrSvf();
    this.dBody = new CrDrift(); this.dMid = new CrDrift(); this.dHi = new CrDrift(); this.dCut = new CrDrift();
    this.flMid = new CrDrift(); this.flHi = new CrDrift();
    this.g = new Float64Array(3); this.gStep = new Float32Array(3);
    this.aM = Math.sqrt(CK.MID_CORR); this.bM = Math.sqrt(1 - CK.MID_CORR);
    this.aH = Math.sqrt(CK.HIGH_CORR); this.bH = Math.sqrt(1 - CK.HIGH_CORR);
  }
  init(sr, rngD) {
    this.sr = sr;
    this.hp.reset(); this.body.reset(); this.midL.reset(); this.midR.reset(); this.hiL.reset(); this.hiR.reset();
    this.hp.set(CK.BODY_HP_HZ, sr);
    this.dBody.init(rngD, 0.4, 3.0); this.dMid.init(rngD, 0.3, 2.5); this.dHi.init(rngD, 0.3, 2.0); this.dCut.init(rngD, 0.5, 4.0);
    this.flMid.init(rngD, CK.FLUTTER_MIN_SEC, CK.FLUTTER_MAX_SEC); this.flHi.init(rngD, CK.FLUTTER_MIN_SEC, CK.FLUTTER_MAX_SEC);
    this.g.fill(0); this.gStep.fill(0);
    this.body.set(CK.BODY_LP_HZ[0], CK.BODY_Q, sr); this.midL.set(CK.MID_HZ[0], CK.MID_Q, sr); this.midR.set(CK.MID_HZ[0], CK.MID_Q, sr);
    this.hiL.set(CK.HIGH_HZ[0], CK.HIGH_Q, sr); this.hiR.set(CK.HIGH_HZ[0], CK.HIGH_Q, sr);
  }
  // control tick. `span` = samples until the next tick (gain ramp length)
  update(fs, rngD, dt, span) {
    const R = fs.m[1], S = fs.m[2], E = fs.E[0], T = fs.T[0], e0 = fs.eta0.v;
    const db = this.dBody.tick(rngD, dt, 1), dm = this.dMid.tick(rngD, dt, 1), dh = this.dHi.tick(rngD, dt, 1), dc = this.dCut.tick(rngD, dt, 1);
    const fm = this.flMid.tick(rngD, dt, 1), fh = this.flHi.tick(rngD, dt, 1);
    const rx = crClamp((R - CK.BED_R_ON[0]) / (CK.BED_R_ON[1] - CK.BED_R_ON[0]), 0, 1), rOn = rx * rx * (3 - 2 * rx);
    const level = CK.BED_GAIN * Math.pow(E, CK.BED_E_EXP) * fs.gate * rOn / (1 + CK.BED_TOP_SQUASH * E * E);
    const depth = crLerp(CK.MOD_DEPTH[0], CK.MOD_DEPTH[1], crClamp(0.6 * R + 0.8 * T, 0, 1));
    const flut = depth * crClamp(0.3 + 1.5 * T, 0, 1);
    const tilt = Math.pow(2, -CK.SCALE_TILT * (2 * S - 1));       // larger Scale -> darker
    const gB = CK.BODY_GAIN * (0.8 + 0.4 * S) * (1 + depth * (0.6 * e0 + 0.4 * db));
    const gM = CK.MID_GAIN * (0.5 + 0.7 * R + 1.2 * T) * (1 + depth * (0.5 * e0 + 0.5 * dm)) * (1 + flut * fm);
    const gH = CK.HIGH_GAIN * (0.3 + 1.0 * R + 1.5 * T) * (1.2 - 0.4 * S) * (1 + depth * (0.4 * e0 + 0.6 * dh)) * (1 + flut * fh);
    this.gStep[0] = (level * Math.max(0, gB) - this.g[0]) / span;
    this.gStep[1] = (level * Math.max(0, gM) - this.g[1]) / span;
    this.gStep[2] = (level * Math.max(0, gH) - this.g[2]) / span;
    const cm = Math.pow(2, CK.CUT_DEPTH * (0.5 * dc + 0.5 * e0)) * tilt;
    const tt = crClamp(0.5 * T + 0.5 * R * E, 0, 1);
    this.body.set(crLerp(CK.BODY_LP_HZ[0], CK.BODY_LP_HZ[1], tt) * cm, CK.BODY_Q, this.sr);
    const fM = crLerp(CK.MID_HZ[0], CK.MID_HZ[1], tt) * cm, fH = crLerp(CK.HIGH_HZ[0], CK.HIGH_HZ[1], tt) * cm;
    this.midL.set(fM, CK.MID_Q, this.sr); this.midR.set(fM * 1.03, CK.MID_Q, this.sr);
    this.hiL.set(fH, CK.HIGH_Q, this.sr); this.hiR.set(fH * 0.97, CK.HIGH_Q, this.sr);
  }
  render(L, R, n, rc, rl, rr) {
    if (!CK.BED_ON) return;             // wash switched off (buffers stay zero)
    const hp = this.hp, b = this.body, mL = this.midL, mR = this.midR, hL = this.hiL, hR = this.hiR;
    const aM = this.aM, bM = this.bM, aH = this.aH, bH = this.bH;
    let g0 = this.g[0], g1 = this.g[1], g2 = this.g[2];
    const s0 = this.gStep[0], s1 = this.gStep[1], s2 = this.gStep[2];
    for (let i = 0; i < n; i++) {
      const c = rc.bip(), l = rl.bip(), r = rr.bip();
      g0 += s0; g1 += s1; g2 += s2;
      // body: one-pole HP then SVF low-pass (mono)
      let v = (c - hp.s) * hp.G, lp = v + hp.s; hp.s = lp + v;
      const x = c - lp;
      const yb = crSvfLp(b, x) * g0;
      const ym1 = crSvfBpN(mL, aM * c + bM * l) * g1, ym2 = crSvfBpN(mR, aM * c + bM * r) * g1;
      const yh1 = crSvfBpN(hL, aH * c + bH * l) * g2, yh2 = crSvfBpN(hR, aH * c + bH * r) * g2;
      L[i] = yb + ym1 + yh1; R[i] = yb + ym2 + yh2;
    }
    this.g[0] = g0; this.g[1] = g1; this.g[2] = g2;
  }
}
function crSvfLp(f, x) {
  const v3 = x - f.ic2, v1 = f.a1 * f.ic1 + f.a2 * v3, v2 = f.ic2 + f.a2 * f.ic1 + f.a3 * v3;
  f.ic1 = 2 * v1 - f.ic1; f.ic2 = 2 * v2 - f.ic2;
  return v2;
}
function crSvfBpN(f, x) { // band-pass normalised to unity gain at the centre
  const v3 = x - f.ic2, v1 = f.a1 * f.ic1 + f.a2 * v3, v2 = f.ic2 + f.a2 * f.ic1 + f.a3 * v3;
  f.ic1 = 2 * v1 - f.ic1; f.ic2 = 2 * v2 - f.ic2;
  return v1 * f.k;
}

// =============================================================================
// Hazard: one Poisson event stream with a time-varying rate (same scheme as the
// bubble scheduler: float accumulator, exponential thresholds, exact samples).
// =============================================================================
class CrHazard {
  constructor(max) { this.acc = 0; this.thr = 1; this.inc = 0; this.off = new Int32Array(max); this.n = 0; this.over = 0; }
  init(rng) { this.acc = 0; this.thr = Math.fround(-Math.log(rng.uniPos())); this.inc = 0; this.n = 0; this.over = 0; }
  setRate(lam, sr) { this.inc = Math.fround(lam / sr); }
  run(rng, n) {
    this.n = 0;
    if (this.inc <= 0) return;
    let acc = this.acc;
    for (let i = 0; i < n; i++) {
      acc = Math.fround(acc + this.inc);
      if (acc >= this.thr) {
        acc = Math.fround(acc - this.thr);
        this.thr = Math.fround(-Math.log(rng.uniPos()));
        if (this.n < this.off.length) this.off[this.n++] = i; else this.over++;
      }
    }
    this.acc = acc;
  }
}

// =============================================================================
// Eddies: 2-4 overlapping slow swells (bounded random, not an LFO). They have
// no audio of their own: they make the rock wake surge (energy, bubble rate,
// larger and lower bubbles). Scale slows them down.
// =============================================================================
class CrEddies {
  constructor() {
    const N = CK.EDDY_SLOTS;
    this.t = new Float32Array(N); this.dur = new Float32Array(N); this.peak = new Float32Array(N); this.wait = new Float32Array(N);
    this.active = new Uint8Array(N); this.value = 0;
  }
  init(rng) {
    for (let k = 0; k < this.t.length; k++) { this.active[k] = 0; this.t[k] = 0; this.dur[k] = 1; this.peak[k] = 0; this.wait[k] = CK.EDDY_GAP_SEC[1] * rng.uni(); }
    this.value = 0;
  }
  // control tick; mul = Scale time multiplier, depth = how strong swells are
  tick(rng, dt, mul, depth) {
    let sum = 0;
    for (let k = 0; k < this.t.length; k++) {
      if (!this.active[k]) {
        this.wait[k] -= dt;
        if (this.wait[k] <= 0) {
          const u0 = rng.uni(), u1 = rng.uni();
          this.active[k] = 1; this.t[k] = 0;
          this.dur[k] = crLerp(CK.EDDY_DUR_SEC[0], CK.EDDY_DUR_SEC[1], u0) * mul;
          this.peak[k] = 0.4 + 0.6 * u1;
        }
      }
      if (this.active[k]) {
        this.t[k] += dt;
        if (this.t[k] >= this.dur[k]) {
          this.active[k] = 0;
          this.wait[k] = crLerp(CK.EDDY_GAP_SEC[0], CK.EDDY_GAP_SEC[1], rng.uni()) * mul;
        } else {
          const s = Math.sin(Math.PI * this.t[k] / this.dur[k]);
          sum += this.peak[k] * s * s;
        }
      }
    }
    this.value = crClamp(sum, 0, 1) * depth;
    return this.value;
  }
}

// =============================================================================
// Small waterfalls at the drop zone. Each parent event: a soft low "plop"
// (falling tone, no noise), an entrainment impulse into the drop zone, and -
// when Aeration permits - 2-8 bubble children over the next 10-120 ms, which
// the bubble bank renders. Overlapping splashes share a bounded energy budget.
// =============================================================================
class CrFalls {
  constructor() { this.hz = new CrHazard(4); this.pool = new CrBubblePool(CK.FALLS_VOICES, CK.FALLS_VOICES + 2, false); this.energy = 0; this.count = 0; }
  init(sr, rng) {
    this.sr = sr; this.hz.init(rng); this.pool.init(sr); this.pool.reset(); this.energy = 0; this.count = 0;
    this.kE = Math.exp(-1 / (CK.CONTROL_HZ * CK.FALLS_ENERGY_SEC));
  }
  update(fs, activity) {
    const E = fs.E[2];
    const lam = E > 0.002 ? crLerp(CK.FALLS_RATE[0], CK.FALLS_RATE[1], Math.pow(E, CK.FALLS_RATE_EXP)) * fs.gate * activity : 0;
    this.hz.setRate(lam, this.sr); this.rate = lam;
    this.energy *= this.kE;
  }
  // births in this segment -> plops, splash impulses and queued children
  run(rng, n, eng) {
    this.hz.run(rng, n);
    const fs = eng.flow, A = fs.m[3], E = fs.E[2], R = fs.m[1];
    for (let b = 0; b < this.hz.n; b++) {
      const off = this.hz.off[b];
      const u0 = rng.uni(), u1 = rng.uni(), u2 = rng.uni(), u3 = rng.uni(), u4 = rng.uni(), u5 = rng.bip();
      // rougher water: more uneven splashes
      let a = crLerp(0.55, 0.15, R) + crLerp(0.45, 0.85, R) * u0;
      a *= 0.5 + 0.5 * E;
      a /= 1 + CK.FALLS_ENERGY_K * this.energy;
      this.energy += a;
      this.count++;
      const pan = CK.ZONE_PAN[2] + CK.ZONE_JITTER[2] * u5;
      const f = crLerp(CK.FALLS_PLOP_HZ[0], CK.FALLS_PLOP_HZ[1], u1) / eng.sched.scaleMul;
      this.pool.spawn(f, a * CK.FALLS_PLOP_GAIN, crLerp(CK.FALLS_TAU_SEC[0], CK.FALLS_TAU_SEC[1], u2),
        crLerp(CK.FALLS_ATTACK_SEC[0], CK.FALLS_ATTACK_SEC[1], u3), CK.FALLS_PLOP_GLIDE, pan, off);
      fs.addSplash(2, a * CK.FALLS_SPLASH_J);
      eng.surface.boost += a * CK.SURF_SPLASH;
      const nc = A > 0.02 ? Math.min(CK.FALLS_CHILDREN[1], Math.round(crLerp(CK.FALLS_CHILDREN[0], CK.FALLS_CHILDREN[1], A * u4))) : 0;
      for (let k = 0; k < CK.FALLS_CHILDREN[1]; k++) {
        const d = rng.uni(), ca = rng.uni(), pj = rng.bip();
        if (k >= nc) continue;
        const c = eng.tmpEvent;
        c.type = CR_EV_BUBBLE; c.zone = 2; c.radiusMm = 0; c.tauSec = 0; c.rise = -1;
        c.amp = a * CK.FALLS_CHILD_GAIN * crLerp(CK.FALLS_CHILD_AMP[0], CK.FALLS_CHILD_AMP[1], ca);
        c.pan = crClamp(pan + 0.12 * pj, -1, 1);
        eng.queue.push(c, eng.sample + off + Math.round(crLerp(CK.FALLS_CHILD_MS[0], CK.FALLS_CHILD_MS[1], d) * 0.001 * this.sr));
      }
    }
  }
}

// =============================================================================
// Surface detail: sparse, quiet, very short high ticks (2-9 kHz), more near
// the drop and at turbulence peaks, briefly more after splashes. A slow drift
// makes them come in loose bursts rather than a constant crackle.
// =============================================================================
class CrSurface {
  constructor() { this.hz = new CrHazard(4); this.pool = new CrBubblePool(CK.SURF_VOICES, CK.SURF_VOICES + 2, false); this.drift = new CrDrift(); this.boost = 0; this.count = 0; }
  init(sr, rng) {
    this.sr = sr; this.hz.init(rng); this.pool.init(sr); this.pool.reset(); this.boost = 0; this.count = 0;
    this.drift.init(rng, 0.3, 2.5);
    this.kB = Math.exp(-1 / (CK.CONTROL_HZ * CK.SURF_BOOST_SEC));
    this.lnHz = Math.log(CK.SURF_HZ[1] / CK.SURF_HZ[0]);
  }
  update(fs, rng, dt, eddy, activity) {
    const d = this.drift.tick(rng, dt, 1);
    const Tm = CK.SURF_ZONE_W[0] * fs.T[0] + CK.SURF_ZONE_W[1] * fs.T[1] + CK.SURF_ZONE_W[2] * fs.T[2];
    const x = crClamp(1.6 * Tm + 0.3 * eddy + this.boost, 0, 1);
    const lam = fs.gate > 0 ? crLerp(CK.SURF_RATE[0], CK.SURF_RATE[1], x) * (0.35 + 0.65 * 0.5 * (d + 1)) * fs.gate * activity : 0;
    this.hz.setRate(lam, this.sr); this.rate = lam;
    this.boost *= this.kB;
  }
  run(rng, n) {
    this.hz.run(rng, n);
    for (let b = 0; b < this.hz.n; b++) {
      const u0 = rng.uni(), u1 = rng.uni(), u2 = rng.uni(), u3 = rng.uni(), u4 = rng.uni(), u5 = rng.bip(), u6 = rng.bip();
      const z = u0 < CK.SURF_ZONE_W[0] ? 0 : (u0 < CK.SURF_ZONE_W[0] + CK.SURF_ZONE_W[1] ? 1 : 2);
      const f = CK.SURF_HZ[0] * Math.exp(this.lnHz * u1);
      const amp = CK.SURF_GAIN * Math.pow(10, -CK.SURF_DB_RANGE * Math.pow(u2, CK.SURF_SKEW) / 20);
      this.pool.spawn(f, amp, crLerp(CK.SURF_TAU_SEC[0], CK.SURF_TAU_SEC[1], u3), crLerp(CK.SURF_ATTACK_SEC[0], CK.SURF_ATTACK_SEC[1], u4),
        0.08 * u5, CK.ZONE_PAN[z] + CK.ZONE_JITTER[z] * u6, this.hz.off[b]);
      this.count++;
    }
  }
}

// =============================================================================
// SpatialMixer: sums the layer buffers with smooth diagnostic mutes, then
// applies stereo width (mid/side). Zone placement happens per event / band.
// =============================================================================
class CrSpatialMixer {
  constructor() { this.g = new Float64Array(CK_LAYERS); this.target = new Float32Array(CK_LAYERS); this.w = 1; this.wTarget = 1; }
  init(sr) {
    this.kMute = 1 - Math.exp(-1 / (CK.MUTE_SMOOTH_SEC * sr));
    this.kW = 1 - Math.exp(-1 / (CK.UTIL_SMOOTH_SEC * sr));
    this.g.fill(1); this.target.fill(1); this.w = this.wTarget;
  }
  setMask(mask, width) {
    for (let l = 0; l < CK_LAYERS; l++) this.target[l] = (mask >> l) & 1 ? 1 : 0;
    this.wTarget = width;
  }
  mix(bufL, bufR, outL, outR, off, n) {
    for (let i = 0; i < n; i++) { outL[off + i] = 0; outR[off + i] = 0; }
    for (let l = 0; l < CK_LAYERS; l++) {
      const bl = bufL[l], br = bufR[l], t = this.target[l];
      let g = this.g[l];
      if (g === 0 && t === 0) continue;
      for (let i = 0; i < n; i++) { g += this.kMute * (t - g); outL[off + i] += bl[i] * g; outR[off + i] += br[i] * g; }
      if (t === 0 && g < 1e-5) g = 0;
      if (t === 1 && g > 0.99999) g = 1;
      this.g[l] = g;
    }
    let w = this.w;
    const wt = this.wTarget, kW = this.kW;
    for (let i = 0; i < n; i++) {
      w += kW * (wt - w);
      const L = outL[off + i], R = outR[off + i], m = 0.5 * (L + R), s = 0.5 * (L - R) * w;
      outL[off + i] = m + s; outR[off + i] = m - s;
    }
    this.w = w;
  }
}

// =============================================================================
// OutputStage: DC blocker (~25 Hz), smoothed gain, linked peak limiter with
// instant attack and smooth release; output never exceeds LIMIT_CEIL.
// =============================================================================
class CrOutputStage {
  constructor() {
    this.x1 = new Float64Array(2); this.y1 = new Float64Array(2); this.g = 1; this.gTarget = 1; this.env = 0;
    this.f1 = new Float64Array(2); this.f2 = new Float64Array(2);   // low-pass state (TPT SVF, as TouchPulsar)
    this.fcT = 0; this.fcLog = 0; this.fk = 1.414; this.a1 = 0; this.a2 = 0; this.a3 = 0;
  }
  init(sr) {
    this.sr = sr;
    this.kF = Math.min(1, CK.MICRO / (CK.FILTER_SMOOTH_SEC * sr));
    this.rDc = Math.exp(-CR_TWO_PI * CK.DC_HZ / sr);
    this.kG = 1 - Math.exp(-1 / (CK.UTIL_SMOOTH_SEC * sr));
    this.rel = Math.exp(-1 / (CK.LIMIT_RELEASE_SEC * sr));
    this.reset();
  }
  reset() {
    this.x1.fill(0); this.y1.fill(0); this.env = 0; this.g = this.gTarget; this.limited = 0; this.peak = 0;
    this.f1.fill(0); this.f2.fill(0); this.fcLog = this.fcT; this.micro();
  }
  // cutoff / resonance 0..1 (cutoff 1 = open)
  setFilter(cut, res) {
    this.fcT = Math.log(crClamp(CK.FILTER_HZ[0] * Math.pow(CK.FILTER_HZ[1] / CK.FILTER_HZ[0], cut), 20, 0.45 * this.sr));
    this.fk = 1 / (CK.FILTER_Q[0] + res * res * (CK.FILTER_Q[1] - CK.FILTER_Q[0]));
  }
  // micro tick: cutoff glides (~5 ms), coefficients follow
  micro() {
    this.fcLog += (this.fcT - this.fcLog) * this.kF;
    const w = Math.PI * Math.exp(this.fcLog) / this.sr, g = Math.sin(w) / Math.cos(w);
    this.a1 = 1 / (1 + g * (g + this.fk)); this.a2 = g * this.a1; this.a3 = g * this.a2;
  }
  setGain(g) { this.gTarget = g; }
  process(L, R, off, n) {
    const rDc = this.rDc, rel = this.rel, ceil = CK.LIMIT_CEIL, kG = this.kG, gt = this.gTarget, a1 = this.a1, a2 = this.a2, a3 = this.a3;
    let xl = this.x1[0], xr = this.x1[1], yl = this.y1[0], yr = this.y1[1], g = this.g, env = this.env;
    for (let i = off; i < off + n; i++) {
      g += kG * (gt - g);
      const a = L[i] * g, b = R[i] * g;
      yl = a - xl + rDc * yl; xl = a;
      yr = b - xr + rDc * yr; xr = b;
      // low-pass
      let v3 = yl - this.f2[0], v1 = a1 * this.f1[0] + a2 * v3, v2 = this.f2[0] + a2 * this.f1[0] + a3 * v3;
      this.f1[0] = 2 * v1 - this.f1[0]; this.f2[0] = 2 * v2 - this.f2[0]; const ol = v2;
      v3 = yr - this.f2[1]; v1 = a1 * this.f1[1] + a2 * v3; v2 = this.f2[1] + a2 * this.f1[1] + a3 * v3;
      this.f1[1] = 2 * v1 - this.f1[1]; this.f2[1] = 2 * v2 - this.f2[1]; const or = v2;
      const p = Math.max(Math.abs(ol), Math.abs(or));
      env = p > env ? p : env * rel + p * (1 - rel);
      let k = 1;
      if (env > ceil) { k = ceil / env; this.limited++; }
      L[i] = ol * k; R[i] = or * k;
      if (p * k > this.peak) this.peak = p * k;
    }
    this.x1[0] = xl; this.x1[1] = xr; this.y1[0] = yl; this.y1[1] = yr; this.g = g; this.env = env;
  }
}

// =============================================================================
// CreekEngine
// =============================================================================
class CreekMacros {
  constructor() {
    this.flow = 0.2; this.roughness = 0.25; this.scale = 0.2; this.aeration = 0.3;   // "Quiet trickle"
    this.gain = 1; this.width = 1; this.mask = 0x1F; this.hold = false;
    this.tuneHz = [0, 0, 0, 0];   // note mode: held note pitches (0 = none)
    this.tune = 0;                // 1 while notes are held
    this.freeFlow = 1;            // note mode: how much the creek plays by itself (0 = still)
    this.variety = 0.5;           // tuned pitches: 0 one note .. 0.5 default .. 1 across the scale
    this.decay = 0.5;             // decay times: 0 = 1/4 .. 0.5 = 1x .. 1 = 4x
    this.scaleMask = 0xFFF;       // active scale as pitch classes (bit 0 = C)
    this.tone = 0.5;              // 0 dark (more big bubbles) .. 0.5 default .. 1 bright (more small ones)
    this.attack = CK.TUNE_ATTACK_SEC;   // note pour swell-in time (s)
    this.release = CK.TUNE_RELEASE_SEC; // note pour tail (s)
    this.cutoff = 1;              // low-pass: 0 = 30 Hz .. 1 = 18 kHz (open)
    this.resonance = 0;           // 0 = Q 0.7 .. 1 = Q 20
  }
}

class CreekEngine {
  constructor(sr, seed) {
    this.rng = []; for (let i = 0; i <= CR_S_TRIGGER; i++) this.rng.push(new CrRng(0));
    this.flow = new CrFlowState(); this.queue = new CrEventQueue(CK.QUEUE_SIZE);
    this.sched = new CrBubbleScheduler(); this.pool = new CrBubblePool();
    this.bed = new CrFlowBed(); this.mixer = new CrSpatialMixer(); this.out = new CrOutputStage();
    this.eddies = new CrEddies(); this.falls = new CrFalls(); this.surface = new CrSurface();
    this.tmpEvent = new CrEvent();
    this.bufL = []; this.bufR = [];
    for (let l = 0; l < CK_LAYERS; l++) { this.bufL.push(new Float32Array(CK.MICRO)); this.bufR.push(new Float32Array(CK.MICRO)); }
    this.macros = new CreekMacros();
    this.init(sr || 48000, seed === undefined ? 1 : seed);
  }
  init(sr, seed) {
    this.sr = sr;
    this.pool.init(sr); this.mixer.init(sr); this.out.init(sr);
    this.kStone = Math.exp(-1 / (CK.CONTROL_HZ * CK.STONE_SURGE_SEC));
    this.kTuneA = 1 - Math.exp(-1 / (CK.CONTROL_HZ * CK.TUNE_ATTACK_SEC));
    this.kTuneR = 1 - Math.exp(-1 / (CK.CONTROL_HZ * CK.TUNE_RELEASE_SEC));
    this.reset(seed);
  }
  reset(seed) {
    this.seed = seed >>> 0;
    for (let i = 0; i < this.rng.length; i++) this.rng[i].seed(crStreamSeed(this.seed, i));
    this.sample = 0; this.tickIndex = 0; this.untilTick = 0;
    this.flow.init(this.sr, this.rng[CR_S_DRIFT]);
    this.queue.reset(); this.pool.reset();
    this.sched.init(this.sr, this.rng[CR_S_BTIME]); this.sched.tuneAmt = 0; this.sched.nTune = 0;
    this.freeS = 1; this.activity = 1; this.stoneS = 0; this.stones = 0; this.creekFade = 1;
    if (CK.BED_ON) this.bed.init(this.sr, this.rng[CR_S_BED_DRIFT]);
    this.eddies.init(this.rng[CR_S_EDDY]);
    this.falls.init(this.sr, this.rng[CR_S_FALLS]);
    this.surface.init(this.sr, this.rng[CR_S_SURFACE]);
    this.sched.rateMul.fill(1); this.sched.radMul.fill(1);
    this.mixer.init(this.sr); this.out.reset();
    this.applyMacros(true);
    this.snapNext = true;      // the first setMacros() after a reset jumps instead of gliding
  }
  // values: {flow, roughness, scale, aeration, gain, width, mask, hold}; nonfinite input is ignored
  setMacros(v) {
    const m = this.macros;
    const fin = (x, lo, hi, d) => (typeof x === 'number' && isFinite(x)) ? crClamp(x, lo, hi) : d;
    m.flow = fin(v.flow, 0, 1, m.flow); m.roughness = fin(v.roughness, 0, 1, m.roughness);
    m.scale = fin(v.scale, 0, 1, m.scale); m.aeration = fin(v.aeration, 0, 1, m.aeration);
    m.gain = fin(v.gain, 0, 4, m.gain); m.width = fin(v.width, 0, CK.WIDTH_MAX, m.width);
    if (v.mask !== undefined) m.mask = v.mask & 0x1F;
    if (v.hold !== undefined) m.hold = !!v.hold;
    if (v.tuneHz) for (let k = 0; k < 4; k++) { const h = v.tuneHz[k]; m.tuneHz[k] = typeof h === 'number' && isFinite(h) && h > 20 && h < 20000 ? h : 0; }
    if (v.tune !== undefined) m.tune = v.tune ? 1 : 0;
    m.freeFlow = fin(v.freeFlow, 0, 1, m.freeFlow); m.variety = fin(v.variety, 0, 1, m.variety); m.decay = fin(v.decay, 0, 1, m.decay);
    m.tone = fin(v.tone, 0, 1, m.tone);
    m.attack = fin(v.attack, 0.001, 10, m.attack); m.release = fin(v.release, 0.005, 20, m.release);
    m.cutoff = fin(v.cutoff, 0, 1, m.cutoff); m.resonance = fin(v.resonance, 0, 1, m.resonance);
    if (v.scaleMask !== undefined) m.scaleMask = (v.scaleMask & 0xFFF) || 0xFFF;
    this.applyMacros(this.snapNext);
    this.snapNext = false;
  }
  applyMacros(snap) {
    const m = this.macros;
    this.flow.setMacros(m.flow, m.roughness, m.scale, m.aeration, snap);
    this.flow.hold = m.hold;
    this.mixer.setMask(m.mask, m.width);
    this.out.setGain(m.gain);
    this.out.setFilter(m.cutoff, m.resonance);
    if (snap) { this.out.fcLog = this.out.fcT; this.out.micro(); }
    if (snap) { this.mixer.w = m.width; this.out.g = m.gain; this.mixer.g.set(this.mixer.target); }
    const sc = this.sched;
    // held notes; after release the last ones stay, so the release tail keeps their pitches
    if (m.tune && m.tuneHz.some((h) => h > 0)) {
      sc.nTune = 0;
      for (let k = 0; k < 4; k++) if (m.tuneHz[k] > 0) { sc.tuneLog2[sc.nTune] = Math.log2(m.tuneHz[k]); sc.tuneMidi[sc.nTune++] = 69 + 12 * Math.log2(m.tuneHz[k] / 440); }
    }
    this.kTuneA = 1 - Math.exp(-1 / (CK.CONTROL_HZ * m.attack));
    this.kTuneR = 1 - Math.exp(-1 / (CK.CONTROL_HZ * m.release));
    sc.variety = m.variety; sc.scaleMask = m.scaleMask;
    const tb = 2 * m.tone - 1;                                    // -1 dark .. +1 bright
    sc.rSkew = CK.R_SKEW * Math.pow(CK.TONE_SKEW_RANGE, tb);
    sc.toneMul = Math.pow(2, (tb < 0 ? CK.TONE_OCT[0] : -CK.TONE_OCT[1]) * Math.pow(Math.abs(tb), CK.TONE_OCT_EXP));
    const tm = Math.pow(2, CK.DECAY_OCT * (2 * m.decay - 1));
    this.pool.tauMul = tm; this.falls.pool.tauMul = tm; this.surface.pool.tauMul = tm;
    if (snap) this.freeS = m.freeFlow;
  }
  // ev: CrEvent-like {type, zone, radiusMm, amp, tauSec, rise, pan, delaySec}; unset (<0 / 0) fields are drawn randomly
  trigger(ev) {
    const e = new CrEvent();
    e.type = ev.type | 0; e.zone = crClamp(ev.zone === undefined ? 1 : ev.zone | 0, 0, 2);
    e.radiusMm = ev.radiusMm > 0 ? crClamp(ev.radiusMm, 0.05, 50) : 0;
    e.amp = ev.amp > 0 ? Math.min(ev.amp, 4) : 0; e.tauSec = ev.tauSec > 0 ? crClamp(ev.tauSec, 0.002, 1) : 0;
    e.rise = ev.rise >= 0 ? Math.min(ev.rise, 1) : -1; e.pan = ev.pan >= -1 ? Math.min(ev.pan, 1) : -2;
    const d = ev.delaySec > 0 ? ev.delaySec : 0;
    return this.queue.push(e, this.sample + Math.round(d * this.sr));
  }
  controlTick() {
    // eddies first: they shape this tick's wake energy (muting the layer removes them)
    const fs = this.flow, R = fs.m[1], T1 = fs.T[1];
    const eMul = crLerp(CK.EDDY_TIME_MUL[0], CK.EDDY_TIME_MUL[1], fs.m[2]);
    const depth = crLerp(CK.EDDY_DEPTH[0], CK.EDDY_DEPTH[1], crClamp(0.7 * R + 0.6 * T1, 0, 1)) * fs.gate * this.mixer.target[CK_L_EDDIES];
    const ed = this.eddies.tick(this.rng[CR_S_EDDY], fs.dt, eMul, depth);
    fs.eddy = ed;
    this.flow.tick(this.rng[CR_S_DRIFT]);
    const k = this.tickIndex;
    const next = Math.floor((k + 1) * this.sr / CK.CONTROL_HZ) - Math.floor(k * this.sr / CK.CONTROL_HZ);
    this.untilTick = next; this.tickIndex++;
    this.sched.rateMul[1] = 1 + CK.EDDY_RATE * ed; this.sched.radMul[1] = 1 + CK.EDDY_RADIUS * ed;
    this.sched.activity = this.activity;
    this.sched.update(this.flow, this.flow.m[2]);
    this.falls.update(this.flow, this.activity);
    this.surface.update(this.flow, this.rng[CR_S_SURFACE], this.flow.dt, ed, this.activity);
    const tt = this.macros.tune ? 1 : 0;
    this.sched.tuneAmt += (tt > this.sched.tuneAmt ? this.kTuneA : this.kTuneR) * (tt - this.sched.tuneAmt);
    // Free flow: the creek plays by itself only this much; holding notes pours
    this.freeS += this.flow.kM * (this.macros.freeFlow - this.freeS);
    this.sched.freeFlow = this.freeS;
    const g = this.flow.gate;
    this.creekFade = g >= 1 ? 1 : Math.exp(-(1 - g) * CK.MICRO / (CK.GATE_FADE_SEC * this.sr));
    this.stoneS *= this.kStone;                                   // a thrown stone's disturbance settles
    const st = this.stoneS * this.stoneS;                        // squared: settles fully to still water
    // the surge matters in quiet water; a full creek needs little extra (keeps headroom)
    this.activity = Math.max(this.freeS, this.sched.tuneAmt, st) * (1 + CK.STONE_SURGE_RATE * st * (1 - 0.8 * this.flow.m[0]));
    if (CK.BED_ON) this.bed.update(this.flow, this.rng[CR_S_BED_DRIFT], this.flow.dt, next);
  }
  process(L, R, n) {
    let pos = 0;
    while (pos < n) {
      if (this.untilTick === 0) this.controlTick();
      const intoMicro = this.sample % CK.MICRO;
      if (intoMicro === 0) {
        this.pool.micro(); this.falls.pool.micro(); this.surface.pool.micro(); this.out.micro();
        if (this.creekFade < 1) { this.pool.fadeCreek(this.creekFade); this.falls.pool.fadeCreek(this.creekFade); this.surface.pool.fadeCreek(this.creekFade); }
      }
      let seg = CK.MICRO - intoMicro;
      if (seg > this.untilTick) seg = this.untilTick;
      if (seg > n - pos) seg = n - pos;
      this.renderSegment(L, R, pos, seg);
      pos += seg; this.sample += seg; this.untilTick -= seg;
    }
  }
  renderSegment(L, R, off, n) {
    const bl = this.bufL, br = this.bufR;
    for (let l = 0; l < CK_LAYERS; l++) for (let i = 0; i < n; i++) { bl[l][i] = 0; br[l][i] = 0; }
    // events: scheduled births, then queued events due in this segment
    const s = this.sched;
    s.run(this.rng[CR_S_BTIME], n);
    for (let b = 0; b < s.nBorn; b++) s.make(this.rng[CR_S_BPARAM], s.bornZone[b], this.pool, s.bornOff[b], null);
    this.falls.run(this.rng[CR_S_FALLS], n, this);
    this.surface.run(this.rng[CR_S_SURFACE], n);
    const q = this.queue, end = this.sample + n;
    for (let i = 0; i < q.used.length; i++) {
      if (!q.used[i]) continue;
      const e = q.ev[i];
      if (e.at >= end) continue;
      q.used[i] = 0;
      const o = e.at > this.sample ? e.at - this.sample : 0;
      if (e.type === CR_EV_BUBBLE) s.make(this.rng[CR_S_TRIGGER], e.zone, this.pool, o, e);
      else if (e.type === CR_EV_DROP) this.drop(e, o);
      else if (e.type === CR_EV_STONE) this.stone(e, o);
    }
    this.bed.render(bl[CK_L_FLOW], br[CK_L_FLOW], n, this.rng[CR_S_NOISE_C], this.rng[CR_S_NOISE_L], this.rng[CR_S_NOISE_R]);
    this.pool.render(bl[CK_L_BUBBLES], br[CK_L_BUBBLES], n);
    this.falls.pool.render(bl[CK_L_FALLS], br[CK_L_FALLS], n);
    this.surface.pool.render(bl[CK_L_SURFACE], br[CK_L_SURFACE], n);
    this.mixer.mix(bl, br, L, R, off, n);
    this.out.process(L, R, off, n);
  }
  // note mode: a tuned drop = one bubble at the note plus a few quieter
  // follow-up bubbles (same pitch or an octave up) over the next 12-90 ms
  drop(e, o) {
    const r = this.rng[CR_S_TRIGGER], rMm = e.radiusMm > 0 ? e.radiusMm : 3;
    const u0 = r.uni(), u1 = r.bip();
    const pan = e.pan >= -1 ? e.pan : CK.DROP_PAN * u1, amp = e.amp > 0 ? e.amp : 0.7;
    this.pool.spawn(Math.max(CK.F_MIN_HZ, 3000 / rMm), amp * CK.DROP_GAIN * CK.BUBBLE_GAIN, this.sched.tauFor(rMm) * CK.DROP_TAU_MUL,
      CK.ATTACK_MIN, CK.DROP_RISE, pan, o, true);
    const nc = 1 + Math.floor(u0 * CK.DROP_CHILDREN * Math.min(1, amp));
    for (let k = 0; k < CK.DROP_CHILDREN; k++) {
      const d = r.uni(), a = r.uni(), oc = r.uni(), pj = r.bip();
      if (k >= nc) continue;
      const c = new CrEvent();   // (allocation-free in C++; JS only)
      c.type = CR_EV_BUBBLE; c.zone = 0; c.radiusMm = oc < 0.35 ? rMm * 0.5 : rMm;
      c.amp = amp * CK.DROP_GAIN * crLerp(CK.DROP_CHILD_AMP[0], CK.DROP_CHILD_AMP[1], a);
      c.rise = 0; c.tauSec = 0; c.pan = crClamp(pan + 0.15 * pj, -1, 1);
      this.queue.push(c, this.sample + o + Math.round(crLerp(CK.DROP_CHILD_MS[0], CK.DROP_CHILD_MS[1], d) * 0.001 * this.sr));
    }
  }
  // throw a stone: a deep falling plop, a burst of larger bubbles spreading from the
  // centre over ~0.35 s, and a disturbance (air in every zone, more events) that settles
  stone(e, o) {
    const r = this.rng[CR_S_TRIGGER], amp = e.amp > 0 ? Math.min(1, e.amp) : 0.8;
    const u0 = r.uni(), u1 = r.uni(), u2 = r.uni(), u3 = r.bip();
    const sm = this.sched.scaleMul;
    this.falls.pool.spawn(crLerp(CK.STONE_PLOP_HZ[0], CK.STONE_PLOP_HZ[1], u0) / sm, amp * CK.STONE_PLOP_GAIN,
      crLerp(CK.STONE_PLOP_TAU_SEC[0], CK.STONE_PLOP_TAU_SEC[1], u1), CK.FALLS_ATTACK_SEC[1], CK.STONE_PLOP_GLIDE, 0.15 * u3, o, true);
    const n = Math.round(crLerp(CK.STONE_BUBBLES[0], CK.STONE_BUBBLES[1], amp * (0.7 + 0.3 * u2)));
    const lnR = Math.log(CK.STONE_RADIUS_MM[1] / CK.STONE_RADIUS_MM[0]);
    for (let k = 0; k < CK.STONE_BUBBLES[1]; k++) {
      const d = r.uni(), rr = r.uni(), a = r.uni(), pj = r.bip();
      if (k >= n) continue;
      const c = this.tmpEvent;
      c.type = CR_EV_BUBBLE; c.zone = 0; c.tauSec = 0; c.rise = -1;
      c.radiusMm = CK.STONE_RADIUS_MM[0] * Math.exp(lnR * rr) * sm;
      c.amp = amp * crLerp(CK.STONE_BUBBLE_AMP[0], CK.STONE_BUBBLE_AMP[1], a) * (1 - 0.6 * d);   // later = quieter
      c.pan = crClamp(0.5 * pj * (0.3 + d), -1, 1);                                              // spreading out
      this.queue.push(c, this.sample + o + Math.round(d * d * CK.STONE_SPREAD_SEC * this.sr));
    }
    for (let z = 0; z < 3; z++) this.flow.addSplash(z, amp * CK.STONE_J);
    this.stoneS = Math.min(1, this.stoneS + amp);
    this.surface.boost += amp * CK.STONE_SURFACE;
    this.stones++;
  }
  stats() {
    const p = this.pool;
    return {
      births: p.births, drops: p.drops + this.sched.over, steals: p.steals, active: p.active(), maxActive: p.maxActive,
      meanF: p.births ? Math.pow(2, p.sumLog2F / p.births) : 0, sumLog2F: p.sumLog2F, queueDrops: this.queue.drops,
      limited: this.out.limited, peak: this.out.peak,
      E: Array.from(this.flow.E), T: Array.from(this.flow.T), B: Array.from(this.flow.B),
      rate: this.sched.rate[0] + this.sched.rate[1] + this.sched.rate[2], gate: this.flow.gate,
      eddy: this.flow.eddy, splashes: this.falls.count, surface: this.surface.count, stones: this.stones,
      splashRate: this.falls.rate, surfaceRate: this.surface.rate,
    };
  }
}

if (typeof module !== 'undefined') {
  module.exports = { CR_LOG, CrRng, crStreamSeed, CreekEngine, CreekMacros, CrEvent, CR_EV_BUBBLE, CR_EV_SPLASH, CR_EV_DROP, CR_EV_STONE };
}
