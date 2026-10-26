# This is Creek

A sample-free procedural creek for the Synthux Touch synthesizer and Daisy Seed, from a quiet trickle to a lively shallow stream. Nothing is recorded: hundreds of tiny bubbles per second, slow eddies, small splashes and fine surface ticks, all driven by one slowly changing creek state. Switch to Notes and play it: tap a pad for a tuned drop, hold it and the water pours onto the note.

Each bubble is a short decaying tone whose pitch comes from its size, f₀ ≈ 3 / r (Kees van den Doel, *Physically-based Models for Liquid Sounds*, 2005): small bubbles are high, big ones low.

The full manual with tables is [manual.html](manual.html).

## Quick Install
Download the newest file from [releases/](releases/) (see its README for what each version changes) and flash it with the [Daisy web programmer](https://flash.daisy.audio/) in Chrome or Edge:
- **Unplugged**: hold BOOT while plugging in USB
- **Already powered**: hold BOOT, tap RESET, release BOOT

Then **Connect** → *DFU in FS Mode* → choose the file → **Program**. To go back, flash any Synthux firmware the same way.

USB is **MIDI**; for measuring CPU there's a separate monitor build (see Emulator & Monitor), not part of the releases. Every version is kept in `releases/` with a name saying what changed, so you can always flash an older one. `build/CreekTouch.bin` is a working build and may be newer than any release.

## Controls

<img src="touch.jpeg" width="300"/>

At power-up the creek plays whatever the knobs and faders are set to; the second-layer controls start at their defaults (natural width, default tone, attack and release, filter open). The **left switch** decides what the pads do.

### Pads
- **P01** - **Throw a stone**, in every mode: a deep falling plop, a burst of larger bubbles spreading from the centre, and a disturbance that settles over 2–3 s (hit harder = bigger stone)
- **Presets** (left switch up)
  - **P03** - Quiet trickle | **P04** - Rocky creek | **P05** - Small cascade | **P06** - Deeper run (250 ms glide)
- **Notes** (left switch centre)
  - **P03...P09** - Notes of the scale (from C5, C major pentatonic by default). **Tap** = a tuned drop with a short bubbly tail (hit harder = louder). **Hold** = the water pours onto the note (up to 4 = a chord)
  - **P00** - Octave down (to −1; in Chromatic −7 semitones, to −14)
  - **P02** - Octave up (to +2; in Chromatic +7 semitones, to +28)
- **Diagnostics** (left switch down)
  - **P03** - Flow bed on/off (the noise wash is off for now: silent) | **P04** - Bubbles | **P05** - Eddies | **P06** - Small waterfalls | **P07** - Surface detail
  - **P08** - All layers on
  - (monitor build only: **P09** - stress test, worst-case settings and note bursts; LED blinks fast)
- **P10** - Modifier:
  - Hold P10 + S30 - **Width** (see Knobs)
  - Notes: hold P10 + P03...P09 - set the **root key** to that pad's current note (the scale type stays)
- **P11** - Modifier:
  - Hold P11 + S31 - **Tone**: dark ↔ bright (noon = default)
  - Hold P11 + S33 - **Attack**: how quickly a held note's pour swells in, 4 ms–3 s (default 0.25 s)
  - Hold P11 + S34 - **Release**: how long the pour keeps going after you let go, 20 ms–4 s (default 0.6 s)
  - Hold P11 + right fader (S37) - **Filter cutoff** (30 Hz–18 kHz; fully up = open)
  - Hold P11 + left fader (S36) - **Filter resonance**
  - Notes: P11 + P00 / P02 - Previous / next scale (cycles through all 7 scales)
  - Notes: P11 + P03...P09 - Direct scale select (P03: Chromatic, P04: Major, P05: Natural Minor, P06: Minor Pentatonic, P07: Major Pentatonic, P08: Dorian, P09: Harmonic Minor)

Diagnostic mutes are smooth (20 ms), don't change what the other layers do, and are remembered; in Presets and Notes all layers always play.

### Knobs (clockwise)
- S30 **Free flow** (Notes) | How much the creek plays by itself. Fully left = still water, only your notes sound. Presets and Diagnostics always flow | Hold P10: **Width** (mono fully left, natural at noon, wider fully right)
- S31 **Scale** | Fine shallow trickle → larger stream body. Bubbles get 0.6× to 1.8× bigger over most of the knob (typical pitch about 3 kHz → 1.3 kHz); the last quarter bends up to 8× for bass, the deepest bubbles around 60 Hz ringing longer. Splashes get lower, eddies get slower | Hold P11: **Tone** - dark ↔ bright, wide: noon = default mix; turning left brings in more and more of the big, low bubbles and, near the end, makes them all up to an octave lower; right does the same towards small, high ones (at mid Scale the average pitch goes from about 350 Hz to 7.5 kHz)
- S32 **Aeration** | Sparse bubbling → bubbly white water. Fully left = no bubbles at all
- S33 **Flow** | Still → trickle → lively creek. More bubbles and splashes (0.2 → 4 per second). Fully left: silence within about 1.5 s | Hold P11: **Attack** (4 ms–3 s)
- S34 **Decay** | How long every bubble, drop, splash and tick rings. Noon = default; left shorter (down to ¼), right longer (up to 4×) | Hold P11: **Release** (20 ms–4 s; the pour's tail, still on the notes you played)
- S35 **Variety** (Notes) | Noon = default (the held note, in the octave nearest each bubble's own size). Left: more and more exactly the held note (fully left = one note). Right: more and more other notes of the active scale, up to an octave around the held note

### Faders
- S36 (left) **Level** | Output volume: silent at the bottom, 0 dB at about 70 %, +6 dB at the top (Hold P11: **Resonance**, Q 0.7–20)
- S37 (right) **Roughness** | Smooth channel → rocky interruptions: more turbulence, so more bubbling, deeper eddies, more uneven splashes and more surface ticks (Hold P11: **Cutoff**, 30 Hz–18 kHz, fully up = open)

### Switches
- **Left Switch — Mode**:
  - **Up**: PRESETS (P03–P06 recall the four presets)
  - **Center**: NOTES (P03–P09 play the scale; Free flow and Variety apply)
  - **Down**: DIAGNOSTICS (P03–P07 mute layers, P08 all on)
- **Right Switch — Latch / Hold**:
  - **Up / Center**: live
  - **Down in Notes**: **Latch** - notes you touch keep pouring after you let go (up to 4); tap a latched note again to release it; switching Latch off releases everything you're not touching
  - **Down in Presets / Diagnostics**: **Hold** the slow creek state - flow, turbulence and air stop changing so you can compare settings; bubbles keep happening

## Features
Everything you hear is made of short decaying tones; there is no noise in the sound for now.

- **Bubbles**: The main voice. Up to 24 at once, mostly small and quiet, now and then a larger, lower and louder one. About a third rise slightly in pitch as they fade. Their size sets their pitch, never a fixed note list, so the creek doesn't turn into a melody unless you ask it to.
- **Three Places, One Creek**: The open flow (centre), the wake behind a rock (slightly left) and a small drop (slightly right), each with its own energy, turbulence and trapped air. They follow the knobs with a little inertia (0.1–0.35 s) and drift on their own; they share causes, not timing, so surges feel connected without everything pulsing at once.
- **Eddies**: Two to four overlapping slow swells (0.3–3 s; slower with larger Scale, deeper with Roughness) in the rock wake. No sound of their own: each makes the left side surge with more, slightly bigger and lower bubbles, then settle.
- **Small Waterfalls**: At the drop, 0.2–4 times a second as Flow rises: a quick fizz of 3–7 tiny bubbles (2.5–10 kHz) within 20 ms, then 2–8 bubbles settling over the next tenth of a second (more with Aeration), plus a short burst of extra air. (Before v16 this started with a low falling plop, which sounded artificial.) Overlapping splashes share a limited energy, so they never swamp the creek.
- **Surface Detail**: Sparse, very short, quiet high ticks (2–9 kHz), more near the drop, at turbulence peaks and right after splashes, in loose bursts rather than a steady crackle. In Notes mode the ticks and the falls' fizz are tuned to the held notes (in their own high octaves; Variety spreads them over the scale), and stay on the last played notes after you let go, until you play new ones.
- **Attack & Release**: Both on P11 (P11 + S33, P11 + S34; ranges and CCs as TouchPulsar): how quickly a held note's pour swells in (4 ms – 3 s) and how long it keeps pouring and settling after you let go (20 ms – 4 s).
- **Tone**: P11 + S31 shifts the creek darker or brighter over a wide range (noon = default): first the mix (more big or more small bubbles), then near the ends all bubbles move up to an octave.
- **Drops & Pouring**: Tapping a note drops one tuned bubble with 1–3 quieter followers; holding it pours the water onto the note in the rhythm of Flow, Roughness and Aeration. With **Free flow** down the creek is still and only your notes sound; **Variety** decides whether the pour sticks to one note or spreads across the scale; **Latch** keeps notes pouring hands-free.
- **Chords**: Hold up to **4 notes** at once (pads and MIDI keys together) and the water pours onto all of them: each bubble lands on whichever held note is nearest its own pitch, so a chord shimmers as one cloud. Holding a 5th note releases the oldest one (it settles with Release). With Latch, tapped notes add up to a 4-note chord you can leave pouring.
- **Throw a Stone**: P01 (or MIDI CC 29): a deep plop that falls in pitch (lower with larger Scale), a burst of larger bubbles spreading out from the centre over a third of a second, then a disturbance that settles over 2–3 s - air in every part of the creek, more bubbling and splashing, ripples of surface ticks. In still water it stirs the water up and lets it go still again.
- **Surge**: MIDI mod wheel or aftertouch makes the water rush (Flow, Roughness and a little Aeration rise on top of the knobs), a crescendo you can play.
- **Filter**: Hold P11 and move the faders: right = cutoff, left = resonance. A smooth 12 dB/oct low-pass on both channels (the same one as TouchPulsar), before the output limiter. At power-up it's fully open.
- **Output**: A DC blocker and a linked stereo limiter keep peaks below −1 dBFS; at 0 dB level the presets peak around −5 to −7 dBFS, so the limiter only catches rare overlaps.
- **Soft Takeover**: After a preset, after a MIDI CC, or when you press or release P10 / P11, a knob only takes control once you move it, so values never jump.

### Scales
Hold **P11** and tap a note pad to jump directly to a scale, or use **P00 / P02** to cycle. Hold **P10** and tap a note pad to move the root (shown here in C, octave 0). The same seven scales as TouchPulsar.
- **P03 — Chromatic**: All semitones; P00/P02 shift by 7 (`C5, C#5, D5, D#5, E5, F5, F#5`)
- **P04 — Major**: Diatonic major (`C5, D5, E5, F5, G5, A5, B5`)
- **P05 — Natural Minor**: Diatonic minor (`C5, D5, Eb5, F5, G5, Ab5, Bb5`)
- **P06 — Minor Pentatonic**: Blues & folk (`C5, Eb5, F5, G5, Bb5, C6, Eb6`)
- **P07 — Major Pentatonic** (default): Bright & open (`C5, D5, E5, G5, A5, C6, D6`)
- **P08 — Dorian**: Minor with a raised sixth (`C5, D5, Eb5, F5, G5, A5, Bb5`)
- **P09 — Harmonic Minor**: Minor with a raised seventh (`C5, D5, Eb5, F5, G5, Ab5, B5`)

### Presets
| Pad | Preset | Flow | Roughness | Scale | Aeration |
|---|---|---|---|---|---|
| P03 | Quiet trickle | 0.20 | 0.25 | 0.20 | 0.30 |
| P04 | Rocky creek | 0.50 | 0.60 | 0.45 | 0.55 |
| P05 | Small cascade | 0.75 | 0.80 | 0.50 | 0.85 |
| P06 | Deeper run | 0.65 | 0.35 | 0.80 | 0.40 |

Presets set the four sound knobs; everything else (Level, Width, Tone, Decay, Attack, Release, Free flow, Variety, filter) stays where it is.

### LED Indicator
The onboard LED provides visual feedback:
- **Bubbles**: Flickers with notes and the louder bubbles (in Diagnostics it stays lit and dips on them).
- **Mode**: 1 blink = Presets, 2 = Notes, 3 = Diagnostics when the left switch moves.
- **Preset**: 1–4 blinks for the preset number.
- **Scale Confirmation**: Blinks N times for scale 1 through 7 (Chromatic = 1 blink, ..., Harmonic Minor = 7 blinks).
- **Octave Shift**: Quick blinks: 1 at octave 0, 2 at ±1, 3 at +2.
- **Root Key**: 1 long blink.
- **Stone**: a longer flash when a stone is thrown.
- **Diagnostics**: 2 blinks = layer on, 1 blink = layer muted, 3 quick blinks = all on (monitor build: stress test = 4 quick blinks, then fast blinking while it runs).

### MIDI
Creek appears as a USB MIDI device (listed as **Creek**) when connected via the Daisy Seed's USB port to a computer, DAW, iPad or USB-MIDI host. A keyboard plugged straight into the Touch won't work (both are USB devices), so route it through the computer or a host box. It listens on all MIDI channels.

- **Notes**: Play like the note pads, in every mode: a tuned drop on press, the water pours onto the note while held. Fully chromatic, separate from the pad scale. Up to 4 held notes (pads and MIDI together). In Notes mode, Free flow and Latch apply.
- **Velocity**: Drop level. **Channel aftertouch**: Surge.
- **Pitch Bend**: ±2 semitones.
- **Sustain** (CC 64): Holds released notes.
- **Mod Wheel** (CC 1): **Surge** - the water rushes; back at zero it changes nothing.
- **Program Change** 1–4: The four presets.

| CC | Control |
|---|---|
| 1 | Mod wheel → **Surge** |
| 7 | S36 **Level** |
| 14 | **Scale** select (0–127 across the 7 scales) |
| 15 | **Root** (0–127 across C … B) |
| 20 | S33 **Flow** |
| 21 | S37 **Roughness** |
| 22 | S31 **Scale** (bubble size) |
| 23 | S32 **Aeration** |
| 24 | S30 **Free flow** |
| 25 | S35 **Variety** |
| 26 | S34 **Decay** |
| 27 | **Width** (P10 + S30) |
| 28 | **Tone** (P11 + S31) |
| 72 | **Release** (P11 + S34) |
| 107 | **Attack** (P11 + S33) |
| 29 | **Throw a stone** (P01; value = size) |
| 64 | **Sustain** |
| 102 | **Filter Cutoff** (P11 + S37) |
| 103 | **Filter Resonance** (P11 + S36) |
| 120, 123 | All Notes Off |

**Soft Takeover**: A value set via MIDI CC is kept until the corresponding knob or fader is moved; from then on the physical control takes over again.

### Emulator & Monitor
- **Browser emulator**: `python3 web/serve.py 8766`, then open http://localhost:8766. The same panel and the same engine (a JavaScript twin, tested against the firmware's), with pickup, layers and LED behaving as on the hardware. **Enable MIDI** plays it from a keyboard on your computer; **Render 30 s WAV** saves what you hear.
- **Monitor** (also for debugging): the emulator's **Monitor** button opens it for the emulator; for the Touch, build it with `make monitor` in `daisy/` (USB sends the report instead of MIDI, and Diagnostics P09 becomes a stress test), flash `build-monitor/CreekTouch-monitor.bin`, open http://localhost:8766/monitor.html in Chrome or Edge, plug in the Touch and click **Connect**. It shows which layers are sounding and how much (bubble voices and rate, played drops / stones, eddies, splash plops, surface ticks, mutes, held notes), the **last pressed** pad / knob / switch with its meaning, CPU load (average and worst 1 ms block), stack use, dropped events and limiter activity. For the worst case use Diagnostics + P09 (stress test); measured on the Touch it stays under 25 % (limit 70 %).

## Project Structure
```
creek/
├── daisy/
│   ├── CreekTouch.cpp   # Main application entry point
│   ├── Makefile         # Build configuration (make monitor = monitor build)
│   ├── creek/           # Instrument core: creek engine + controls (platform-free)
│   ├── touch/           # Touch wrapper (pads, knobs, switches) from TouchString
│   ├── ui/              # UI connecting instrument core with touch wrapper, MIDI, LED
│   ├── test/            # Host tests, soak test, offline renderer, C++ vs browser comparison
│   └── releases/        # Firmware to flash
├── web/                 # Browser emulator and monitor; engine/ = the JavaScript twin
├── scenarios/           # Seeds and automation for the reference renders
└── tools/               # gen-config.js: one config file for both engines
```

## Project Setup
Needs the [Daisy toolchain](https://daisy.audio/tutorials/cpp-dev-env/). libDaisy is a git submodule in `daisy/lib/libDaisy` (Synthux's fork, pinned); DaisySP isn't needed. Clone with submodules, then build from `daisy/`:
```
$ git clone --recurse-submodules <repository URL> creek
$ cd creek/daisy
$ make libs -j8
$ make clean; make -j8
$ make program-dfu
```
- `make monitor` builds `build-monitor/CreekTouch-monitor.bin` (only when needed): USB sends the CPU report instead of MIDI, and Diagnostics P09 runs a stress test.
- `make -C test` checks the engine on your computer and compares its sound with the browser engine in `../web`; `make -C test soak` runs a 30-minute stability test; `make -C test renders` writes reference WAVs of every scenario.
- All tuning constants live in `web/engine/creek-config.js`; `node tools/gen-config.js` writes the C++ copy.

## Credits
- Bubble model: Kees van den Doel, *Physically-based Models for Liquid Sounds*, ACM Transactions on Applied Perception, vol. 2, no. 4, 2005.
- Sound design method: Andy Farnell, *Designing Sound*, MIT Press, 2010.
- Touch drivers: Synthux Academy [TouchString](https://github.com/Synthux-Academy/TouchString). Hardware: [Touch 2](https://www.synthux.academy/store/simple-touch-2) by [Synthux Academy](https://www.synthux.academy/).
- Filter and scale handling shared with TouchPulsar.
