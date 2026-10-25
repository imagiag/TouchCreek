# This is Creek

A sample-free procedural creek for the Synthux Touch 2 and Daisy Seed, from a quiet trickle to a lively shallow stream. Nothing is recorded: up to a few hundred tiny "bubbles" per second, slow eddies, small splashes and fine surface ticks. Each bubble is a short decaying tone whose pitch comes from its size (f₀ ≈ 3 / r, after van den Doel 2005). A slowly changing shared "creek state" drives all of it, so surges feel connected without pulsing in step.

## Quick Install
Download the newest file from [releases/](releases/) (see its README for what each version changes) and flash it with the [Daisy web programmer](https://flash.daisy.audio/) in Chrome or Edge:
- **Unplugged**: hold BOOT while plugging in USB
- **Already powered**: hold BOOT, tap RESET, release BOOT

Then **Connect** → *DFU in FS Mode* → choose the file → **Program**. To go back, flash any other firmware the same way.

`build/CreekTouch.bin` is only a working build and may be newer than any release: flash from `releases/`.

## Controls

<img src="touch.jpeg" width="300"/>

The browser emulator (`web/`, see below) has the same layout and behaves like the hardware, including soft pickup and the LED.

### Faders
- S36 (left) **Level** | Output volume, as on TouchPulsar: silent at the bottom, 0 dB at about 70 %, +6 dB at the top.
- S37 (right) **Roughness** | Smooth channel → rocky interruptions: more turbulence, so more bubbling, deeper eddies, more uneven splashes and more surface ticks.

### Knobs
- S31 **Scale** | Fine shallow trickle → larger stream body. Bubbles get 0.6× to 1.8× bigger over most of the knob, so their typical pitch falls (about 3 kHz → 1.3 kHz); in the last quarter it bends up to 4× for bass, with the deepest bubbles around 125 Hz ringing longer. Splashes plop lower and eddies get slower.
- S32 **Aeration** | Sparse bubbling → bubbly white water. Fully left = no bubbles at all.
- S33 **Flow** | Still → trickle → lively creek. More bubbles, more splashes at the drop (0.2 → 4 per second). Fully left: the creek fades to silence within about 1.5 s.
- S34 **Decay** | How long every bubble, drop, plop and tick rings. Noon = default; left shorter (down to ¼), right longer (up to 4×).
- P10 + S34 **Width** | Mono fully left, natural stereo at noon, wider fully right. Hold P10 and turn S34; it picks up once moved.
- S30 **Free flow** (Notes) | How much the creek plays by itself. Fully left = still water: only your notes sound. In Presets and Diagnostics the creek always flows.
- S35 **Variety** (Notes) | How the tuned bubbles spread. Noon = default (the held note, in the octave nearest each bubble's own size). Left: more and more exactly the held note; fully left = one note. Right: more and more other notes of the active scale, up to an octave around the held note.

### Switches
- **Left switch** (S09/S10) | Up: **Presets**. Centre: **Notes**. Down: **Diagnostics**. It sets what the pads do.
- **Right switch** (S07/S08) | In Notes, down = **Latch**: notes you touch keep pouring after you let go (up to 4, a chord); tap a latched note again to release it; switching Latch off releases everything you're not touching. In Presets and Diagnostics, down = **Hold** the slow creek state. The flow, turbulence and air in the water stop changing, so you can compare settings, while bubbles keep happening. Up or centre: live.

### Pads
**Presets** (left switch up)
- **P03** - Quiet trickle
- **P04** - Rocky creek
- **P05** - Small cascade
- **P06** - Deeper run

Presets glide over 250 ms. After a preset, the four sound knobs (Flow, Roughness, Scale, Aeration) are **picked up**: they do nothing until you move them a little, then they take over from where they are. So recalling a preset never jumps back to the knob positions.

**Notes** (left switch centre) - play the creek as an instrument; the creek keeps running underneath
- **P03...P09** - Notes of the scale (default C major pentatonic, from C5)
  - **Tap**: a tuned **drop**, a bubble at that note with a short bubbly tail (1–3 quieter bubbles at the same note or an octave up). Hit harder for a louder drop.
  - **Hold**: the water **pours** onto the held note: bubbles in the rhythm set by Flow, Roughness and Aeration, pitched by Variety, rising in about a quarter second and settling about half a second after you let go. Hold several pads (up to 4) for a chord. With Free flow up, the creek's own bubbles gather onto the held notes too.
- **P00** - Octave down | **P02** - Octave up (−1 to +2 octaves; in Chromatic, pages of 7 semitones)
- **P11 + P00 / P02** - Previous / next scale | **P11 + P03...P09** - Choose a scale: Chromatic, Major, Natural Minor, Minor Pentatonic, Major Pentatonic, Dorian, Harmonic Minor (the same seven as TouchPulsar)
- **P10 + P03...P09** - Set the root key to that pad's note

**Diagnostics** (left switch down)
- **P03** - Flow bed on/off *(the noise wash is switched off for now, so this is silent)*
- **P04** - Bubbles on/off
- **P05** - Eddies on/off (they have no sound of their own: muting them stops the wake from surging)
- **P06** - Small waterfalls on/off (the plops; their bubbly settling belongs to the bubbles)
- **P07** - Surface detail on/off
- **P08** - All layers on
- **P09** - **Stress test** on/off: worst-case settings with sudden jumps and loud note bursts (LED blinks fast). For measuring the CPU load with the monitor (below). Leaving Diagnostics stops it.

Muting is smooth (20 ms) and doesn't change what the other layers do. In Presets and Notes all layers always play; the diagnostic mutes are remembered for next time.

### LED
- Switching mode: 1 blink = Presets, 2 = Notes, 3 = Diagnostics
- 1–4 blinks: preset number; 1–7 quick blinks: scale number; octave changes blink the octave; 1 long blink: root set
- 2 blinks: layer on; 1 blink: layer muted; 3 quick blinks: all layers on; fast blinking: stress test running
- Otherwise it flickers with notes and the louder bubbles (in Diagnostics it stays lit and dips)

## Presets

| Pad | Preset | Flow | Roughness | Scale | Aeration |
|---|---|---|---|---|---|
| P03 | Quiet trickle | 0.20 | 0.25 | 0.20 | 0.30 |
| P04 | Rocky creek | 0.50 | 0.60 | 0.45 | 0.55 |
| P05 | Small cascade | 0.75 | 0.80 | 0.50 | 0.85 |
| P06 | Deeper run | 0.65 | 0.35 | 0.80 | 0.40 |

At power-up the creek plays whatever the knobs and faders are set to.

## What you hear
Everything is made of short decaying tones; there is no noise in the sound for now.
- **Bubbles**: the main voice. Up to 24 at once, mostly small and quiet, now and then a larger, lower and louder one. About a third rise slightly in pitch as they fade. They gather in the rock wake (slightly left) and the drop (slightly right), fewer in the open flow (centre).
- **Eddies**: two to four overlapping slow swells (0.3–3 s, slower with larger Scale) in the rock wake. Each one gathers energy there: more bubbles, slightly bigger and lower ones, then lets go. You hear them as the left side of the creek surging and settling.
- **Small waterfalls**: at the drop (right), 0.2–4 times a second as Flow rises: a soft low plop that falls in pitch, then 2–8 bubbles settling over the next tenth of a second (more with Aeration), plus a short burst of extra air in the drop. Overlapping splashes share a limited energy, so they never swamp the creek.
- **Surface detail**: sparse, very short and quiet high ticks (2–9 kHz), more of them near the drop, at turbulence peaks and right after splashes, in loose bursts rather than a steady crackle.
- **Shared state**: the three places (flow, rock wake, drop) each have their own energy, turbulence and air. They follow the knobs with some inertia (0.1–0.35 s) and drift on their own, so surges feel connected without everything pulsing at once.
- The noise **wash** (flow bed) is in the code but switched off for now.

## Browser emulator and offline renders
- **Emulator**: `python3 web/serve.py 8766`, then open http://localhost:8766. Same engine (JS twin of the firmware engine), same panel, same pickup and LED. "Render 30 s WAV" saves what you're hearing.
- **Monitor**: open http://localhost:8766/monitor.html (Chrome or Edge) with the Touch 2 plugged in by USB and click Connect. The firmware reports CPU load (average and worst 1 ms block), voices, bubbles per second, stack use, dropped events and limiter activity once a second. Use Diagnostics + P09 for the worst case.
- **Manual (HTML)**: [manual.html](manual.html). **Validation report**: [../REPORT.md](../REPORT.md).
- **Reference renders**: `make -C daisy/test renders` renders every scenario in `scenarios/` to `renders/` with the C++ engine (24-bit WAV plus a .json with seed, settings, automation, version and stats). Identical every time.
- **Tests**: `make -C daisy/test` runs the acceptance checks and compares the C++ engine with the JS one; `make -C daisy/test soak` runs the 30-minute stability test.

## Building the firmware
Needs the ARM GNU toolchain (13.3) on the PATH.
```bash
git submodule update --init --recursive
make -C daisy/lib/libDaisy
make -C daisy
```
The firmware always reports CPU load and voice statistics once a second over USB serial (115200 baud, lines starting with `CREEK`); see the monitor above.
