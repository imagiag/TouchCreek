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
// physical controls (p = S30..S37 -> 0..7) -> macro (CC.M_*); alt = macro while P10 is held
const KNOBS = [
  { id: 'S30', p: 0, x: 21.2, y: 49.5, m: CC.M_FREE, alt: CC.M_WIDTH }, { id: 'S31', p: 1, x: 21.2, y: 35.5, m: CC.M_SCALE, alt11: CC.M_TONE },
  { id: 'S32', p: 2, x: 40.8, y: 35.5, m: CC.M_AER }, { id: 'S33', p: 3, x: 60.3, y: 35.5, m: CC.M_FLOW, alt11: CC.M_ATTACK },
  { id: 'S34', p: 4, x: 79.2, y: 35.5, m: CC.M_DECAY, alt11: CC.M_RELEASE }, { id: 'S35', p: 5, x: 79.2, y: 49.5, m: CC.M_VARIETY },
];
const FADERS = [{ id: 'S36', p: 6, x: 6.5, y: 42, m: CC.M_LEVEL, alt11: CC.M_RES }, { id: 'S37', p: 7, x: 93.5, y: 42, m: CC.M_ROUGH, alt11: CC.M_CUTOFF }];
const SWITCHES = [ // pos 0 = up, 1 = centre, 2 = down
  { k: 'mode', id: 'S09/10', x: 37.8, y: 66, names: ['PRESETS', 'NOTES', 'DIAG'] },
  { k: 'hold', id: 'S07/08', x: 53.5, y: 72.5, names: ['LIVE', 'LIVE', 'HOLD'], notesNames: ['LIVE', 'LIVE', 'LATCH'] },
];
const MACRO_NAMES = ['Flow', 'Roughness', 'Scale', 'Aeration', 'Level', 'Width', 'Free flow', 'Variety', 'Decay', 'Cutoff', 'Resonance', 'Tone', 'Attack', 'Release'];

// ------------------------------------------------------------------ hardware glue (as creek_ui.cpp)
// Soft pickup: after a preset recall a macro knob only writes again once it
// moves more than 0.03 from where it was.
class Takeover {
  constructor() { this.value = 0; this.init = 0; this.tracking = true; }
  process(v) { if (!this.tracking && Math.abs(v - this.init) < 0.03) return this.value; this.tracking = true; this.value = v; return v; }
  hold(value, knobNow) { this.value = value; this.init = knobNow; this.tracking = false; }
}
const CONTROLS = KNOBS.concat(FADERS);
const PHYS_OF = [];                                    // macro -> physical control
for (const c of CONTROLS) { PHYS_OF[c.m] = c.p; if (c.alt !== undefined) PHYS_OF[c.alt] = c.p; if (c.alt11 !== undefined) PHYS_OF[c.alt11] = c.p; }
// layers (as the firmware): P11 held = filter on the faders, P10 held = Width on S34
function macroFor(c) {
  if (c.alt11 !== undefined && ctl.touched[CC.P_SCALE]) return c.alt11;
  if (c.alt !== undefined && ctl.touched[CC.P_ROOT]) return c.alt;
  return c.m;
}
const physPos = new Float32Array(8);                   // where each knob / fader physically sits
for (const c of CONTROLS) physPos[c.p] = ctl.value[c.m];
const take = MACRO_NAMES.map((_, i) => { const t = new Takeover(); t.value = ctl.value[i]; return t; });
for (const c of CONTROLS) for (const a of [c.alt, c.alt11]) if (a !== undefined) take[a].hold(ctl.value[a], physPos[c.p]);   // layers start at defaults and wait
const activeWas = CONTROLS.map((c) => c.m);
const sw = { mode: 0, hold: 0 };                       // switch positions: 0 up, 1 centre, 2 down

function moveKnob(c, v) {
  physPos[c.p] = clamp(v, 0, 1);
  const m = macroFor(c);
  if (ctl.gliding() && m < 4 && !take[m].tracking) return;  // preset glide owns the macro
  const out = take[m].process(physPos[c.p]);
  if (take[m].tracking) ctl.setKnob(m, out);
}

// LED (same priorities as the firmware)
const led = { left: 0, on: 0, off: 0, lit: false, end: 0, flickerUntil: 0 };
function blink(count, onMs, offMs) { led.left = count; led.on = onMs; led.off = offMs; led.lit = count > 0; led.end = performance.now() + onMs; }
function handleEvents() {
  for (const e of ctl.events) {
    if (e.type === CC.EV_PRESET) { for (let i = 0; i < 4; i++) take[i].hold(ctl.value[i], physPos[PHYS_OF[i]]); blink(e.arg + 1, 70, 90); }
    else if (e.type === CC.EV_LAYER) blink(e.arg ? 2 : 1, 60, 80);
    else if (e.type === CC.EV_ALL) blink(3, 40, 40);
    else if (e.type === CC.EV_MODE) blink(e.arg + 1, 150, 100);
    else if (e.type === CC.EV_SCALE) blink(e.arg + 1, 30, 30);
    else if (e.type === CC.EV_OCT) blink(e.arg < 0 ? 1 - e.arg : e.arg + 1, 25, 25);
    else if (e.type === CC.EV_ROOT) blink(1, 120, 0);
    else if (e.type === CC.EV_NOTE) led.flickerUntil = performance.now() + 40;
    else if (e.type === CC.EV_STONE) led.flickerUntil = performance.now() + 250;
    else if (e.type === CC.EV_STRESS) blink(e.arg ? 4 : 1, 40, 40);
    else if (e.type === CC.EV_CC) take[e.arg].hold(ctl.value[e.arg], physPos[PHYS_OF[e.arg]]);   // knob waits until moved
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
  if (ctl.stress) return Math.floor(t / 62.5) % 2 === 0;    // stress test: 8 Hz blink
  return ctl.mode === CC.MODE_DIAG ? t >= led.flickerUntil : t < led.flickerUntil;
}

// UI tick every 4 ms, like the firmware main loop
const t0 = performance.now();
setInterval(() => {
  ctl.tick((performance.now() - t0) / 1000);
  ctl.setMode(sw.mode);                    // 0 presets, 1 notes, 2 diagnostics
  CONTROLS.forEach((c, i) => {             // layer switch: the newly active control waits until the knob moves
    const m = macroFor(c);
    if (m !== activeWas[i]) { activeWas[i] = m; take[m].hold(ctl.value[m], physPos[c.p]); }
  });
  ctl.setRightSwitch(sw.hold === 2);       // Latch in Notes, Hold elsewhere
  for (const d of ctl.drops) if (node) node.port.postMessage({ type: 'trigger', ev: { type: 2, radiusMm: 3000 / d.hz, amp: 0.25 + 0.75 * d.vel } });
  ctl.drops.length = 0;
  for (const v of ctl.stones) if (node) node.port.postMessage({ type: 'trigger', ev: { type: 3, amp: 0.3 + 0.7 * v } });
  ctl.stones.length = 0;
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
    // velocity: pen pressure when available, otherwise where you hit the pad (top = hard), like TouchPulsar
    e.addEventListener('pointerdown', (ev) => {
      ev.preventDefault(); e.setPointerCapture(ev.pointerId); e.classList.add('down'); ensureAudio();
      const r = e.getBoundingClientRect();
      const vel = ev.pointerType === 'pen' && ev.pressure > 0 ? ev.pressure : clamp(1 - (ev.clientY - r.top) / r.height, 0.1, 1);
      ctl.padDown(p.id, vel);
    });
    const up = () => { if (!e.classList.contains('down')) return; e.classList.remove('down'); ctl.padUp(p.id); };
    e.addEventListener('pointerup', up); e.addEventListener('pointercancel', up);
    padEls[p.id] = e;
  }
  for (const k of KNOBS) {
    const e = el('div', 'knob', board);
    e.style.left = k.x + '%'; e.style.top = k.y + '%';
    e.innerHTML = `<svg viewBox="-50 -50 100 100"><circle r="40" fill="#161b1c" stroke="#333" stroke-width="2"/>
      <path class="trk" fill="none" stroke="#263033" stroke-width="6" stroke-linecap="round"/>
      <path class="val" fill="none" stroke="var(--gold)" stroke-width="6" stroke-linecap="round"/>
      <line class="ptr" x1="0" y1="0" x2="0" y2="-30" stroke="#eee" stroke-width="5" stroke-linecap="round"/></svg>
      <span class="name"><b></b><i class="kid"></i><span class="kv"></span></span>`;
    e.querySelector('.trk').setAttribute('d', arc(-135, 135, 46));
    e._val = e.querySelector('.val'); e._ptr = e.querySelector('.ptr');
    e._b = e.querySelector('.name b'); e._id = e.querySelector('.kid'); e._s = e.querySelector('.kv'); e._c = k;
    bindDrag(e, k);
    knobEls.push(e);
  }
  for (const f of FADERS) {
    const e = el('div', 'fader ' + (f.x < 50 ? 'edge-l' : 'edge-r'), board);
    e.style.left = f.x + '%'; e.style.top = f.y + '%';
    e.innerHTML = `<div class="track"></div><div class="fill"></div><div class="cap"></div><span class="name"><b></b><i class="kid"></i><span class="kv"></span></span>`;
    e._fill = e.querySelector('.fill'); e._cap = e.querySelector('.cap');
    e._b = e.querySelector('.name b'); e._id = e.querySelector('.kid'); e._s = e.querySelector('.kv'); e._c = f;
    bindFader(e, f);
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
function bindDrag(e, c) {
  let startY = 0, startV = 0, id = null;
  e.addEventListener('pointerdown', (ev) => { id = ev.pointerId; e.setPointerCapture(id); startY = ev.clientY; startV = physPos[c.p]; ev.preventDefault(); });
  e.addEventListener('pointermove', (ev) => { if (ev.pointerId === id) moveKnob(c, startV + (startY - ev.clientY) * 0.005 * (ev.altKey ? 0.2 : 1)); });
  const end = (ev) => { if (ev.pointerId === id) id = null; };
  e.addEventListener('pointerup', end); e.addEventListener('pointercancel', end);
}
function bindFader(e, c) {
  let id = null;
  const set = (ev) => { const r = e.getBoundingClientRect(); moveKnob(c, 1 - (ev.clientY - r.top) / r.height); };
  e.addEventListener('pointerdown', (ev) => { id = ev.pointerId; e.setPointerCapture(id); set(ev); ev.preventDefault(); });
  e.addEventListener('pointermove', (ev) => { if (ev.pointerId === id) set(ev); });
  const end = (ev) => { if (ev.pointerId === id) id = null; };
  e.addEventListener('pointerup', end); e.addEventListener('pointercancel', end);
}

function fmt(m, v) {
  if (m === CC.M_LEVEL) { const g = ccLevel(v); return g < 1e-4 ? 'off' : (20 * Math.log10(g)).toFixed(1).replace(/^-0\.0$/, '0.0') + ' dB'; }
  if (m === CC.M_WIDTH) return ccWidth(v).toFixed(2) + '×';
  if (m === CC.M_SCALE) return crScaleMul(v).toFixed(2) + '×';
  if (m === CC.M_CUTOFF) { const hz = CK.FILTER_HZ[0] * Math.pow(CK.FILTER_HZ[1] / CK.FILTER_HZ[0], v); return v > 0.995 ? 'open' : hz >= 1000 ? (hz / 1000).toFixed(1) + ' kHz' : Math.round(hz) + ' Hz'; }
  if (m === CC.M_RES) return 'Q ' + (CK.FILTER_Q[0] + v * v * (CK.FILTER_Q[1] - CK.FILTER_Q[0])).toFixed(1);
  if (m === CC.M_TONE) return Math.abs(v - 0.5) < 0.02 ? 'default' : v < 0.5 ? `dark ${Math.round((0.5 - v) * 200)}%` : `bright ${Math.round((v - 0.5) * 200)}%`;
  if (m === CC.M_ATTACK || m === CC.M_RELEASE) { const s = ccSec(v, m === CC.M_ATTACK ? CK.ATTACK_RANGE_SEC : CK.RELEASE_RANGE_SEC); return s < 1 ? Math.round(s * 1000) + ' ms' : s.toFixed(2) + ' s'; }
  if (m === CC.M_DECAY) return '×' + Math.pow(2, CK.DECAY_OCT * (2 * v - 1)).toFixed(2);
  if (m === CC.M_VARIETY) return Math.abs(v - 0.5) < 0.02 ? 'default' : v < 0.5 ? `one note ${Math.round((0.5 - v) * 200)}%` : `scale ${Math.round((v - 0.5) * 200)}%`;
  if (m === CC.M_FREE) return (v < 0.005 ? 'still' : Math.round(v * 100) + '%') + (ctl.mode === CC.MODE_NOTES ? '' : ' (Notes)');
  return Math.round(v * 100) + '%';
}

function drawBoard() {
  for (const e of knobEls.concat(faderEls)) {
    const c = e._c, m = macroFor(c), v = ctl.value[m], p = physPos[c.p];
    if (e._val) { e._val.setAttribute('d', v > 0.002 ? arc(-135, -135 + 270 * v, 46) : ''); e._ptr.setAttribute('transform', `rotate(${-135 + 270 * p})`); }
    else { e._fill.style.height = (v * 100) + '%'; e._cap.style.bottom = (p * 100) + '%'; }
    const waiting = !take[m].tracking;
    e.classList.toggle('waiting', waiting);
    e.classList.toggle('altl', m !== c.m);
    if (e._b.textContent !== MACRO_NAMES[m]) e._b.textContent = MACRO_NAMES[m];
    const layer = c.alt !== undefined ? ['P10', c.alt] : c.alt11 !== undefined ? ['P11', c.alt11] : null;
    const idTxt = !layer ? c.id : m === c.m ? `${c.id} · ${layer[0]}: ${MACRO_NAMES[layer[1]]}` : `${layer[0]} + ${c.id}`;
    if (e._id.textContent !== idTxt) e._id.textContent = idTxt;
    e._s.textContent = waiting ? 'pick up ↕' : fmt(m, v);
  }
  for (const k in swEls) {
    const e = swEls[k], pos = sw[k];
    e._lever.style.top = ['6%', '35%', '64%'][pos];
    e._label.textContent = (ctl.mode === CC.MODE_NOTES && e._s.notesNames ? e._s.notesNames : e._s.names)[pos];
  }
  for (const p of PADS) {
    const e = padEls[p.id];
    let html = `P${String(p.id).padStart(2, '0')}`, cls = 'idle';
    if (p.id === CC.P_STONE) { html += '<small>stone</small>'; cls = ''; }
    else if (ctl.mode === CC.MODE_PRESETS) {
      const k = p.id - CC.PAD_PRESET0;
      if (k >= 0 && k < CREEK_PRESETS.length) { html += `<small>${CREEK_PRESETS[k].name}</small>`; cls = k === ctl.preset ? 'sel' : ''; }
    } else if (ctl.mode === CC.MODE_NOTES) {
      const sub = noteLabel(p.id);
      if (sub) {
        html += `<small>${sub}</small>`;
        const isNote = p.id >= CC.PAD_NOTE0 && p.id <= CC.PAD_NOTE0 + 6;
        cls = ctl.touched[CC.P_SCALE] && isNote ? (p.id - CC.PAD_NOTE0 === ctl.scale ? 'sel' : '')   // current scale
          : ctl.held.includes(p.id) || ((p.id === CC.P_SCALE || p.id === CC.P_ROOT) && ctl.touched[p.id]) ? 'sel' : '';
      }
    } else if (p.id === CC.PAD_ALL) { html += '<small>All on</small>'; cls = ''; }
    else if (p.id === CC.PAD_STRESS && ctl.stressEnabled) { html += '<small>Stress</small>'; cls = ctl.stress ? 'sel' : ''; }
    else {
      const l = p.id - CC.PAD_LAYER0;
      if (l >= 0 && l < CK_LAYERS) { html += `<small>${CC_LAYER_NAMES[l]}</small>`; cls = (ctl.diagMask >> l) & 1 ? 'layer-on' : 'muted'; }
    }
    if (e._html !== html) { e._lbl.innerHTML = html; e._html = html; }
    e.classList.remove('idle', 'sel', 'muted', 'layer-on'); if (cls) e.classList.add(cls);
  }
  $('led').classList.toggle('on', ledState(performance.now()));
  $('dMode').textContent = ['PRESETS', 'NOTES', 'DIAGNOSTICS'][ctl.mode]; $('dMode').classList.toggle('diag', ctl.mode !== CC.MODE_PRESETS);
  $('dHold').textContent = ctl.latch ? 'LATCH' : ctl.hold ? 'HOLD' : 'LIVE'; $('dHold').classList.toggle('on', ctl.hold || ctl.latch);
  $('dPreset').textContent = ctl.mode === CC.MODE_DIAG ? 'layers ' + CC_LAYER_NAMES.map((n, l) => ((ctl.diagMask >> l) & 1 ? n[0] : '·')).join('')
    : ctl.mode === CC.MODE_NOTES ? `${ctl.scaleName()} · oct ${ctl.oct > 0 ? '+' : ''}${ctl.oct}` : CREEK_PRESETS[ctl.preset].name;
  $('hint').textContent = !running ? 'Press Start audio, then play the panel.'
    : ctl.mode === CC.MODE_DIAG ? (ctl.stress ? 'Stress test running: worst-case settings and note bursts. Tap P09 to stop.' : 'Diagnostics: P03–P07 mute / unmute layers, P08 all on.')
    : ctl.mode === CC.MODE_NOTES ? 'Tap a note for a drop, hold it to tune the creek. Hit a pad higher up = louder.'
    : 'P03–P06 recall presets. Knobs pick up once moved.';
}
const midiName = (m) => CC.NOTE_NAMES[((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1);
function noteLabel(id) {
  const note = id >= CC.PAD_NOTE0 && id <= CC.PAD_NOTE0 + 6;
  // P11 held: the note pads show the scales they select; P10 held: the root each pad would set
  if (ctl.touched[CC.P_SCALE] && note) return CC.SCALES[id - CC.PAD_NOTE0][0];
  if (ctl.touched[CC.P_ROOT] && note) return 'root ' + CC.NOTE_NAMES[((ctl.padMidi(id) - CC.BASE_MIDI) % 12 + 12) % 12];
  if (note) return midiName(ctl.padMidi(id));
  if (ctl.touched[CC.P_SCALE] && (id === CC.P_OCT_DN || id === CC.P_OCT_UP)) return id === CC.P_OCT_UP ? 'scale +' : 'scale −';
  return { 0: 'oct −', 2: 'oct +', 10: 'root', 11: 'scale' }[id] || '';
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
  monStats(d.s);
  const now = performance.now();
  for (let k = 0; k < d.f.length; k++) {
    dots.push({ t: now - (d.f.length - k) * 2, f: d.f[k], a: d.a[k], p: d.pan[k] });
    if (d.a[k] > 0.06) led.flickerUntil = now + 20;
  }
  const s = d.s;
  $('dRate').textContent = `${s.rate.toFixed(0)} bubbles/s`;
  $('stats').textContent = `eddy ${(s.eddy * 100).toFixed(0)}% · splashes ${s.splashRate.toFixed(1)}/s · surface ${s.surfaceRate.toFixed(0)}/s · ` +
    `voices ${s.active} (max ${s.maxActive}) · steals ${s.steals} · mean pitch ${s.meanF.toFixed(0)} Hz · ` +
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

// ------------------------------------------------------------------ MIDI (Web MIDI -> same Controls.midi as the firmware)
let midiOn = false;
$('midiBtn').onclick = async () => {
  if (midiOn) return;
  if (!navigator.requestMIDIAccess) { $('midiBtn').textContent = 'No Web MIDI (use Chrome)'; return; }
  try {
    const acc = await navigator.requestMIDIAccess();
    const hook = () => { let n = 0; acc.inputs.forEach((inp) => { n++; inp.onmidimessage = (e) => { const d = e.data; if (d.length >= 2) { ctl.midi(d[0], d[1], d.length > 2 ? d[2] : 0); ensureAudio(); } }; });
      $('midiBtn').textContent = n ? `MIDI: ${n} input${n > 1 ? 's' : ''}` : 'MIDI: no inputs'; };
    acc.onstatechange = hook; hook(); midiOn = true; $('midiBtn').classList.add('on');
  } catch (err) { $('midiBtn').textContent = 'MIDI blocked'; }
};

// ------------------------------------------------------------------ monitor (web/monitor.html in another tab)
// The emulator sends the same report lines as the monitor firmware: once a second
// CREEK (voices, events per second) and CREEKL (layers), and CREEKE for every press.
let monBc = null;
try { monBc = new BroadcastChannel('creek-monitor'); } catch (e) { /* old browser */ }
const monSend = (line) => { if (monBc) monBc.postMessage(line); };
const monMods = () => (ctl.touched[CC.P_ROOT] ? 1 : 0) | (ctl.touched[CC.P_SCALE] ? 2 : 0);
{ // wrap the controls so every press from mouse, keyboard or MIDI-free paths is reported
  const down = ctl.padDown.bind(ctl), up = ctl.padUp.bind(ctl);
  ctl.padDown = (pad, vel) => { monSend(`CREEKE p ${pad} ${ctl.mode} ${Math.round((vel || 0.7) * 100)} ${monMods()}`); down(pad, vel); };
  ctl.padUp = (pad) => { monSend(`CREEKE r ${pad}`); up(pad); };
}
const monKnob = new Float32Array(8), monSw = { mode: -1, hold: -1 };
for (const c of CONTROLS) monKnob[c.p] = ctl.value[macroFor(c)];   // report changes only, not the start state
let monPrev = null, monLast = 0;
function monStats(s) {
  const now = performance.now();
  if (now - monLast < 1000) return;
  const dt = monPrev ? (now - monLast) / 1000 : 1, d = (k) => (monPrev ? Math.round((s[k] - monPrev[k]) / dt) : 0);
  monSend(`CREEK v ${s.active} p ${s.maxActive} b ${d('births')} s ${d('steals')} d ${d('drops')} f ${d('splashes')} t ${d('surface')} q ${s.queueDrops} l ${d('limited')} x ${ctl.stress ? 1 : 0}`);
  monSend(`CREEKL a ${s.active - s.activeUser} u ${s.activeUser} f ${s.activeFalls} t ${s.activeSurface} e ${Math.round(s.eddy * 100)} g ${Math.round(s.gate * 100)} y ${Math.round(s.activity * 100)} h ${ctl.held.length} o ${ctl.mode} m ${macros.mask} n ${s.stones}`);
  monPrev = s; monLast = now;
}
setInterval(() => {   // knob and switch changes (as the firmware reports them)
  for (const c of CONTROLS) {
    const m = macroFor(c), v = ctl.value[m];
    if (take[m].tracking && Math.abs(v - monKnob[c.p]) > 0.01 && Math.abs(physPos[c.p] - v) < 0.02) { monKnob[c.p] = v; monSend(`CREEKE k ${c.p} ${m} ${Math.round(v * 1000)} ${monMods()}`); }
  }
  if (sw.mode !== monSw.mode) { if (monSw.mode >= 0) monSend(`CREEKE w 0 ${sw.mode}`); monSw.mode = sw.mode; }
  if (sw.hold !== monSw.hold) { if (monSw.hold >= 0) monSend(`CREEKE w 1 ${sw.hold}`); monSw.hold = sw.hold; }
}, 50);
$('monitorBtn').onclick = () => window.open('monitor.html', 'creek-monitor');

// ------------------------------------------------------------------ wiring
$('power').onclick = () => (running ? stop() : start());
$('seed').onchange = () => { if (node) node.port.postMessage({ type: 'reset', seed: seed() }); };
$('reseed').onclick = () => { $('seed').value = (Math.random() * 4294967295) >>> 0; $('seed').onchange(); };
document.addEventListener('keydown', (e) => {
  if (e.target.tagName === 'INPUT') return;
  const n = Number(e.key);
  if (e.key === ' ') { e.preventDefault(); running ? stop() : start(); }
  else if (e.repeat) return;
  else if (e.key === 'd' || e.key === 'D') sw.mode = (sw.mode + 1) % 3;
  else if (e.key === 'h' || e.key === 'H') sw.hold = sw.hold === 2 ? 0 : 2;
  else if (e.key === 's' || e.key === 'S') ctl.padDown(CC.P_STONE, 0.8);
  else if (ctl.mode === CC.MODE_PRESETS && n >= 1 && n <= 4) ctl.padDown(CC.PAD_PRESET0 + n - 1, 0.7);
  else if (ctl.mode === CC.MODE_NOTES && n >= 1 && n <= 7) ctl.padDown(CC.PAD_NOTE0 + n - 1, 0.7);
  else if (ctl.mode === CC.MODE_NOTES && (e.key === 'z' || e.key === 'x')) ctl.padDown(e.key === 'x' ? CC.P_OCT_UP : CC.P_OCT_DN, 0.7);
  else if (ctl.mode === CC.MODE_DIAG && n >= 1 && n <= 5) ctl.padDown(CC.PAD_LAYER0 + n - 1, 0.7);
  else if (ctl.mode === CC.MODE_DIAG && n === 6) ctl.padDown(CC.PAD_ALL, 0.7);
});
document.addEventListener('keyup', (e) => {
  const n = Number(e.key);
  if (n >= 1 && n <= 7) ctl.padUp(CC.PAD_NOTE0 + n - 1);
  if (e.key === 'z' || e.key === 'x') ctl.padUp(e.key === 'x' ? CC.P_OCT_UP : CC.P_OCT_DN);
  if (e.key === 's' || e.key === 'S') ctl.padUp(CC.P_STONE);
  if (n >= 1 && n <= 6) { ctl.padUp(CC.PAD_PRESET0 + n - 1); ctl.padUp(CC.PAD_ALL); }
});
buildBoard(); frame();
