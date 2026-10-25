// Creek controls: panel state -> engine macros + note-mode drops. Shared by the
// browser emulator and the Touch 2 firmware (C++ port in daisy/creek/controls.*).
// Hardware details (pickup, debouncing, LED) live in the hardware glue.
//
// Panel:
//   left fader S36 = Level, right fader S37 = Roughness,
//   S31 = Scale, S32 = Aeration, S33 = Flow, S34 = Decay (P10 + S34 = Width),
//   S30 = Free flow (Notes mode), S35 = Variety,
//   P11 + S37 = filter cutoff, P11 + S36 = resonance (as TouchPulsar)
//   left switch  (S09/S10): up = Presets, centre = Notes, down = Diagnostics
//   right switch (S07/S08): down = Hold slow state; in Notes = Latch (touched notes keep pouring,
//                tap a latched note again to release it)
//   Presets:     P03..P06 recall the four presets (250 ms glide)
//   Notes:       P03..P09 play the scale (tap = tuned drop, hold = the creek's
//                bubbles gather onto the held notes), P00 / P02 octave,
//                P11 + P00 / P02 / P03..P09 = scale, P10 + note pad = root
//   MIDI (any channel): notes play like note pads (drop + pour, chromatic), velocity = drop level,
//                sustain pedal holds notes, pitch bend +-2, mod wheel / aftertouch = Surge,
//                CCs for every knob, CC 14/15 scale/root, Program Change 1-4 = presets
//   Diagnostics: P03..P07 toggle flow / bubbles / eddies / falls / surface, P08 = all on,
//                P09 = stress test (worst-case settings, rapid jumps, note bursts)

const CREEK_PRESETS = [
  { key: 'trickle', name: 'Quiet trickle', flow: 0.20, roughness: 0.25, scale: 0.20, aeration: 0.30 },
  { key: 'rocky',   name: 'Rocky creek',   flow: 0.50, roughness: 0.60, scale: 0.45, aeration: 0.55 },
  { key: 'cascade', name: 'Small cascade', flow: 0.75, roughness: 0.80, scale: 0.50, aeration: 0.85 },
  { key: 'deeper',  name: 'Deeper run',    flow: 0.65, roughness: 0.35, scale: 0.80, aeration: 0.40 },
];
const CC = {
  M_FLOW: 0, M_ROUGH: 1, M_SCALE: 2, M_AER: 3, M_LEVEL: 4, M_WIDTH: 5, M_FREE: 6, M_VARIETY: 7, M_DECAY: 8, M_CUTOFF: 9, M_RES: 10, M_COUNT: 11,
  MODE_PRESETS: 0, MODE_NOTES: 1, MODE_DIAG: 2,
  P_OCT_DN: 0, P_OCT_UP: 2, P_ROOT: 10, P_SCALE: 11,
  PAD_PRESET0: 3,           // P03..P06
  PAD_NOTE0: 3,             // P03..P09
  PAD_LAYER0: 3,            // P03..P07 in diagnostics
  PAD_ALL: 8,               // P08 in diagnostics
  PAD_STRESS: 9,            // P09 in diagnostics
  PRESET_GLIDE_SEC: 0.25,
  BASE_MIDI: 72,            // P03 with root C, octave 0 = C5 (523 Hz, a 5.7 mm bubble)
  MAX_HELD: 4,
  EV_NONE: 0, EV_PRESET: 1, EV_LAYER: 2, EV_ALL: 3, EV_MODE: 4, EV_SCALE: 5, EV_OCT: 6, EV_ROOT: 7, EV_NOTE: 8, EV_STRESS: 9, EV_CC: 10,
  MIDI_KEY: 128,            // held keys: pads 0..11, MIDI notes 128 + note
  // CC -> macro (CC 1 / channel pressure = Surge, 64 sustain, 14 scale, 15 root, 120/123 all notes off)
  CC_MAP: { 7: 4, 20: 0, 21: 1, 22: 2, 23: 3, 24: 6, 25: 7, 26: 8, 27: 5, 102: 9, 103: 10 },   // 102/103 as TouchPulsar
  BEND_SEMIS: 2,
  // same scales as TouchPulsar (semitones above the root for P03..P09)
  SCALES: [
    ['Chromatic', [0, 1, 2, 3, 4, 5, 6]],
    ['Major', [0, 2, 4, 5, 7, 9, 11]],
    ['Minor', [0, 2, 3, 5, 7, 8, 10]],
    ['Min Pent', [0, 3, 5, 7, 10, 12, 15]],
    ['Maj Pent', [0, 2, 4, 7, 9, 12, 14]],
    ['Dorian', [0, 2, 3, 5, 7, 9, 10]],
    ['Harm Minor', [0, 2, 3, 5, 7, 8, 11]],
  ],
  NOTE_NAMES: ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'],
};
const CC_LAYER_NAMES = ['Flow', 'Bubbles', 'Eddies', 'Falls', 'Surface'];
const CC_MODE_NAMES = ['PRESETS', 'NOTES', 'DIAG'];

// Level fader: as TouchPulsar, gain = 2 v^2 (0 dB at ~70 %, +6 dB at the top)
function ccLevel(v) { return 2 * v * v; }
// knob 0..1 -> width: 0 = mono, noon = 1 (natural), fully up = WIDTH_MAX
function ccWidth(v) { return v < 0.5 ? v * 2 : 1 + (CK.WIDTH_MAX - 1) * ((v - 0.5) / 0.5); }
function ccHz(midi) { return 440 * Math.pow(2, (midi - 69) / 12); }

class CreekControls {
  constructor() {
    this.value = new Float32Array(CC.M_COUNT);   // control positions (0..1) as the engine sees them
    const p = CREEK_PRESETS[0];
    this.value[CC.M_FLOW] = p.flow; this.value[CC.M_ROUGH] = p.roughness;
    this.value[CC.M_SCALE] = p.scale; this.value[CC.M_AER] = p.aeration;
    this.value[CC.M_LEVEL] = 0.7071; this.value[CC.M_WIDTH] = 0.5;
    this.value[CC.M_FREE] = 1; this.value[CC.M_VARIETY] = 0.5; this.value[CC.M_DECAY] = 0.5;
    this.value[CC.M_CUTOFF] = 1; this.value[CC.M_RES] = 0;   // filter open at power-up
    this.glideFrom = new Float32Array(4); this.glideTo = new Float32Array(4);
    this.glideT = -1;            // < 0: no glide running
    this.preset = 0;
    this.mode = CC.MODE_PRESETS;
    this.diag = false;           // mode === MODE_DIAG (kept for the UI)
    this.diagMask = 0x1F;
    this.hold = false;
    this.touched = new Uint8Array(12);
    this.scale = 4;              // Maj Pent
    this.root = 0;               // C
    this.oct = 0;
    this.held = [];              // note pads held (newest last), up to MAX_HELD
    this.drops = [];             // {hz, vel} for the host to send as engine drop events
    this.events = [];            // {type, arg} for the UI (LED blinks, display)
    this.lastT = 0;
    this.stress = false;         // diagnostics P09: worst-case stress test
    this.latch = false;          // Notes + right switch down: notes stay held after release
    this.midiDown = new Uint8Array(128); this.sustain = false;
    this.surge = 0; this.wheel = 0; this.pressure = 0; this.bend = 0;   // MIDI performance controls
    this.stressV = new Float32Array(4); this.stressNext = 0; this.stressDrop = 0; this.rs = 0x12345678;
  }
  // a physical control moved (after pickup): cancels a running glide for that macro
  setKnob(k, v) {
    this.value[k] = Math.min(1, Math.max(0, v));
    if (k < 4 && this.glideT >= 0) { this.glideFrom[k] = this.glideTo[k] = this.value[k]; }
  }
  recallPreset(i) {
    const p = CREEK_PRESETS[i];
    for (let k = 0; k < 4; k++) this.glideFrom[k] = this.value[k];
    this.glideTo[0] = p.flow; this.glideTo[1] = p.roughness; this.glideTo[2] = p.scale; this.glideTo[3] = p.aeration;
    this.glideT = 0; this.preset = i;
    this.events.push({ type: CC.EV_PRESET, arg: i });
  }
  setMode(m) {
    if (m === this.mode) return;
    this.mode = m; this.diag = m === CC.MODE_DIAG;
    if (m !== CC.MODE_DIAG && this.stress) { this.stress = false; this.events.push({ type: CC.EV_STRESS, arg: 0 }); }
    if (m !== CC.MODE_NOTES) this.held = this.held.filter((k) => k >= CC.MIDI_KEY);   // MIDI notes play in every mode
    this.events.push({ type: CC.EV_MODE, arg: m });
  }
  chromatic() { return this.scale === 0; }
  octRange() { return this.chromatic() ? [-2, 4] : [-1, 2]; }
  keyMidi(k) { return k >= CC.MIDI_KEY ? k - CC.MIDI_KEY : this.padMidi(k); }
  // MIDI note of a note pad: root + scale degree + octave (or 7-semitone page in Chromatic)
  padMidi(pad) { return CC.BASE_MIDI + this.root + CC.SCALES[this.scale][1][pad - CC.PAD_NOTE0] + this.oct * (this.chromatic() ? 7 : 12); }
  setScale(s) {
    this.scale = s;
    const r = this.octRange(); this.oct = Math.max(r[0], Math.min(r[1], this.oct));
    this.events.push({ type: CC.EV_SCALE, arg: s });
  }
  padDown(pad, vel) {
    if (pad < 0 || pad > 11) return;
    this.touched[pad] = 1;
    if (this.mode === CC.MODE_PRESETS) {
      const i = pad - CC.PAD_PRESET0;
      if (i >= 0 && i < CREEK_PRESETS.length) this.recallPreset(i);
    } else if (this.mode === CC.MODE_DIAG) {
      const l = pad - CC.PAD_LAYER0;
      if (l >= 0 && l < CK_LAYERS) { this.diagMask ^= 1 << l; this.events.push({ type: CC.EV_LAYER, arg: (this.diagMask >> l) & 1 }); }
      else if (pad === CC.PAD_ALL) { this.diagMask = 0x1F; this.events.push({ type: CC.EV_ALL, arg: 0 }); }
      else if (pad === CC.PAD_STRESS) { this.stress = !this.stress; this.stressNext = 0; this.events.push({ type: CC.EV_STRESS, arg: this.stress ? 1 : 0 }); }
    } else {
      const note = pad >= CC.PAD_NOTE0 && pad <= CC.PAD_NOTE0 + 6;
      if (this.touched[CC.P_SCALE] && pad !== CC.P_SCALE) {          // P11 + pad: scale
        if (pad === CC.P_OCT_DN || pad === CC.P_OCT_UP) this.setScale((this.scale + (pad === CC.P_OCT_UP ? 1 : 6)) % 7);
        else if (note) this.setScale(pad - CC.PAD_NOTE0);
      } else if (this.touched[CC.P_ROOT] && pad !== CC.P_ROOT) {     // P10 + note pad: root
        if (note) { this.root = ((this.padMidi(pad) - CC.BASE_MIDI) % 12 + 12) % 12; this.events.push({ type: CC.EV_ROOT, arg: this.root }); }
      } else if (pad === CC.P_OCT_DN || pad === CC.P_OCT_UP) {
        const r = this.octRange();
        this.oct = Math.max(r[0], Math.min(r[1], this.oct + (pad === CC.P_OCT_UP ? 1 : -1)));
        this.events.push({ type: CC.EV_OCT, arg: this.oct });
      } else if (note && this.latch && this.held.includes(pad)) {
        this.held.splice(this.held.indexOf(pad), 1);                // latched note tapped again: release it
        this.events.push({ type: CC.EV_NOTE, arg: pad });
      } else if (note) {
        const v = vel > 0 ? Math.min(1, vel) : 0.7;
        this.addHeld(pad, ccHz(this.padMidi(pad)), v);
      }
    }
  }
  padUp(pad) {
    if (pad < 0 || pad > 11) return;
    this.touched[pad] = 0;
    if (this.latch && this.mode === CC.MODE_NOTES) return;          // latched: the note keeps pouring
    const j = this.held.indexOf(pad); if (j >= 0) this.held.splice(j, 1);
  }
  // right switch: Latch in Notes, Hold (slow state) elsewhere
  setRightSwitch(down) {
    const latch = down && this.mode === CC.MODE_NOTES;
    if (this.latch && !latch)                                       // release what isn't touched / still down
      this.held = this.held.filter((k) => (k >= CC.MIDI_KEY ? this.midiDown[k - CC.MIDI_KEY] || this.sustain : this.touched[k]));
    this.latch = latch;
    this.hold = down && this.mode !== CC.MODE_NOTES;
  }
  // ---------------- MIDI (raw bytes, same entry point as the firmware) ----------------
  addHeld(key, hz, vel) {
    this.drops.push({ hz, vel });
    const j = this.held.indexOf(key); if (j >= 0) this.held.splice(j, 1);
    this.held.push(key); if (this.held.length > CC.MAX_HELD) this.held.shift();
    this.events.push({ type: CC.EV_NOTE, arg: key });
  }
  midiNoteOn(n, vel) {
    const key = CC.MIDI_KEY + n;
    this.midiDown[n] = 1;
    if (this.latch && this.held.includes(key)) { this.held.splice(this.held.indexOf(key), 1); return; }
    this.addHeld(key, ccHz(n) * Math.pow(2, this.bend / 12), vel);
  }
  midiNoteOff(n) {
    this.midiDown[n] = 0;
    if (this.latch || this.sustain) return;
    const j = this.held.indexOf(CC.MIDI_KEY + n); if (j >= 0) this.held.splice(j, 1);
  }
  midiCc(c, v) {           // v 0..127
    const x = v / 127, m = CC.CC_MAP[c];
    if (m !== undefined) { this.setKnob(m, x); this.events.push({ type: CC.EV_CC, arg: m }); }
    else if (c === 1) { this.wheel = x; this.surge = Math.max(this.wheel, this.pressure); }
    else if (c === 64) {
      this.sustain = v >= 64;
      if (!this.sustain && !this.latch) this.held = this.held.filter((k) => k < CC.MIDI_KEY || this.midiDown[k - CC.MIDI_KEY]);
    }
    else if (c === 14) this.setScale(Math.min(6, Math.floor(v * 7 / 128)));
    else if (c === 15) { this.root = Math.min(11, Math.floor(v * 12 / 128)); this.events.push({ type: CC.EV_ROOT, arg: this.root }); }
    else if (c === 120 || c === 123) { this.held = this.held.filter((k) => k < CC.MIDI_KEY); this.midiDown.fill(0); this.sustain = false; }
  }
  midi(st, d1, d2) {
    const t = st & 0xF0;
    if (t === 0x90 && d2 > 0) this.midiNoteOn(d1, d2 / 127);
    else if (t === 0x80 || (t === 0x90 && d2 === 0)) this.midiNoteOff(d1);
    else if (t === 0xB0) this.midiCc(d1, d2);
    else if (t === 0xC0) { if (d1 < CREEK_PRESETS.length) this.recallPreset(d1); }
    else if (t === 0xD0) { this.pressure = d1 / 127; this.surge = Math.max(this.wheel, this.pressure); }
    else if (t === 0xE0) this.bend = (((d2 << 7) | d1) - 8192) / 8192 * CC.BEND_SEMIS;
  }
  // t in seconds (monotonic)
  tick(t) {
    const dt = Math.max(0, t - this.lastT); this.lastT = t;
    if (this.glideT >= 0) {
      this.glideT += dt;
      const x = Math.min(1, this.glideT / CC.PRESET_GLIDE_SEC), s = x * x * (3 - 2 * x);
      for (let k = 0; k < 4; k++) this.value[k] = this.glideFrom[k] + (this.glideTo[k] - this.glideFrom[k]) * s;
      if (x >= 1) this.glideT = -1;
    }
    if (this.stress) this.stressTick(t);
  }
  rnd() { let x = this.rs; x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; this.rs = x; return x / 4294967296; }
  // Worst case for the engine: mostly maximal settings with sudden jumps every
  // 50-400 ms (incl. Flow 0 <-> 1), plus bursts of loud drops at ~20 per second.
  stressTick(t) {
    if (t >= this.stressNext) {
      for (let k = 0; k < 4; k++) { const u = this.rnd(); this.stressV[k] = u < 0.65 ? 1 : (u < 0.8 ? 0 : this.rnd()); }
      this.stressV[CC.M_FLOW] = this.rnd() < 0.85 ? 1 : 0;
      this.stressNext = t + 0.05 + 0.35 * this.rnd();
    }
    while (t >= this.stressDrop) {
      this.drops.push({ hz: 150 * Math.pow(2, 6 * this.rnd()), vel: 1 });
      this.stressDrop = Math.max(this.stressDrop, t - 0.1) + 0.05;
    }
  }
  gliding() { return this.glideT >= 0; }
  scaleName() { return CC.NOTE_NAMES[this.root] + ' ' + CC.SCALES[this.scale][0]; }
  // active scale as a 12-bit pitch-class mask (bit 0 = C); Chromatic = all 12
  scaleMask() {
    if (this.chromatic()) return 0xFFF;
    let m = 0;
    for (const s of CC.SCALES[this.scale][1]) m |= 1 << ((this.root + s) % 12);
    return m;
  }
  compute(m) {
    m.flow = this.value[CC.M_FLOW]; m.roughness = this.value[CC.M_ROUGH];
    m.scale = this.value[CC.M_SCALE]; m.aeration = this.value[CC.M_AER];
    m.gain = ccLevel(this.value[CC.M_LEVEL]); m.width = ccWidth(this.value[CC.M_WIDTH]);
    // Surge (mod wheel / aftertouch): the water rushes on top of the knob settings
    const s = this.surge;
    if (s > 0) { m.flow += s * (1 - m.flow); m.roughness += 0.6 * s * (1 - m.roughness); m.aeration += 0.4 * s * (1 - m.aeration); }
    m.mask = this.mode === CC.MODE_DIAG ? this.diagMask : 0x1F;
    if (this.stress) { m.flow = this.stressV[0]; m.roughness = this.stressV[1]; m.scale = this.stressV[2]; m.aeration = this.stressV[3]; m.mask = 0x1F; }
    m.hold = this.hold;
    if (!m.tuneHz) m.tuneHz = [0, 0, 0, 0];
    const bendMul = Math.pow(2, this.bend / 12);
    for (let k = 0; k < 4; k++) m.tuneHz[k] = k < this.held.length ? ccHz(this.keyMidi(this.held[k])) * bendMul : 0;
    m.tune = this.held.length > 0 ? 1 : 0;
    m.freeFlow = this.mode === CC.MODE_NOTES ? this.value[CC.M_FREE] : 1;   // presets / diagnostics always flow
    m.variety = this.value[CC.M_VARIETY]; m.decay = this.value[CC.M_DECAY];
    m.scaleMask = this.scaleMask();
    m.cutoff = this.value[CC.M_CUTOFF]; m.resonance = this.value[CC.M_RES];
    return m;
  }
}

if (typeof module !== 'undefined') module.exports = { CREEK_PRESETS, CC, CC_LAYER_NAMES, CC_MODE_NAMES, CreekControls, ccLevel, ccWidth, ccHz };
