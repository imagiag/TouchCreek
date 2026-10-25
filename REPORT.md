# Creek — validation and handoff report

Status on 2026-10-25, firmware `CreekTouch-v5-monitor-fix.bin`. Everything measured offline is below; items marked **hardware** need the Touch 2 and the [monitor page](web/monitor.html) (see "How to finish the hardware checks").

## Deliverables (brief, "Required handoff")

| Item | Where |
|---|---|
| Source code | `daisy/creek/` (portable C++ engine, no hardware code), `daisy/ui/` + `daisy/CreekTouch.cpp` (Touch 2 glue), `web/engine/` (JS twin used by the emulator) |
| One configuration file | `web/engine/creek-config.js` → `node tools/gen-config.js` writes `daisy/creek/config.h` |
| Desktop build and render instructions | `daisy/README.md` → "Browser emulator and offline renders"; `make -C daisy/test`, `make -C daisy/test renders`, `make -C daisy/test soak` |
| Touch 2 build and flash instructions | `daisy/README.md` → "Quick Install" and "Building the firmware"; libDaisy pinned (Synthux fork `5c86d21`), ARM GNU 13.3 |
| Four presets | `daisy/README.md` → Presets (also `creek-controls.js` / `controls.cpp`) |
| Isolated layer renders | `renders/solo_bubbles_*.wav`, `solo_falls_cascade.wav`, `solo_surface_cascade.wav`, `rocky_no_eddies.wav` vs `rocky.wav` (eddies have no audio of their own) |
| 60-second stereo demonstration | `renders/demo_60s.wav` (scenario `scenarios/demo_60s.txt`, seed 7) |
| Performance and listening report | this file |

Renders are regenerated with `make -C daisy/test renders` (not in git, ~160 MB); each WAV has a `.json` with seed, preset, sample rate, block size, build version, full automation, engine statistics and signal analysis.

## Departures from the brief (decided with the user)

- **No noise.** The flow-noise wash is switched off (`BED_ON = 0`, user's request; "we'll figure that part out later"). So Phase 2 layers are noise-free: eddies modulate the rock wake instead of being noise swells; the waterfall impact is a soft falling low tone instead of a noise burst; surface detail is very short high ticks. The noise wash code is kept and can be turned back on.
- **Panel.** Left fader = Level (as TouchPulsar), Flow on S33, Roughness on the right fader, S31 Scale, S32 Aeration, S34 Width. Left switch: Presets / Notes / Diagnostics; right switch: Hold.
- **Notes mode** (not in the brief): tap = tuned drop, hold = the creek's bubbles gather onto the held notes.
- **Desktop audition host** = browser emulator (JS twin of the engine) plus the C++ offline renderer; `compare.js` checks the two engines against each other.

## Acceptance criteria

| Area | Pass condition (brief) | Result |
|---|---|---|
| Recognition | ≥ 4 of 5 listeners call the default 30 s render running water / a creek | **Listener test pending.** Play `renders/trickle.wav` and `renders/rocky.wav` unprompted ("what is this sound?") and note answers below. |
| Layer contribution | Five solo renders plus full mix; each layer has an audible role; bubbles not musical notes; surface not dominant | Four active layers (wash off). Rocky creek: bubbles −27.6 dBFS rms (main voice), splashes −47 dBFS rms (sparse, peaks −19.5 dBFS), surface ticks −54 dBFS rms (27 dB under the bubbles; peaks −24 dBFS), eddies change the wake (60 → 72 bubbles/s with eddies on). Bubble radii are drawn continuously (0.24–10.8 mm across Scale), never quantised; only Notes mode tunes them, on purpose. |
| Control behaviour | Macro sweeps smooth and audible; Flow 0 → silence within 3 s; Scale lowers bubble pitch; Aeration raises event count | Flow 0: below −80 dBFS after **1.4 s**. Scale sweep: mean bubble pitch **3018 → 2318 → 1700 → 1246 Hz** per quarter. Aeration sweep: **32 → 61 → 67 → 89** bubbles/s. Flow sweep: **14 → 49 → 76 → 117** bubbles/s. Radius → pitch exact: 0.5/1/2/4/6 mm → 6000/3000/1500/750/500 Hz. Aeration 0: no bubbles. |
| Stability | 30 min at max activity + corner presets + rapid changes: no NaN/Inf, clicks, stuck voices, underruns, sustained limiter pumping | **Offline: pass** (`make -C daisy/test soak`, 30 min, 408× real time): 0 NaN/Inf, 0 sample jumps > 0.6, no sound 3.5 s after Flow 0, limiter active in 0.00 % of blocks at maximum settings and 0 dB level, 0 queue drops. **Hardware:** run the stress test (Diagnostics + P09) for 30 min with the monitor open. |
| Timing and memory | Worst callback < 70 % of the 1 ms deadline in the stress run; report memory map, stack margin, voice saturation, queue drops | **Hardware: pending** (monitor shows CPU worst block, stack, voices, drops). Memory map: flash 121 144 B of 128 KB (92.4 %), SRAM 42 432 B, RAM_D2 16 704 B, DTCM = stack only (128 KB). Engine working set 13.4 KiB (limit 256 KiB). Voice saturation (offline): 24 voices reached in Rocky creek and above; under the stress test ~150 steals/s (quietest voices, 2 ms fades), 0 queue drops. |
| Signal quality | No hard clipping; protected peaks ≤ −1 dBFS; DC < 0.001 per channel over 10 s; mono keeps the body | Presets peak −5.2 to −7.0 dBFS; worst corner (everything at maximum) −5.8 dBFS before protection (nominal < −3 dBFS). Protected peak ≤ −1.0 dBFS by construction, −1.66 dBFS in the soak. DC worst 1.5 × 10⁻⁵. Mono / stereo rms 0.97. |
| Reproducibility | Same seed, build, rate and automation → identical offline output; reset and block partitions tested; float differences documented | C++: **bit-identical** across runs, `Reset(seed)`, and block sizes 1, 37, 48, 1000. JS twin vs C++: correlation ≥ 0.994 over the first 10 s, equal level; in long runs a rare 1-ulp float/double difference moves a single bubble by one sample, after which later random draws reshuffle (documented float tolerance). |

## Performance (offline, this Mac)

- C++ engine: ~400× real time single-threaded; worst 48-frame block 55 µs (desktop, includes OS jitter).
- Firmware: 48 kHz, 48-frame blocks, control state at 250 Hz, voice glides at 3 kHz, no heap, no locks in the audio callback; macros arrive through a double-buffered mailbox, note drops through a 16-entry lock-free queue (drops counted, never blocks).

## How to finish the hardware checks

1. Flash `daisy/releases/CreekTouch-v5-monitor-fix.bin`.
2. Run `python3 web/serve.py 8766`, open http://localhost:8766/monitor.html in Chrome, plug the Touch 2 in, click **Connect**.
3. Play the four presets and Notes for a minute, then left switch down → **P09** (stress test) and let it run 30 minutes.
4. Copy the "CPU worst block", "Stack used", "Voices" peak and "Dropped events" into the table below (or paste the copied log into the conversation).

| Hardware measurement | Value |
|---|---|
| CPU average, presets | 13–16 % (v4, first reading, Rocky-like settings) |
| CPU worst block, stress test (limit 70 %) | 23.9 % worst so far in normal play (v4); stress test pending |
| Stack used | |
| Peak voices / dropped events | |
| 30 min stress: clicks, dropouts, stuck sound? | |

## Listening notes

| Listener | Unprompted answer (trickle) | Unprompted answer (rocky) |
|---|---|---|
| 1 | | |
| 2 | | |
| 3 | | |
| 4 | | |
| 5 | | |

## Known limitations

- The noise wash is off: a big, fast stream lacks its broadband rush; that's left for later.
- Eddies only shape the wake's bubbles, so they're audible mostly at Rocky creek and above.
- Large bubble clusters are independent oscillators; the fuller low "cloud" sound of coupled bubbles (Xue et al. 2023) is not modelled (optional collective modes come after the baseline).
- Flash is 92 % full; a bigger feature may need the Daisy bootloader (QSPI) or trimming.
- One fixed seed on the hardware (the same creek every power-up), by design for comparison with the renders.
