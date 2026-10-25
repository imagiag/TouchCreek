// Creek audition page: CreekControls on the main thread (like the firmware's
// main loop) posts macro snapshots to the engine running in an AudioWorklet.
'use strict';

const ctl = new CreekControls();
const macros = new CreekMacros();
let ctx = null, node = null, running = false;
let lastSent = '';
const $ = (id) => document.getElementById(id);
const knobs = [0, 1, 2, 3, 4, 5].map((k) => $('k' + k));

// ---------------------------------------------------------------- audio
async function start() {
  if (!ctx) {
    ctx = new AudioContext({ sampleRate: 48000, latencyHint: 'interactive' });
    const src = await Promise.all(['engine/creek-config.js', 'engine/creek-engine.js', 'engine/creek-worklet.js']
      .map((f) => fetch(f, { cache: 'no-store' }).then((r) => r.text())));
    const url = URL.createObjectURL(new Blob([src.join('\n')], { type: 'application/javascript' }));
    await ctx.audioWorklet.addModule(url);
    node = new AudioWorkletNode(ctx, 'creek', { outputChannelCount: [2], processorOptions: { seed: seed() } });
    node.port.onmessage = (e) => { if (e.data.type === 'stats') onStats(e.data); };
    node.connect(ctx.destination);
    lastSent = '';
  }
  await ctx.resume();
  running = true; $('start').textContent = 'Stop';
}
async function stop() { if (ctx) await ctx.suspend(); running = false; $('start').textContent = 'Start'; $('stats').textContent = 'stopped'; }
const seed = () => (Number($('seed').value) >>> 0);

// ---------------------------------------------------------------- controls
function knobFromUi(k) { ctl.setKnob(k, Number(knobs[k].value)); }
knobs.forEach((el, k) => el.addEventListener('input', () => knobFromUi(k)));

function fmtKnob(k, v) {
  if (k === CC.M_GAIN) { const g = ccGain(v); return g === 0 ? 'off' : (20 * Math.log10(g)).toFixed(1) + ' dB'; }
  if (k === CC.M_WIDTH) return ccWidth(v).toFixed(2) + '×';
  if (k === CC.M_SCALE) return (CK.SCALE_MUL_MIN * Math.pow(CK.SCALE_MUL_MAX / CK.SCALE_MUL_MIN, v)).toFixed(2) + '× radius';
  return v.toFixed(2);
}

const padEls = [];
function buildPads() {
  const box = $('pads');
  for (let p = 3; p <= 8; p++) {
    const el = document.createElement('div'); el.className = 'pad';
    el.addEventListener('pointerdown', () => { ctl.padDown(p); refreshPads(); });
    box.appendChild(el); padEls.push(el);
  }
}
function refreshPads() {
  for (let i = 0; i < padEls.length; i++) {
    const p = i + 3, el = padEls[i];
    el.classList.remove('on', 'muted', 'blank');
    if (!ctl.diag) {
      const k = p - CC.PAD_PRESET0;
      if (k < CREEK_PRESETS.length) { el.innerHTML = `${CREEK_PRESETS[k].name}<small>P0${p}</small>`; if (k === ctl.preset) el.classList.add('on'); }
      else { el.innerHTML = `<small>P0${p}</small>`; el.classList.add('blank'); }
    } else if (p === CC.PAD_ALL) {
      el.innerHTML = `All on<small>P0${p}</small>`;
    } else {
      const l = p - CC.PAD_LAYER0, on = (ctl.diagMask >> l) & 1;
      el.innerHTML = `${CC_LAYER_NAMES[l]}${l > 1 ? ' (P2)' : ''}<small>P0${p}</small>`;
      el.classList.add(on ? 'on' : 'muted');
    }
  }
  $('swMode').textContent = ctl.diag ? 'Diagnostics' : 'Performance'; $('swMode').classList.toggle('on', ctl.diag);
  $('swHold').textContent = ctl.hold ? 'Hold' : 'Live'; $('swHold').classList.toggle('on', ctl.hold);
  $('hint').textContent = ctl.diag ? 'Diagnostics: pads solo/mute layers (eddies, falls and surface arrive in Phase 2).'
    : 'Pads recall presets (250 ms glide). Knobs pick up from where they are.';
}
$('swMode').onclick = () => { ctl.setMode(!ctl.diag); refreshPads(); };
$('swHold').onclick = () => { ctl.hold = !ctl.hold; refreshPads(); };

// main-loop tick (~4 ms, like the firmware UI tick)
const t0 = performance.now();
setInterval(() => {
  ctl.tick((performance.now() - t0) / 1000);
  ctl.events.length = 0;
  for (let k = 0; k < CC.M_COUNT; k++) {
    const v = ctl.value[k];
    if (document.activeElement !== knobs[k] && Math.abs(Number(knobs[k].value) - v) > 1e-4) knobs[k].value = v;
    $('v' + k).textContent = fmtKnob(k, v);
  }
  ctl.compute(macros);
  const key = JSON.stringify(macros);
  if (node && key !== lastSent) { node.port.postMessage({ type: 'macros', m: Object.assign({}, macros) }); lastSent = key; }
}, 4);

// ---------------------------------------------------------------- display
const cv = $('bubbles'), g2 = cv.getContext('2d');
let dots = [];
function onStats(d) {
  const now = performance.now();
  for (let k = 0; k < d.f.length; k++) dots.push({ t: now - (d.f.length - k) * 2, f: d.f[k], a: d.a[k], p: d.pan[k] });
  const s = d.s;
  $('stats').textContent = `bubbles ${s.rate.toFixed(0)}/s · voices ${s.active} (max ${s.maxActive}) · steals ${s.steals} · drops ${s.drops} · ` +
    `mean pitch ${s.meanF.toFixed(0)} Hz · peak ${d.peak > 0 ? (20 * Math.log10(d.peak)).toFixed(1) : '−∞'} dBFS · limiter ${s.limited} samples`;
  const names = ['Flow bed (centre)', 'Rock wake (left)', 'Drop (right)'];
  $('zones').innerHTML = names.map((n, z) => `<div class="zone"><h4>${n}</h4>` +
    `<div class="bar" title="E energy"><i style="width:${(s.E[z] * 100).toFixed(1)}%"></i></div>` +
    `<div class="bar t" title="T turbulence"><i style="width:${(s.T[z] * 100).toFixed(1)}%"></i></div>` +
    `<div class="bar b" title="B entrained air"><i style="width:${(s.B[z] * 100).toFixed(1)}%"></i></div></div>`).join('');
}
function draw() {
  const W = cv.width, H = cv.height, now = performance.now(), span = 6000;
  g2.fillStyle = '#0a1012'; g2.fillRect(0, 0, W, H);
  g2.strokeStyle = '#1b2a2e'; g2.fillStyle = '#4b6260'; g2.font = '10px system-ui';
  for (const f of [250, 500, 1000, 2000, 4000, 8000]) {
    const y = H - (Math.log(f / 200) / Math.log(50)) * H;
    g2.beginPath(); g2.moveTo(0, y); g2.lineTo(W, y); g2.stroke(); g2.fillText(f >= 1000 ? f / 1000 + 'k' : f, 4, y - 2);
  }
  dots = dots.filter((d) => now - d.t < span);
  for (const d of dots) {
    const x = W - ((now - d.t) / span) * W, y = H - (Math.log(Math.max(d.f, 200) / 200) / Math.log(50)) * H;
    const r = 1.5 + Math.min(7, Math.sqrt(d.a) * 14), hue = 170 - d.p * 120;
    g2.fillStyle = `hsla(${hue}, 70%, 65%, ${Math.max(0.15, 1 - (now - d.t) / span)})`;
    g2.beginPath(); g2.arc(x, y, r, 0, 2 * Math.PI); g2.fill();
  }
  requestAnimationFrame(draw);
}

// ---------------------------------------------------------------- offline render
function wavBlob(L, R, sr) {
  const n = L.length, b = new DataView(new ArrayBuffer(44 + n * 4));
  const w = (o, s) => { for (let i = 0; i < s.length; i++) b.setUint8(o + i, s.charCodeAt(i)); };
  w(0, 'RIFF'); b.setUint32(4, 36 + n * 4, true); w(8, 'WAVEfmt '); b.setUint32(16, 16, true); b.setUint16(20, 1, true);
  b.setUint16(22, 2, true); b.setUint32(24, sr, true); b.setUint32(28, sr * 4, true); b.setUint16(32, 4, true); b.setUint16(34, 16, true);
  w(36, 'data'); b.setUint32(40, n * 4, true);
  const q = (v) => Math.max(-32767, Math.min(32767, Math.round(v * 32767)));
  for (let i = 0; i < n; i++) { b.setInt16(44 + i * 4, q(L[i]), true); b.setInt16(46 + i * 4, q(R[i]), true); }
  return new Blob([b], { type: 'audio/wav' });
}
$('render').onclick = () => {
  const sr = 48000, secs = 30, eng = new CreekEngine(sr, seed());
  const m = ctl.compute(new CreekMacros()); m.hold = false; eng.setMacros(m);
  const n = secs * sr, L = new Float32Array(n), R = new Float32Array(n), bl = new Float32Array(48), br = new Float32Array(48);
  for (let i = 0; i < n; i += 48) { eng.process(bl, br, 48); L.set(bl, i); R.set(br, i); }
  const a = document.createElement('a');
  a.href = URL.createObjectURL(wavBlob(L, R, sr));
  a.download = `creek-seed${seed()}-F${m.flow.toFixed(2)}-R${m.roughness.toFixed(2)}-S${m.scale.toFixed(2)}-A${m.aeration.toFixed(2)}.wav`;
  a.click();
};

// ---------------------------------------------------------------- wiring
$('start').onclick = () => (running ? stop() : start());
$('seed').onchange = () => { if (node) node.port.postMessage({ type: 'reset', seed: seed() }); };
$('reseed').onclick = () => { $('seed').value = (Math.random() * 4294967295) >>> 0; $('seed').onchange(); };
document.addEventListener('keydown', (e) => {
  if (e.target.tagName === 'INPUT' && e.target.type === 'number') return;
  const n = Number(e.key);
  if (e.key === ' ') { e.preventDefault(); running ? stop() : start(); }
  else if (e.key === 'd' || e.key === 'D') { ctl.setMode(!ctl.diag); }
  else if (e.key === 'h' || e.key === 'H') { ctl.hold = !ctl.hold; }
  else if (!ctl.diag && n >= 1 && n <= 4) ctl.padDown(CC.PAD_PRESET0 + n - 1);
  else if (ctl.diag && n >= 1 && n <= 5) ctl.padDown(CC.PAD_LAYER0 + n - 1);
  else if (ctl.diag && n === 6) ctl.padDown(CC.PAD_ALL);
  refreshPads();
});
buildPads(); refreshPads(); draw();
