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
  GATE_FADE_SEC: 0.15,      // as the scene gate closes, the creek's own ringing voices fade this fast (drops / stones untouched)

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
  QUEUE_SIZE: 96,           // pending events (splash children, stone bursts, Trigger())

  // radius -> pitch: f0 = 3 / r  (r in metres) [van den Doel]
  R_MIN_MM: 0.4, R_MAX_MM: 6.0,
  R_SKEW: 1.15,             // >1 favours small radii (u^skew in log-radius); Tone knob scales it ...
  TONE_SKEW_RANGE: 6,       // ... by 6^(-1..+1): left = more big, low bubbles (dark), right = more small, high ones (bright) ...
  TONE_OCT: [1, 0.6],       // ... and towards the ends all bubbles shift by up to [dark, bright] octaves (x2 bigger / x0.66 smaller)
  TONE_OCT_EXP: 2,          // the octave shift comes in late (|2t-1|^2), so noon stays the default mix
  SCALE_MUL_MIN: 0.6, SCALE_MUL_MAX: 1.8,
  SCALE_MUL_TOP: 8.0,       // ... bending up near the top of the knob to 8x for bass (presets unchanged)
  SCALE_TOP_EXP: 8,         // how late the bend starts (S^8: +27 % at 0.8, x4.4 at 1)
  F_MAX_RATIO: 0.4,         // every voice, incl. glide, stays below 0.4 Fs
  F_MIN_HZ: 35,

  TAU_MIN: 0.008, TAU_MAX: 0.080,     // decay, longer for larger bubbles (at R_MAX_MM) ...
  TAU_X_MAX: 1.8,                     // ... and still longer above it: up to ~0.5 s for the deepest bubbles
  TAU_JITTER: 0.30,                   // +- relative
  ATTACK_MIN: 0.0005, ATTACK_MAX: 0.002,
  RISE_PROB: 0.30,          // minority of voices glide upward ...
  RISE_MAX: 0.25,           // ... by up to 25 %
  RISE_TIME_TAUS: 2.5,      // glide reaches its end after this many tau

  // birth rate per zone: lambda = RATE_MAX * w_z * B^RATE_EXP (0 when B ~ 0)
  RATE_MAX: 90,
  RATE_EXP: 0.5,
  ZONE_RATE_W: [0.30, 1.0, 0.75],
  B_MIN: 0.0002,

  // loudness: many quiet events, occasional stronger low ones
  AMP_DB_RANGE: 34,         // event level spread below the maximum
  AMP_SKEW: 0.55,           // u^skew -> weighted toward quiet
  AMP_R_EXP: 0.45,          // larger bubbles louder: (r / 1 mm)^exp
  AMP_R_MAX: 1.8,
  BUBBLE_GAIN: 0.45,
  DENSITY_REF: 60,          // headroom: gain / sqrt(1 + rate / ref)

  // ---- flow bed (noise layers) ----
  BED_ON: 0,                // 0 = the noise wash is off (user, 2026-10-25: water only for now)
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
  BED_TOP_SQUASH: 0.95,     // headroom at full flow: level / (1 + k E^2)
  BED_R_ON: [0.45, 0.95],   // the wash only comes in with high Roughness (smoothstep over this range)          // level ~ E^exp
  MOD_DEPTH: [0.12, 0.45],  // gain modulation depth, low .. high roughness
  CUT_DEPTH: 0.25,          // cutoff modulation depth (fraction, log)
  FLUTTER_MIN_SEC: 0.04, FLUTTER_MAX_SEC: 0.3, // faster turbulent flutter (scaled by T)
  SCALE_TILT: 0.45,         // larger Scale lowers the bed's spectral centre

  // ---- eddies: slow swells in the rock wake (no audio of their own) ----
  EDDY_SLOTS: 3,            // 2-4 overlapping swells
  EDDY_DUR_SEC: [0.3, 3.0], // swell length (x Scale time multiplier)
  EDDY_GAP_SEC: [0.1, 1.5], // pause before a slot swells again
  EDDY_TIME_MUL: [0.6, 2.0],// Scale: eddy time multiplier
  EDDY_E: 0.35,             // wake energy boost at a full swell
  EDDY_RATE: 0.9,           // wake bubble-rate boost
  EDDY_RADIUS: 0.25,        // wake bubbles up to 25 % larger (lower) in a swell
  EDDY_DEPTH: [0.35, 1.0],  // swell depth, low .. high roughness

  // ---- small waterfalls at the drop: a soft plop, then bubbly settling ----
  FALLS_RATE: [0.2, 4.0],   // parent events per second as the drop's energy rises
  FALLS_RATE_EXP: 0.8,
  FALLS_FIZZ: [3, 7],       // impact = a quick fizz of small bubbles (no tonal plop) ...
  FALLS_FIZZ_MS: 20,        // ... within this time of the impact ...
  FALLS_FIZZ_RADIUS_MM: [0.3, 1.2], // ... small (x Scale) ...
  FALLS_FIZZ_GAIN: 0.5,     // ... at this level (x splash size x BUBBLE_GAIN) ...
  FALLS_FIZZ_TAU_MUL: 0.7,  // ... and a little shorter than creek bubbles
  FALLS_ATTACK_SEC: [0.002, 0.008],   // (used by the stone's plop)
  FALLS_CHILDREN: [2, 8],   // bubble children when Aeration permits ...
  FALLS_CHILD_MS: [10, 120],// ... spread over this window
  FALLS_CHILD_AMP: [0.25, 0.8],
  FALLS_CHILD_GAIN: 0.45,
  FALLS_SPLASH_J: 0.25,     // entrainment impulse into the drop zone
  FALLS_ENERGY_SEC: 0.3,    // overlapping splashes share a bounded energy budget
  FALLS_ENERGY_K: 1.5,
  FALLS_VOICES: 10,

  // ---- bubble cloud: low damped collective modes in dense bubbling ----
  // One mode each for the rock wake and the drop (brief: "one or two low, damped collective modes").
  // Every bubble born in the zone kicks its mode; strength fades in with the zone's bubble rate.
  CLOUD_RATE: [20, 110],    // zone bubble rate (per second, / ZONE_RATE_W): mode starts .. full
  CLOUD_HZ: 260,            // mode pitch at the start density, Scale 1x ...
  CLOUD_DENSE_DROP: 0.35,   // ... up to 35 % lower when fully dense (more air in the water)
  CLOUD_SCALE_EXP: 0.7,     // ... and lower with a bigger stream (Scale)
  CLOUD_HZ_RANGE: [40, 400],
  CLOUD_DRIFT: 0.12,        // slow random pitch wander (+-)
  CLOUD_Q: 4,               // heavily damped: a rumble, not a tone
  CLOUD_GAIN: 0.4,          // kick size (x the zone's bubble level)
  CLOUD_SPLASH: 1.5,          // a splash kicks the drop's mode this many times harder
  CLOUD_PAN: 0.5,           // zone pans x this (low sound: narrower)

  // ---- surface detail: sparse, quiet, short high ticks ----
  SURF_RATE: [2, 40],       // events per second, low .. high turbulence
  SURF_HZ: [2000, 9000],
  SURF_ATTACK_SEC: [0.001, 0.005],
  SURF_TAU_SEC: [0.002, 0.01],
  SURF_DB_RANGE: 30,        // weighted toward very low levels
  SURF_SKEW: 0.45,
  SURF_GAIN: 0.16,
  SURF_ZONE_W: [0.2, 0.3, 0.5], // more near the drop
  SURF_SPLASH: 1.5,         // splashes briefly raise surface activity
  SURF_BOOST_SEC: 0.4,
  SURF_VOICES: 10,

  // ---- throw a stone (P01): deep plop, bubble burst, a disturbance that settles ----
  STONE_PLOP_HZ: [70, 130], // impact tone (divided by the Scale radius multiplier)
  STONE_PLOP_GLIDE: -0.45,
  STONE_PLOP_TAU_SEC: [0.09, 0.18],
  STONE_PLOP_GAIN: 0.35,
  STONE_BUBBLES: [10, 24],  // burst size (by stone size) ...
  STONE_SPREAD_SEC: 0.35,   // ... spread over this time, getting quieter
  STONE_RADIUS_MM: [1.2, 7],// burst bubbles are larger than the creek's own (x Scale)
  STONE_BUBBLE_AMP: [0.18, 0.62],
  STONE_J: 0.45,            // entrainment impulse into every zone
  STONE_SURGE_SEC: 2.0,     // the disturbance settles over this time ...
  STONE_SURGE_RATE: 2.0,    // ... raising every event rate by up to (1 + 2) x, even in still water
  STONE_SURFACE: 2.0,       // ripples: surface ticks

  // ---- note mode: tuned drops and creek tuning ----
  DROP_GAIN: 0.55,          // drop level at full velocity (x BUBBLE_GAIN)
  DROP_TAU_MUL: 1.6,        // drops ring a little longer than creek bubbles
  DROP_RISE: 0.04,          // slight upward "plink"
  DROP_CHILDREN: 3,         // up to this many quieter follow-up bubbles ...
  DROP_CHILD_MS: [12, 90],  // ... after this delay ...
  DROP_CHILD_AMP: [0.15, 0.4], // ... at this level relative to the drop
  DROP_PAN: 0.35,           // drops spread +- this around the centre
  TUNE_ATTACK_SEC: 0.25,    // holding a note gathers / pours the bubbles onto it (default; P11 + S33 = Attack) ...
  TUNE_RELEASE_SEC: 0.6,    // ... and lets go after release (default; P11 + S34 = Release)
  ATTACK_RANGE_SEC: [0.004, 3.0],   // Attack knob (exponential), as TouchPulsar
  RELEASE_RANGE_SEC: [0.02, 4.0],   // Release knob (exponential), as TouchPulsar
  TUNE_MAX: 0.9,            // share of creek bubbles tuned when fully gathered
  TUNE_TAU_MUL: 1.3,        // tuned bubbles ring a bit longer
  TUNE_MIDI_RANGE: [28, 123], // tuned bubbles fold by octaves into E1 (41 Hz) .. 9.9 kHz
  VARIETY_STEPS: 7,         // Variety fully right: up to +- this many scale steps from the held note
  DECAY_OCT: 2,             // Decay knob: decay times x 2^(+-2) = 0.25x .. 4x, noon = 1x

  // ---- low-pass filter (as TouchPulsar: P11 + faders) ----
  FILTER_HZ: [30, 18000],   // cutoff range (exponential); fully up = open
  FILTER_Q: [0.707, 20],    // resonance: Q = 0.707 + r^2 * 19.3
  FILTER_SMOOTH_SEC: 0.005,

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
const CK_CLOUD_BIT = 0x20, CK_ALL_LAYERS = 0x3F;   // mask bit 5 = bubble cloud (rendered after the mixer, P09 in diagnostics)

if (typeof module !== 'undefined') module.exports = { CK, CK_ZONES, CK_LAYERS, CK_L_FLOW, CK_L_BUBBLES, CK_L_EDDIES, CK_L_FALLS, CK_L_SURFACE, CK_CLOUD_BIT, CK_ALL_LAYERS };
