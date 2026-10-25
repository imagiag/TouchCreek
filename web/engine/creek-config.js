// Creek — all tuning constants in one place (brief: "keep their constants in one
// configuration file"). The C++ engine uses the same numbers: run
//   node tools/gen-config.js
// to regenerate daisy/creek/config.h from this file. Only flat numbers and
// arrays of numbers here, so the generator stays trivial.
// Everything here is an initial engineering choice, to be tuned by ear.

const CK = {
  // ---- timing ----
  CONTROL_HZ: 250,          // shared flow-state update rate
  MICRO: 16,                // samples per micro tick (voice glide, gain ramps)

  // ---- macros ----
  MACRO_SMOOTH_SEC: 0.05,   // 30–80 ms smoothing of physical controls
  UTIL_SMOOTH_SEC: 0.05,    // gain / width smoothing
  MUTE_SMOOTH_SEC: 0.02,    // diagnostic layer mute ramp

  // ---- shared flow state (zones: 0 flow bed, 1 rock wake, 2 drop) ----
  KZ: [0.6, 1.0, 1.3],      // turbulence factor per zone
  TAU_E: 0.35, TAU_T: 0.18, TAU_B: 0.12,
  ETA_MIN_SEC: 0.2, ETA_MAX_SEC: 4.0, // smooth random targets for eta
  J_DECAY_SEC: 0.3, J_CAP: 0.6,       // splash entrainment impulse (Phase 2)
  E_GATE: 0.004,            // below this energy the scene fades to silence
  E_GATE_FULL: 0.012,       // full level above this energy

  // ---- spatial ----
  ZONE_PAN: [0.0, -0.35, 0.35],       // -1 left .. +1 right
  ZONE_JITTER: [0.30, 0.14, 0.14],    // per-event pan jitter

  // ---- bubble pool ----
  VOICES: 24,               // voice budget
  VOICE_SLOTS: 32,          // ceiling: spare slots carry stolen voices' 2 ms fades
  STEAL_FADE_SEC: 0.002,
  RETIRE_DB: -80,           // relative to the event's start level
  RETIRE_ABS_DB: -100,      // absolute (before output gain)
  MAX_BIRTHS_PER_MICRO: 4,
  QUEUE_SIZE: 64,           // pending events (splash children, Trigger())

  // radius -> pitch: f0 = 3 / r  (r in metres) [van den Doel]
  R_MIN_MM: 0.4, R_MAX_MM: 6.0,
  R_SKEW: 1.15,             // >1 favours small radii (u^skew in log-radius)
  SCALE_MUL_MIN: 0.6, SCALE_MUL_MAX: 1.8,
  F_MAX_RATIO: 0.4,         // every voice, incl. glide, stays below 0.4 Fs
  F_MIN_HZ: 150,

  TAU_MIN: 0.008, TAU_MAX: 0.080,     // decay, longer for larger bubbles
  TAU_JITTER: 0.30,                   // +- relative
  ATTACK_MIN: 0.0005, ATTACK_MAX: 0.002,
  RISE_PROB: 0.30,          // minority of voices glide upward ...
  RISE_MAX: 0.25,           // ... by up to 25 %
  RISE_TIME_TAUS: 2.5,      // glide reaches its end after this many tau

  // birth rate per zone: lambda = RATE_MAX * w_z * B^RATE_EXP (0 when B ~ 0)
  RATE_MAX: 120,
  RATE_EXP: 0.5,
  ZONE_RATE_W: [0.30, 1.0, 0.75],
  B_MIN: 0.0002,

  // loudness: many quiet events, occasional stronger low ones
  AMP_DB_RANGE: 34,         // event level spread below the maximum
  AMP_SKEW: 0.55,           // u^skew -> weighted toward quiet
  AMP_R_EXP: 0.45,          // larger bubbles louder: (r / 1 mm)^exp
  AMP_R_MAX: 2.2,
  BUBBLE_GAIN: 0.45,
  DENSITY_REF: 60,          // headroom: gain / sqrt(1 + rate / ref)

  // ---- flow bed (noise layers) ----
  BODY_HP_HZ: 90,
  BODY_LP_HZ: [480, 1100],  // cutoff range (low T .. high T)
  BODY_Q: 0.6,
  MID_HZ: [700, 1800],
  MID_Q: 0.55,
  HIGH_HZ: [2200, 4200],    // sheen 1–6 kHz
  HIGH_Q: 0.5,
  MID_CORR: 0.6,            // L/R shared part of the mid band
  HIGH_CORR: 0.35,
  BED_GAIN: 0.9,
  BODY_GAIN: 1.0,
  MID_GAIN: 0.30,
  HIGH_GAIN: 0.10,
  BED_E_EXP: 0.45,
  BED_TOP_SQUASH: 0.95,     // headroom at full flow: level / (1 + k E^2)          // level ~ E^exp
  MOD_DEPTH: [0.12, 0.45],  // gain modulation depth, low .. high roughness
  CUT_DEPTH: 0.25,          // cutoff modulation depth (fraction, log)
  FLUTTER_MIN_SEC: 0.04, FLUTTER_MAX_SEC: 0.3, // faster turbulent flutter (scaled by T)
  SCALE_TILT: 0.45,         // larger Scale lowers the bed's spectral centre

  // ---- output ----
  DC_HZ: 25,
  LIMIT_CEIL: 0.891,        // -1 dBFS
  LIMIT_RELEASE_SEC: 0.15,
  OUT_GAIN_DB: [-24, 6],    // trim 3 range; noon ~ 0 dB
  WIDTH_MAX: 1.6,           // trim 4: 0 = mono .. 1.6
};

const CK_ZONES = 3;
const CK_LAYERS = 5;        // flow, bubbles, eddies, waterfall, surface
const CK_L_FLOW = 0, CK_L_BUBBLES = 1, CK_L_EDDIES = 2, CK_L_FALLS = 3, CK_L_SURFACE = 4;

if (typeof module !== 'undefined') module.exports = { CK, CK_ZONES, CK_LAYERS, CK_L_FLOW, CK_L_BUBBLES, CK_L_EDDIES, CK_L_FALLS, CK_L_SURFACE };
