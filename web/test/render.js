// Offline renders with the JS engine:  node web/test/render.js [outdir]
// Writes WAVs (+ a .json with seed, settings and stats) and prints a summary.
// The deterministic reference renders come from the C++ renderer
// (daisy/test/creek_render); this script is for quick checks while tuning.
const fs = require('fs'), path = require('path');
Object.assign(globalThis, require('../engine/creek-config.js'));
const { CreekEngine } = require('../engine/creek-engine.js');
const { CREEK_PRESETS } = require('../engine/creek-controls.js');
const SR = 48000, BLOCK = 48;
const OUT = process.argv[2] || path.join(__dirname, 'out');
fs.mkdirSync(OUT, { recursive: true });

function wav(file, L, R) { // 16-bit PCM stereo
  const n = L.length, b = Buffer.alloc(44 + n * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 4, 4); b.write('WAVEfmt ', 8);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22);
  b.writeUInt32LE(SR, 24); b.writeUInt32LE(SR * 4, 28); b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34);
  b.write('data', 36); b.writeUInt32LE(n * 4, 40);
  const q = (v) => Math.max(-32767, Math.min(32767, Math.round(v * 32767)));
  for (let i = 0; i < n; i++) { b.writeInt16LE(q(L[i]), 44 + i * 4); b.writeInt16LE(q(R[i]), 46 + i * 4); }
  fs.writeFileSync(file, b);
}
const db = (x) => (20 * Math.log10(Math.max(x, 1e-9))).toFixed(1);

function analyse(L, R, from) {
  let peak = 0, sl = 0, sr = 0, sm = 0, ml = 0, mr = 0, nan = 0;
  const n = L.length - from;
  for (let i = from; i < L.length; i++) {
    const a = L[i], b = R[i];
    if (!isFinite(a) || !isFinite(b)) { nan++; continue; }
    peak = Math.max(peak, Math.abs(a), Math.abs(b));
    sl += a * a; sr += b * b; sm += 0.25 * (a + b) * (a + b); ml += a; mr += b;
  }
  const rms = Math.sqrt((sl + sr) / (2 * n)), mono = Math.sqrt(sm / n);
  return { peak, rms, monoRatio: mono / (rms + 1e-12), dcL: ml / n, dcR: mr / n, nan };
}

// script(t, eng) -> macros object (or null to keep) ; may call eng.trigger()
// probes: every `probe` seconds, births/s and mean bubble pitch since the last probe
function render(name, secs, script, seed = 1, probe = 0) {
  const probes = []; let pb = 0, pl = 0, pt = 0;
  const eng = new CreekEngine(SR, seed);
  const n = Math.floor(secs * SR), L = new Float32Array(n), R = new Float32Array(n);
  const bl = new Float32Array(BLOCK), br = new Float32Array(BLOCK);
  let cpu = 0n;
  for (let i = 0; i < n; i += BLOCK) {
    const m = script(i / SR, eng, i); if (m) eng.setMacros(m);
    const t0 = process.hrtime.bigint(); eng.process(bl, br, BLOCK); cpu += process.hrtime.bigint() - t0;
    const k = Math.min(BLOCK, n - i); L.set(bl.subarray(0, k), i); R.set(br.subarray(0, k), i);
    if (probe && (i + BLOCK) / SR - pt >= probe - 1e-9) {
      const s = eng.stats(), nb = s.births - pb;
      probes.push({ rate: nb / probe, meanF: nb ? Math.pow(2, (s.sumLog2F - pl) / nb) : 0 });
      pb = s.births; pl = s.sumLog2F; pt = (i + BLOCK) / SR;
    }
  }
  const a = analyse(L, R, Math.min(n - 1, SR)), st = eng.stats();
  const line = `${name.padEnd(18)} peak ${db(a.peak)} dB  rms ${db(a.rms)} dB  mono/st ${a.monoRatio.toFixed(2)}  ` +
    `dc ${a.dcL.toExponential(1)}  births ${(st.births / secs).toFixed(1)}/s  meanF ${st.meanF.toFixed(0)} Hz  ` +
    `maxV ${st.maxActive} steal ${st.steals} drop ${st.drops}  lim ${st.limited}  cpu ${(Number(cpu) / 1e9 / secs * 100).toFixed(1)}%`;
  console.log(line + (a.nan ? `  NaN ${a.nan}` : ''));
  if (probes.length) console.log(`  per ${probe} s: ` + probes.map((p) => `${p.rate.toFixed(0)}/s ${p.meanF.toFixed(0)} Hz`).join(' | '));
  wav(path.join(OUT, name + '.wav'), L, R);
  fs.writeFileSync(path.join(OUT, name + '.json'), JSON.stringify({ name, engine: 'js', seed, sampleRate: SR, block: BLOCK, seconds: secs, stats: st, analysis: a }, null, 1));
  if (a.nan) process.exitCode = 1;
  return { L, R, st, a };
}

const P = Object.fromEntries(CREEK_PRESETS.map((p) => [p.key, p]));
const preset = (k, extra) => Object.assign({ flow: P[k].flow, roughness: P[k].roughness, scale: P[k].scale, aeration: P[k].aeration }, extra || {});
const once = (m) => (t, e, i) => (i === 0 ? m : null);

render('trickle', 30, once(preset('trickle')));
render('trickle_flow', 12, once(preset('trickle', { mask: 0b00001 })));
render('trickle_bubbles', 12, once(preset('trickle', { mask: 0b00010 })));
render('rocky', 20, once(preset('rocky')));
render('cascade', 20, once(preset('cascade')));
render('deeper', 20, once(preset('deeper')));

// radius -> pitch: aeration 0 (no random bubbles), triggered bubbles 0.5 .. 6 mm
const radii = [0.5, 1, 2, 4, 6];
const rs = render('radius_steps', 6, (t, e, i) => {
  const m = i === 0 ? preset('trickle', { aeration: 0, mask: 0b00010 }) : null;
  if (m) e.setMacros(m);
  const k = Math.floor(t / 1.0);
  if (k < radii.length && i % (SR / 4) === 0) e.trigger({ zone: 0, radiusMm: radii[k], amp: 1, rise: 0, pan: 0 });
  return null;
});
{ // zero-crossing pitch of each triggered bubble (2..40 ms after onset)
  const f = radii.map((r, k) => { let zc = 0; const a = Math.round((k + 0.002) * SR), b = Math.round((k + 0.040) * SR);
    for (let i = a + 1; i < b; i++) if ((rs.L[i - 1] < 0) !== (rs.L[i] < 0)) zc++; return zc / 2 / 0.038; });
  console.log('  radius -> pitch: ' + radii.map((r, k) => `${r} mm ${f[k].toFixed(0)} Hz (3/r = ${(3000 / r).toFixed(0)})`).join(', '));
  if (!f.every((x, k) => k === 0 || x < f[k - 1])) { console.log('  FAIL: pitch must fall as radius grows'); process.exitCode = 1; }
}
// Flow to zero: must reach silence within 3 s
const fz = render('flow_to_zero', 8, (t, e, i) => (i === 0 ? preset('rocky') : (t >= 4 && t < 4 + BLOCK / SR ? { flow: 0 } : null)));
{ let last = 0; for (let i = 4 * SR; i < fz.L.length; i++) if (Math.abs(fz.L[i]) > 1e-4 || Math.abs(fz.R[i]) > 1e-4) last = i;
  console.log(`  flow_to_zero: last sample above -80 dBFS at ${(last / SR - 4).toFixed(2)} s after Flow = 0`); }
// Macro sweeps
render('sweep_flow', 20, (t) => ({ flow: t / 20, roughness: 0.5, scale: 0.5, aeration: 0.5 }), 1, 5);
render('sweep_aeration', 20, (t) => ({ flow: 0.5, roughness: 0.5, scale: 0.5, aeration: t / 20 }), 1, 5);
render('sweep_scale', 20, (t) => ({ flow: 0.5, roughness: 0.5, scale: t / 20, aeration: 0.5 }), 1, 5);
render('sweep_roughness', 20, (t) => ({ flow: 0.5, roughness: t / 20, scale: 0.5, aeration: 0.5 }), 1, 5);
