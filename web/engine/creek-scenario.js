// Scenario files (scenarios/*.txt): seed, length and control automation for a
// render. The same files drive the C++ renderer (daisy/test/scenario.h) and
// the JS engine, so renders can be compared and every render records exactly
// what was automated.
//
//   seconds 30                     length
//   seed 1                         engine seed
//   at 0 preset trickle            preset macros (trickle rocky cascade deeper)
//   at 4 set flow 0 hold 1         set macros: flow roughness scale aeration gain width mask hold
//   ramp 0 20 flow 0 1             linear from t0 to t1 (updated every 48 samples)
//   at 1.5 bubble zone 0 radius 2 amp 1 rise 0 pan 0
//   every 0 5 0.25 bubble radius 1 repeat a bubble from t0 (incl.) to t1 (excl.)
//   at 2 drop hz 880 amp 1         note-mode tuned drop (optional pan)
//   at 3 stone amp 1               throw a stone (P01)
//   at 2 tune 523.25 659.26        held notes tuning the creek (up to 4 Hz values; 'at 3 tune' = off)
// Times in seconds; '#' starts a comment.

const CS_KEYS = ['flow', 'roughness', 'scale', 'aeration', 'gain', 'width', 'mask', 'hold', 'freeflow', 'variety', 'decay', 'scalemask', 'tone', 'tunedetail'];
const CS_BUBBLE_KEYS = ['zone', 'radius', 'amp', 'tau', 'rise', 'pan'];
const CS_RAMP_STEP = 48;

function csParse(text, sr) {
  const sc = { seconds: 10, seed: 1, points: [], ramps: [] };
  const toS = (t) => Math.round(Number(t) * sr);
  const kv = (arr, keys) => {
    const o = {};
    for (let i = 0; i + 1 < arr.length; i += 2) {
      if (!keys.includes(arr[i])) throw new Error('unknown key ' + arr[i]);
      o[arr[i]] = Number(arr[i + 1]);
    }
    return o;
  };
  let order = 0;
  for (const raw of text.split('\n')) {
    const w = raw.replace(/#.*/, '').trim().split(/\s+/).filter(Boolean);
    if (!w.length) continue;
    if (w[0] === 'seconds') sc.seconds = Number(w[1]);
    else if (w[0] === 'seed') sc.seed = Number(w[1]) >>> 0;
    else if (w[0] === 'at') {
      const s = toS(w[1]), op = w[2];
      if (op === 'preset') sc.points.push({ s, order: order++, op, name: w[3] });
      else if (op === 'set') sc.points.push({ s, order: order++, op, v: kv(w.slice(3), CS_KEYS) });
      else if (op === 'bubble') sc.points.push({ s, order: order++, op, v: kv(w.slice(3), CS_BUBBLE_KEYS) });
      else if (op === 'stone') sc.points.push({ s, order: order++, op, v: kv(w.slice(3), ['amp']) });
      else if (op === 'drop') sc.points.push({ s, order: order++, op, v: kv(w.slice(3), ['hz', 'amp', 'pan']) });
      else if (op === 'tune') sc.points.push({ s, order: order++, op, hz: w.slice(3, 7).map(Number) });
      else throw new Error('unknown op ' + op);
    } else if (w[0] === 'every') {
      const t0 = Number(w[1]), t1 = Number(w[2]), dt = Number(w[3]);
      if (w[4] !== 'bubble') throw new Error('every: only bubble');
      const v = kv(w.slice(5), CS_BUBBLE_KEYS);
      for (let k = 0; t0 + k * dt < t1 - 1e-9; k++) sc.points.push({ s: toS(t0 + k * dt), order: order++, op: 'bubble', v });
    } else if (w[0] === 'ramp') {
      if (!CS_KEYS.includes(w[3])) throw new Error('unknown key ' + w[3]);
      sc.ramps.push({ s0: toS(w[1]), s1: toS(w[2]), key: w[3], v0: Number(w[4]), v1: Number(w[5]) });
    } else throw new Error('unknown line: ' + raw);
  }
  sc.points.sort((a, b) => a.s - b.s || a.order - b.order);
  return sc;
}

// Runs a parsed scenario on a CreekEngine-like object. `presets` = CREEK_PRESETS.
// Returns interleaved-free {L, R}. Splits processing at automation points, so the
// result is independent of `block`.
function csRun(sc, eng, sr, block, presets, onBlock) {
  const n = Math.floor(sc.seconds * sr), L = new Float32Array(n), R = new Float32Array(n);
  const m = { flow: 0.2, roughness: 0.25, scale: 0.2, aeration: 0.3, gain: 1, width: 1, mask: 31, hold: 0, tuneHz: [0, 0, 0, 0],
    freeflow: 1, variety: 0.5, decay: 0.5, scalemask: 4095, tone: 0.5, tunedetail: 0 };
  eng.reset(sc.seed);
  let pi = 0, pos = 0;
  const bl = new Float32Array(block), br = new Float32Array(bl.length);
  while (pos < n) {
    let changed = pos === 0;
    for (; pi < sc.points.length && sc.points[pi].s <= pos; pi++) {
      const p = sc.points[pi];
      if (p.op === 'preset') { const q = presets.find((x) => x.key === p.name); m.flow = q.flow; m.roughness = q.roughness; m.scale = q.scale; m.aeration = q.aeration; changed = true; }
      else if (p.op === 'set') { Object.assign(m, p.v); changed = true; }
      else if (p.op === 'tune') { for (let k = 0; k < 4; k++) m.tuneHz[k] = p.hz[k] || 0; changed = true; }
      else if (p.op === 'stone') eng.trigger({ type: 3, amp: p.v.amp || 0.8 });
      else if (p.op === 'drop') eng.trigger({ type: 2, radiusMm: 3000 / p.v.hz, amp: p.v.amp || 0.7, pan: p.v.pan === undefined ? -2 : p.v.pan });
      else eng.trigger({ zone: p.v.zone === undefined ? 1 : p.v.zone, radiusMm: p.v.radius || 0, amp: p.v.amp || 0, tauSec: p.v.tau || 0,
        rise: p.v.rise === undefined ? -1 : p.v.rise, pan: p.v.pan === undefined ? -2 : p.v.pan });
    }
    if (pos % CS_RAMP_STEP === 0) for (const r of sc.ramps) if (pos >= r.s0 && pos <= r.s1 + CS_RAMP_STEP) {
      const x = Math.min(1, (pos - r.s0) / Math.max(1, r.s1 - r.s0)); m[r.key] = r.v0 + (r.v1 - r.v0) * x; changed = true;
    }
    if (changed) eng.setMacros({ flow: m.flow, roughness: m.roughness, scale: m.scale, aeration: m.aeration, gain: m.gain, width: m.width, mask: m.mask | 0, hold: !!m.hold,
      tuneHz: m.tuneHz.slice(), tune: m.tuneHz.some((h) => h > 0) ? 1 : 0,
      freeFlow: m.freeflow, variety: m.variety, decay: m.decay, scaleMask: m.scalemask | 0, tone: m.tone, tuneDetail: m.tunedetail ? 1 : 0 });
    const rampOn = sc.ramps.some((r) => pos <= r.s1 + CS_RAMP_STEP);
    let next = rampOn ? Math.min(n, (Math.floor(pos / CS_RAMP_STEP) + 1) * CS_RAMP_STEP) : n;
    if (pi < sc.points.length && sc.points[pi].s < next) next = sc.points[pi].s;
    const k = Math.min(block, next - pos);
    eng.process(bl, br, k);
    L.set(bl.subarray(0, k), pos); R.set(br.subarray(0, k), pos);
    if (onBlock) onBlock(pos, k);
    pos += k;
  }
  return { L, R };
}

if (typeof module !== 'undefined') module.exports = { csParse, csRun };
