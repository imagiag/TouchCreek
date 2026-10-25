// Creek controls: panel state -> engine macros. Shared by the browser page and
// the Touch 2 firmware (C++ port in daisy/creek/controls.*). Hardware details
// (pickup, debouncing, LED) live in the hardware glue, not here.
//
// Panel (brief, "Suggested panel assignment"):
//   left fader S36 = Flow, right fader S37 = Roughness,
//   S31 = Scale, S32 = Aeration, S33 = Gain, S34 = Width (S30, S35 reserved)
//   left switch  (S09/S10): up/centre = Performance, down = Diagnostics
//   right switch (S07/S08): up/centre = live, down = Hold slow state
//   Performance: P03..P06 recall the four presets (250 ms glide)
//   Diagnostics: P03..P07 toggle flow / bubbles / eddies / falls / surface, P08 = all on

const CREEK_PRESETS = [
  { key: 'trickle', name: 'Quiet trickle', flow: 0.20, roughness: 0.25, scale: 0.20, aeration: 0.30 },
  { key: 'rocky',   name: 'Rocky creek',   flow: 0.50, roughness: 0.60, scale: 0.45, aeration: 0.55 },
  { key: 'cascade', name: 'Small cascade', flow: 0.75, roughness: 0.80, scale: 0.50, aeration: 0.85 },
  { key: 'deeper',  name: 'Deeper run',    flow: 0.65, roughness: 0.35, scale: 0.80, aeration: 0.40 },
];
const CC = {
  M_FLOW: 0, M_ROUGH: 1, M_SCALE: 2, M_AER: 3, M_GAIN: 4, M_WIDTH: 5, M_COUNT: 6,
  PAD_PRESET0: 3,           // P03..P06
  PAD_LAYER0: 3,            // P03..P07 in diagnostics
  PAD_ALL: 8,               // P08 in diagnostics
  PRESET_GLIDE_SEC: 0.25,
  EV_NONE: 0, EV_PRESET: 1, EV_LAYER: 2, EV_ALL: 3, EV_MODE: 4,
};
const CC_LAYER_NAMES = ['Flow', 'Bubbles', 'Eddies', 'Falls', 'Surface'];

// knob 0..1 -> output gain: silent at the bottom, 0 dB at noon, +6 dB fully up
function ccGain(v) {
  if (v < 0.01) return 0;
  const dB = v < 0.5 ? -30 + 30 * (v / 0.5) : 6 * ((v - 0.5) / 0.5);
  return Math.pow(10, dB / 20);
}
// knob 0..1 -> width: 0 = mono, noon = 1 (natural), fully up = WIDTH_MAX
function ccWidth(v) { return v < 0.5 ? v * 2 : 1 + (CK.WIDTH_MAX - 1) * ((v - 0.5) / 0.5); }

class CreekControls {
  constructor() {
    this.value = new Float32Array(CC.M_COUNT);   // knob positions (0..1) as the engine sees them
    const p = CREEK_PRESETS[0];
    this.value[CC.M_FLOW] = p.flow; this.value[CC.M_ROUGH] = p.roughness;
    this.value[CC.M_SCALE] = p.scale; this.value[CC.M_AER] = p.aeration;
    this.value[CC.M_GAIN] = 0.5; this.value[CC.M_WIDTH] = 0.5;
    this.glideFrom = new Float32Array(4); this.glideTo = new Float32Array(4);
    this.glideT = -1;            // < 0: no glide running
    this.preset = 0;
    this.diag = false;
    this.diagMask = 0x1F;
    this.hold = false;
    this.events = [];            // {type, arg} for the UI (LED blinks, display)
    this.lastT = 0;
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
  setMode(diag) {
    if (diag === this.diag) return;
    this.diag = diag;
    this.events.push({ type: CC.EV_MODE, arg: diag ? 1 : 0 });
  }
  padDown(pad) {
    if (!this.diag) {
      const i = pad - CC.PAD_PRESET0;
      if (i >= 0 && i < CREEK_PRESETS.length) this.recallPreset(i);
    } else {
      const l = pad - CC.PAD_LAYER0;
      if (l >= 0 && l < CK_LAYERS) { this.diagMask ^= 1 << l; this.events.push({ type: CC.EV_LAYER, arg: (this.diagMask >> l) & 1 }); }
      else if (pad === CC.PAD_ALL) { this.diagMask = 0x1F; this.events.push({ type: CC.EV_ALL, arg: 0 }); }
    }
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
  }
  gliding() { return this.glideT >= 0; }
  compute(m) {
    m.flow = this.value[CC.M_FLOW]; m.roughness = this.value[CC.M_ROUGH];
    m.scale = this.value[CC.M_SCALE]; m.aeration = this.value[CC.M_AER];
    m.gain = ccGain(this.value[CC.M_GAIN]); m.width = ccWidth(this.value[CC.M_WIDTH]);
    m.mask = this.diag ? this.diagMask : 0x1F;
    m.hold = this.hold;
    return m;
  }
}

if (typeof module !== 'undefined') module.exports = { CREEK_PRESETS, CC, CC_LAYER_NAMES, CreekControls, ccGain, ccWidth };
