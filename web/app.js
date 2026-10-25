// Creek — browser host: Touch 2 emulator, display, audio plumbing.
// The synthesis lives in engine/ (portable). This file mirrors the firmware's
// hardware glue (daisy/ui/creek_ui.cpp): soft pickup, switches, LED.
'use strict';

const $ = (id) => document.getElementById(id);
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

const ctl = new CreekControls();
const macros = new CreekMacros();
let ctx = null, node = null, running = false, lastSent = '';

// ------------------------------------------------------------------ layout
// Positions in % of the board (from the Touch 2 photo, 600 x 972).
const PADS = [
  { id: 0, x: 24, y: 63.8, small: 1 }, { id: 1, x: 51.7, y: 62.6, small: 1 }, { id: 2, x: 75, y: 63.8, small: 1 },
  { id: 3, x: 10.5, y: 82.3 }, { id: 4, x: 29, y: 78.4 }, { id: 5, x: 48, y: 82 },
  { id: 6, x: 68, y: 80.8 }, { id: 7, x: 89.5, y: 85.6 }, { id: 8, x: 31.7, y: 93.6 }, { id: 9, x: 68, y: 94.2 },
  { id: 10, x: 38.7, y: 5, small: 1 }, { id: 11, x: 62.2, y: 5, small: 1 },
];
// physical control -> macro index (CC.M_*), or -1 = reserved
const KNOBS = [
  { id: 'S30', x: 21.2, y: 49.5, m: -1 }, { id: 'S31', x: 21.2, y: 35.5, m: CC.M_SCALE }, { id: 'S32', x: 40.8, y: 35.5, m: CC.M_AER },
  { id: 'S33', x: 60.3, y: 35.5, m: CC.M_GAIN }, { id: 'S34', x: 79.2, y: 35.5, m: CC.M_WIDTH }, { id: 'S35', x: 79.2, y: 49.5, m: -1 },
];
const FADERS = [{ id: 'S36', x: 6.5, y: 42, m: CC.M_FLOW }, { id: 'S37', x: 93.5, y: 42, m: CC.M_ROUGH }];
const SWITCHES = [ // pos 0 = up, 1 = centre, 2 = down
  { k: 'mode', id: 'S09/10', x: 37.8, y: 66, names: ['PERF', 'PERF', 'DIAG'] },
  { k: 'hold', id: 'S07/08', x: 53.5, y: 72.5, names: ['LIVE', 'LIVE', 'HOLD'] },
];
const MACRO_NAMES = ['Flow', 'Roughness', 'Scale', 'Aeration', 'Gain', 'Width'];

// ------------------------------------------------------------------ hardware glue (as creek_ui.cpp)
// Soft pickup: after a preset recall a macro knob only writes again once it
// moves more than 0.03 from where it was.
class Takeover {
  constructor() { this.value = 0; this.init = 0; this.tracking = true; }
  process(v) { if (!this.tracking && Math.abs(v - this.init) < 0.03) return this.value; this.tracking = true; this.value = v; return v; }
  hold(value, knobNow) { this.value = value; this.init = knobNow; this.tracking = false; }
}
const phys = Float32Array.from(ctl.value);            // where each knob / fader physically sits
const take = MACRO_NAMES.map((_, i) => { const t = new Takeover(); t.value = ctl.value[i]; return t; });
const sw = { mode: 0, hold: 0 };

function moveKnob(m, v) {
  phys[m] = clamp(v, 0, 1);
  if (ctl.gliding() && m < 4 && !take[m].tracking) return;  // preset glide owns the macro
  const out = take[m].process(phys[m]);
  if (take[m].tracking) ctl.setKnob(m, out);
}

// LED (same priorities as the firmware)
const led = { left: 0, on: 0, off: 0, lit: false, end: 0, flickerUntil: 0 };
function blink(count, onMs, offMs) { led.left = count; led.on = onMs; led.off = offMs; led.lit = count > 0; led.end = performance.now() + onMs; }
function handleEvents() {
  for (const e of ctl.events) {
    if (e.type === CC.EV_PRESET) { for (let i = 0; i < 4; i++) take[i].hold(ctl.value[i], phys[i]); blink(e.arg + 1, 70, 90); }
    else if (e.type === CC.EV_LAYER) blink(e.arg ? 2 : 1, 60, 80);
    else if (e.type === CC.EV_ALL) blink(3, 40, 40);
    else if (e.type === CC.EV_MODE) blink(e.arg ? 2 : 1, 150, 100);
  }
  ctl.events.length = 0;
}
function ledState(t) {
  if (led.left) {
    if (t >= led.end) {
      if (led.lit) { led.lit = false; led.end = t + led.off; led.left--; } else { led.lit = true; led.end = t + led.on; }
    }
    return led.lit;
  }
  return ctl.diag ? t >= led.flickerUntil : t < led.flickerUntil;
}

// UI tick every 4 ms, like the firmware main loop
const t0 = performance.now();
setInterval(() => {
  ctl.tick((performance.now() - t0) / 1000);
  ctl.setMode(sw.mode === 2);
  ctl.hold = sw.hold === 2;
  handleEvents();
  ctl.compute(macros);
  const key = JSON.stringify(macros);
  if (node && key !== lastSent) { node.port.postMessage({ type: 'macros', m: Object.assign({}, macros) }); lastSent = key; }
}, 4);

// ------------------------------------------------------------------ board
const board = $('board');
const padEls = [], knobEls = [], faderEls = [], swEls = {};
function el(tag, cls, parent, html) {
  const e = document.createElement(tag); if (cls) e.className = cls;
  if (html != null) e.innerHTML = html; if (parent) parent.appendChild(e); return e;
}
function arc(a0, a1, r) {
  const p = (a) => [Math.sin(a * Math.PI / 180) * r * 0.9, -Math.cos(a * Math.PI / 180) * r * 0.9];
  const [x0, y0] = p(a0), [x1, y1] = p(a1);
  return `M${x0.toFixed(2)},${y0.toFixed(2)} A${r * 0.9},${r * 0.9} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1.toFixed(2)},${y1.toFixed(2)}`;
}
function buildBoard() {
  for (const p of PADS) {
    const e = el('div', 'pad' + (p.small ? ' small' : ''), board);
    e.style.left = p.x + '%'; e.style.top = p.y + '%';
    e._lbl = el('div', 'lbl', e);
    e.addEventListener('pointerdown', (ev) => { ev.preventDefault(); e.classList.add('down'); ctl.padDown(p.id); ensureAudio(); });
    const up = () => e.classList.remove('down');
    e.addEventListener('pointerup', up); e.addEventListener('pointercancel', up); e.addEventListener('pointerleave', up);
    padEls[p.id] = e;
  }
  for (const k of KNOBS) {
    const e = el('div', 'knob' + (k.m < 0 ? ' idle' : ''), board);
    e.style.left = k.x + '%'; e.style.top = k.y + '%';
    e.innerHTML = `<svg viewBox="-50 -50 100 100"><circle r="40" fill="#161b1c" stroke="#333" stroke-width="2"/>
      <path class="trk" fill="none" stroke="#263033" stroke-width="6" stroke-linecap="round"/>
      <path class="val" fill="none" stroke="var(--gold)" stroke-width="6" stroke-linecap="round"/>
      <line class="ptr" x1="0" y1="0" x2="0" y2="-30" stroke="#eee" stroke-width="5" stroke-linecap="round"/></svg>
      <span class="name"><b>${k.m < 0 ? '—' : MACRO_NAMES[k.m]}</b><span>${k.id}</span></span>`;
    e.querySelector('.trk').setAttribute('d', arc(-135, 135, 46));
    e._val = e.querySelector('.val'); e._ptr = e.querySelector('.ptr'); e._s = e.querySelector('.name span'); e._m = k.m; e._id = k.id;
    if (k.m >= 0) bindDrag(e, k.m);
    knobEls.push(e);
  }
  for (const f of FADERS) {
    const e = el('div', 'fader', board);
    e.style.left = f.x + '%'; e.style.top = f.y + '%';
    e.innerHTML = `<div class="track"></div><div class="fill"></div><div class="cap"></div><span class="name"><b>${MACRO_NAMES[f.m]}</b><span>${f.id}</span></span>`;
    e._fill = e.querySelector('.fill'); e._cap = e.querySelector('.cap'); e._s = e.querySelector('.name span'); e._m = f.m; e._id = f.id;
    bindFader(e, f.m);
    faderEls.push(e);
  }
  for (const s of SWITCHES) {
    const e = el('div', 'sw', board);
    e.style.left = s.x + '%'; e.style.top = s.y + '%';
    e.innerHTML = '<div class="lever"></div>'; e.title = s.id;
    e._lever = e.querySelector('.lever');
    e._label = el('div', 'swlabel', board);
    const right = s.k === 'hold';
    Object.assign(e._label.style, { left: (right ? s.x + 5.5 : s.x) + '%', top: (right ? s.y + 2.5 : s.y + 6.4) + '%', transform: right ? 'none' : 'translateX(-50%)' });
    e.addEventListener('pointerdown', (ev) => {
      const r = e.getBoundingClientRect();
      sw[s.k] = clamp(Math.floor((ev.clientY - r.top) / r.height * 3), 0, 2); ensureAudio();
    });
    e._s = s; swEls[s.k] = e;
  }
}
function bindDrag(e, m) {
  let startY = 0, startV = 0, id = null;
  e.addEventListener('pointerdown', (ev) => { id = ev.pointerId; e.setPointerCapture(id); startY = ev.clientY; startV = phys[m]; ev.preventDefault(); });
  e.addEventListener('pointermove', (ev) => { if (ev.pointerId === id) moveKnob(m, startV + (startY - ev.clientY) * 0.005 * (ev.altKey ? 0.2 : 1)); });
  const end = (ev) => { if (ev.pointerId === id) id = null; };
  e.addEventListener('pointerup', end); e.addEventListener('pointercancel', end);
}
function bindFader(e, m) {
  let id = null;
  const set = (ev) => { const r = e.getBoundingClientRect(); moveKnob(m, 1 - (ev.clientY - r.top) / r.height); };
  e.addEventListener('pointerdown', (ev) => { id = ev.pointerId; e.setPointerCapture(id); set(ev); ev.preventDefault(); });
  e.addEventListener('pointermove', (ev) => { if (ev.pointerId === id) set(ev); });
  const end = (ev) => { if (ev.pointerId === id) id = null; };
  e.addEventListener('pointerup', end); e.addEventListener('pointercancel', end);
}

function fmt(m, v) {
  if (m === CC.M_GAIN) { const g = ccGain(v); return g === 0 ? 'off' : (20 * Math.log10(g)).toFixed(1) + ' dB'; }
  if (m === CC.M_WIDTH) return ccWidth(v).toFixed(2) + '×';
  if (m === CC.M_SCALE) return (CK.SCALE_MUL_MIN * Math.pow(CK.SCALE_MUL_MAX / CK.SCALE_MUL_MIN, v)).toFixed(2) + '× size';
  return Math.round(v * 100) + '%';
}

function drawBoard() {
  for (const e of knobEls) {
    if (e._m < 0) { e._val.setAttribute('d', ''); e._ptr.setAttribute('transform', 'rotate(-135)'); continue; }
    const v = ctl.value[e._m], p = phys[e._m];
    e._val.setAttribute('d', v > 0.002 ? arc(-135, -135 + 270 * v, 46) : '');
    e._ptr.setAttribute('transform', `rotate(${-135 + 270 * p})`);
    const waiting = !take[e._m].tracking;
    e.classList.toggle('waiting', waiting);
    e._s.textContent = waiting ? 'pick up ↕' : fmt(e._m, v);
  }
  for (const e of faderEls) {
    const v = ctl.value[e._m], p = phys[e._m];
    e._fill.style.height = (v * 100) + '%'; e._cap.style.bottom = (p * 100) + '%';
    const waiting = !take[e._m].tracking;
    e.classList.toggle('waiting', waiting);
    e._s.textContent = waiting ? 'pick up ↕' : fmt(e._m, v);
  }
  for (const k in swEls) {
    const e = swEls[k], pos = sw[k];
    e._lever.style.top = ['6%', '35%', '64%'][pos];
    e._label.textContent = e._s.names[pos];
  }
  for (const p of PADS) {
    const e = padEls[p.id];
    let html = `P${String(p.id).padStart(2, '0')}`, cls = 'idle';
    if (!ctl.diag) {
      const k = p.id - CC.PAD_PRESET0;
      if (k >= 0 && k < CREEK_PRESETS.length) { html += `<small>${CREEK_PRESETS[k].name}</small>`; cls = k === ctl.preset ? 'sel' : ''; }
    } else if (p.id === CC.PAD_ALL) { html += '<small>All on</small>'; cls = ''; }
    else {
      const l = p.id - CC.PAD_LAYER0;
      if (l >= 0 && l < CK_LAYERS) { html += `<small>${CC_LAYER_NAMES[l]}</small>`; cls = (ctl.diagMask >> l) & 1 ? 'layer-on' : 'muted'; }
    }
    if (e._html !== html) { e._lbl.innerHTML = html; e._html = html; }
    e.classList.remove('idle', 'sel', 'muted', 'layer-on'); if (cls) e.classList.add(cls);
  }
  $('led').classList.toggle('on', ledState(performance.now()));
  $('dMode').textContent = ctl.diag ? 'DIAGNOSTICS' : 'PERFORMANCE'; $('dMode').classList.toggle('diag', ctl.diag);
  $('dHold').textContent = ctl.hold ? 'HOLD' : 'LIVE'; $('dHold').classList.toggle('on', ctl.hold);
  $('dPreset').textContent = ctl.diag ? 'layers ' + CC_LAYER_NAMES.map((n, l) => ((ctl.diagMask >> l) & 1 ? n[0] : '·')).join('') : CREEK_PRESETS[ctl.preset].name;
  $('hint').textContent = !running ? 'Press Start audio, then play the panel.'
    : ctl.diag ? 'Diagnostics: P03–P07 mute / unmute layers, P08 all on.' : 'P03–P06 recall presets. Knobs pick up once moved.';
}

// ------------------------------------------------------------------ audio
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
  running = true; $('power').textContent = 'Stop audio';
}
async function stop() { if (ctx) await ctx.suspend(); running = false; $('power').textContent = 'Start audio'; $('stats').textContent = 'audio stopped'; }
function ensureAudio() { if (!running) start(); }
const seed = () => (Number($('seed').value) >>> 0);

// ------------------------------------------------------------------ display
const cv = $('bubbles'), g2 = cv.getContext('2d');
let dots = [];
function onStats(d) {
  const now = performance.now();
  for (let k = 0; k < d.f.length; k++) {
    dots.push({ t: now - (d.f.length - k) * 2, f: d.f[k], a: d.a[k], p: d.pan[k] });
    if (d.a[k] > 0.06) led.flickerUntil = now + 20;
  }
  const s = d.s;
  $('dRate').textContent = `${s.rate.toFixed(0)} bubbles/s`;
  $('stats').textContent = `voices ${s.active} (max ${s.maxActive}) · steals ${s.steals} · drops ${s.drops} · mean pitch ${s.meanF.toFixed(0)} Hz · ` +
    `peak ${d.peak > 0 ? (20 * Math.log10(d.peak)).toFixed(1) : '−∞'} dBFS · limiter ${s.limited} samples`;
  const names = ['Flow bed · centre', 'Rock wake · left', 'Drop · right'];
  $('zones').innerHTML = names.map((n, z) => `<div class="zone"><h4>${n}</h4>` +
    `<div class="bar" title="E energy"><i style="width:${(s.E[z] * 100).toFixed(1)}%"></i></div>` +
    `<div class="bar t" title="T turbulence"><i style="width:${(s.T[z] * 100).toFixed(1)}%"></i></div>` +
    `<div class="bar b" title="B entrained air"><i style="width:${(s.B[z] * 100).toFixed(1)}%"></i></div></div>`).join('');
}
function drawScope() {
  const W = (cv.width = cv.clientWidth * devicePixelRatio), H = (cv.height = 190 * devicePixelRatio), now = performance.now(), span = 6000;
  g2.fillStyle = '#070a0b'; g2.fillRect(0, 0, W, H);
  g2.strokeStyle = '#162022'; g2.fillStyle = '#4b6260'; g2.font = `${10 * devicePixelRatio}px system-ui`;
  for (const f of [250, 500, 1000, 2000, 4000, 8000]) {
    const y = H - (Math.log(f / 200) / Math.log(50)) * H;
    g2.beginPath(); g2.moveTo(0, y); g2.lineTo(W, y); g2.stroke(); g2.fillText(f >= 1000 ? f / 1000 + 'k' : f, 4, y - 2);
  }
  dots = dots.filter((d) => now - d.t < span);
  for (const d of dots) {
    const x = W - ((now - d.t) / span) * W, y = H - (Math.log(Math.max(d.f, 200) / 200) / Math.log(50)) * H;
    const r = (1.5 + Math.min(7, Math.sqrt(d.a) * 14)) * devicePixelRatio, hue = 170 - d.p * 120;
    g2.fillStyle = `hsla(${hue}, 70%, 65%, ${Math.max(0.15, 1 - (now - d.t) / span)})`;
    g2.beginPath(); g2.arc(x, y, r, 0, 2 * Math.PI); g2.fill();
  }
}
function frame() { drawBoard(); drawScope(); requestAnimationFrame(frame); }

// ------------------------------------------------------------------ offline render
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

// ------------------------------------------------------------------ wiring
$('power').onclick = () => (running ? stop() : start());
$('seed').onchange = () => { if (node) node.port.postMessage({ type: 'reset', seed: seed() }); };
$('reseed').onclick = () => { $('seed').value = (Math.random() * 4294967295) >>> 0; $('seed').onchange(); };
document.addEventListener('keydown', (e) => {
  if (e.target.tagName === 'INPUT') return;
  const n = Number(e.key);
  if (e.key === ' ') { e.preventDefault(); running ? stop() : start(); }
  else if (e.key === 'd' || e.key === 'D') sw.mode = sw.mode === 2 ? 0 : 2;
  else if (e.key === 'h' || e.key === 'H') sw.hold = sw.hold === 2 ? 0 : 2;
  else if (!ctl.diag && n >= 1 && n <= 4) ctl.padDown(CC.PAD_PRESET0 + n - 1);
  else if (ctl.diag && n >= 1 && n <= 5) ctl.padDown(CC.PAD_LAYER0 + n - 1);
  else if (ctl.diag && n === 6) ctl.padDown(CC.PAD_ALL);
});
buildBoard(); frame();
