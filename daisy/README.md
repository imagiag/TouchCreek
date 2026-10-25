# This is Creek

A sample-free procedural creek for the Synthux Touch 2 and Daisy Seed, from a quiet trickle to a lively shallow stream. Nothing is recorded: a continuous flow bed plus hundreds of tiny "bubbles" per second. Each bubble is a short decaying tone whose pitch comes from its size (f₀ ≈ 3 / r, after van den Doel 2005). A slowly changing shared "creek state" drives all of it, so surges feel connected without pulsing in step.

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
- S36 (left) **Flow** | Still → trickle → lively creek. More level, more bubbles, more splash activity. Fully down: the creek fades to silence within about 1.5 s.
- S37 (right) **Roughness** | Smooth channel → rocky interruptions: brighter wash, more movement in it, more turbulence (and so more bubbling).

### Knobs
- S31 **Scale** | Fine shallow trickle → larger stream body. Bubbles get 0.6× to 1.8× bigger, so their typical pitch falls (about 3.2 kHz → 1.3 kHz), and the wash gets darker and fuller.
- S32 **Aeration** | Sparse bubbling → bubbly white water. Fully left = no bubbles at all (only the flowing wash).
- S33 **Gain** | Output level. Off fully left, 0 dB at noon, +6 dB fully right.
- S34 **Width** | Mono fully left, natural stereo at noon, wider fully right. The low body of the creek is always centred, so it holds up in mono.
- S30, S35 | Not used yet.

### Switches
- **Left switch** (S09/S10) | Up or centre: **Performance**. Down: **Diagnostics** (the pads mute layers, see below).
- **Right switch** (S07/S08) | Down: **Hold** the slow creek state. The flow, turbulence and air in the water stop changing, so you can compare settings, while bubbles keep happening. Up or centre: live.

### Pads
**Performance**
- **P03** - Quiet trickle
- **P04** - Rocky creek
- **P05** - Small cascade
- **P06** - Deeper run

Presets glide over 250 ms. After a preset, the four sound knobs (Flow, Roughness, Scale, Aeration) are **picked up**: they do nothing until you move them a little, then they take over from where they are. So recalling a preset never jumps back to the knob positions.

**Diagnostics** (left switch down)
- **P03** - Flow bed on/off
- **P04** - Bubbles on/off
- **P05** - Eddies on/off *(arrives with Phase 2)*
- **P06** - Small waterfalls on/off *(Phase 2)*
- **P07** - Surface detail on/off *(Phase 2)*
- **P08** - All layers on

Muting is smooth (20 ms) and doesn't change what the other layers do. Back in Performance, all layers always play; the diagnostic mutes are remembered for next time.

### LED
- 1–4 blinks: preset number
- 2 blinks: layer on; 1 blink: layer muted; 3 quick blinks: all layers on
- Otherwise it flickers with the louder bubbles (in Diagnostics it stays lit and dips on bubbles)

## Presets

| Pad | Preset | Flow | Roughness | Scale | Aeration |
|---|---|---|---|---|---|
| P03 | Quiet trickle | 0.20 | 0.25 | 0.20 | 0.30 |
| P04 | Rocky creek | 0.50 | 0.60 | 0.45 | 0.55 |
| P05 | Small cascade | 0.75 | 0.80 | 0.50 | 0.85 |
| P06 | Deeper run | 0.65 | 0.35 | 0.80 | 0.40 |

At power-up the creek plays whatever the knobs and faders are set to.

## What you hear (Phase 1)
- **Flow bed**: a continuous wash of filtered noise in three overlapping bands (a centred body, a mid band and a soft sheen), slowly breathing with the creek state; faster flutter as turbulence rises.
- **Bubbles**: up to 24 at once, mostly small and quiet, now and then a larger, lower and louder one. About a third of them rise slightly in pitch as they fade. They gather in the rock wake (slightly left) and the drop (slightly right), fewer in the open flow (centre).
- The creek state has three places, flow (centre), rock wake (left) and drop (right), each with its own energy, turbulence and air. They follow the macros with some inertia (0.1–0.35 s) and drift on their own.

Coming in Phase 2: eddies (slow swells), small waterfalls (soft splashes followed by bubbly settling) and surface detail (fine flecks), all sharing the same state.

## Browser emulator and offline renders
- **Emulator**: `python3 web/serve.py 8766`, then open http://localhost:8766. Same engine (JS twin of the firmware engine), same panel, same pickup and LED. "Render 30 s WAV" saves what you're hearing.
- **Reference renders**: `make -C daisy/test renders` renders every scenario in `scenarios/` to `renders/` with the C++ engine (24-bit WAV plus a .json with seed, settings, automation, version and stats). Identical every time.
- **Tests**: `make -C daisy/test` runs the acceptance checks and compares the C++ engine with the JS one.

## Building the firmware
Needs the ARM GNU toolchain (13.3) on the PATH.
```bash
git submodule update --init --recursive
make -C daisy/lib/libDaisy
make -C daisy
```
`DEBUG=1 make -C daisy` prints CPU load and voice statistics once a second over USB serial.
