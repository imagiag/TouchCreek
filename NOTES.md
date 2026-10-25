# Creek — working notes

Shared log for Claude Code (and Codex, if a website follows, as in TouchPulsar's `NOTES.md`). Keep entries short, dated and attributed. The user's current instructions take precedence.

## Layout
- `web/engine/` — portable JS engine (`creek-engine.js`), constants (`creek-config.js`, the one config file), panel logic (`creek-controls.js`), scenario runner, AudioWorklet wrapper.
- `web/` — browser audition page (`python3 web/serve.py 8766`, open http://localhost:8766).
- `daisy/creek/` — C++ port: `FlowState`, `EventQueue`, `BubbleScheduler`, `BubblePool`, `FlowBed` (noise layers), `SpatialMixer` + `OutputStage`, `CreekEngine`, `Controls`. `config.h` is generated: `node tools/gen-config.js`.
- `daisy/test/` — `host_test` (acceptance checks), `creek_render` (deterministic offline WAV renders), `compare.js` (C++ vs JS). `make -C daisy/test`, `make -C daisy/test renders`.
- `scenarios/` — seed + automation for every render (the record the brief asks for).

## Status
- 2026-10-25, Claude Code: Phase 1 (audible core) — flow bed, three-zone flow state, seeded hazard scheduler, 24/32-voice bubble pool, output stage, browser audition, C++ port with tests.
- 2026-10-25, Claude Code: firmware `CreekTouch-v1.bin` (105 KB of 128 KB flash, engine 7 KB RAM) and the Touch 2 emulator page. Waiting for the user's listening notes on hardware. CPU on hardware not measured yet (`DEBUG=1` build prints it).
- 2026-10-25, Claude Code: v2-notes released (notes mode, Level fader, wash gated by Roughness). 52/52 host checks; C++/JS match incl. note mode.
- 2026-10-25, Claude Code: Phase 2 (v3-layers): eddies (modulation only), falls (plop + J impulse + bubble children), surface ticks; all noise-free. 58/58 checks. Flash 114/128 KB.
- 2026-10-25, Claude Code: Phases 3–4 offline: v4-monitor (USB stats, P09 stress test), web/monitor.html, 30-min soak (pass), daisy/manual.html, REPORT.md. Hardware CPU / stack numbers and the listener test are still open (REPORT.md has the tables).

## Decisions
- 2026-10-25: audition host = browser page (JS twin of the engine) + C++ offline renderer; the C++/JS comparison test keeps them equal (user's choice).
- 2026-10-25: first firmware right after Phase 1 (user's choice).
- 2026-10-25: when the firmware lands, the web page becomes a Touch 2 **emulator** layout like TouchPulsar's, as a guide to the controls (user's request).
- 2026-10-25 (user, after v1): left fader = **Level** as TouchPulsar (2 v²); Flow on **S33**; S31 Scale, S32 Aeration, S34 Width, right fader Roughness. Left switch 3-way: up Presets, centre **Notes**, down Diagnostics; right switch down = Hold.
- 2026-10-25 (user): Notes mode = both: tap = tuned drop, hold = creek bubbles gather onto held notes. Pulsar's scale/octave/root pad scheme reused.
- 2026-10-25 (user): no "wind": the noisy flow bed only fades in with Roughness 0.45→0.95 (`BED_R_ON`).
- 2026-10-25 (user): remove the noise completely for now (`BED_ON = 0`); "we'll figure that part out later". So Phase 2 layers are noise-free too: eddies modulate the wake instead of being noise swells, the waterfall impact is a falling low tone instead of a noise burst, surface detail is short high ticks.

## Known limitations / reproducibility
- C++ renders are bit-exact across runs, `Reset(seed)` and block sizes (1, 37, 48, 1000).
- JS and C++ match sample by sample for the first ~10 s (correlation > 0.996); in longer runs a rare 1-ulp float/double difference moves one bubble by a sample and later random draws reshuffle. Levels stay equal. Documented float-tolerance difference, as the brief allows.

## Parked ideas
- Right switch: up and centre both = live, so one position is free for a future mode (ideas: notes + presets together, a second knob layer, Hold + Latch together). User: keep as is for now (2026-10-25).
- Revisit the flow noise / wash (user wants it figured out later): e.g. noise only as very soft, heavily modulated gurgle at high Roughness, or a noise splash transient.
- Collective low modes during dense bubble clusters (brief, after baseline passes).
- Optional subtle ambience (disabled for evaluation).
- LED flicker on loud bubble births.
- Use S30/S35 for calibration or "bubble glide amount" if listening calls for it.
- 2026-10-25, user: stress test on hardware (v5 + monitor) stays under 25 % CPU worst block: timing criterion passes.
- 2026-10-25 (user): Free flow (S30, Notes only; 0 = still, holding notes pours), Variety (S35, bipolar: one note .. default .. across the scale), Decay (S34, bipolar 1/4x..4x), Width -> P10 + S34. Scales stay exactly as TouchPulsar. Emulator knobs always show name, S-number, P10 layer and value.
- 2026-10-25 (user agreed): keep the USB monitor + P09 stress test in releases for now (~14 KB flash; v6 = 120/128 KB). When a feature no longer fits, make it a build option (normal releases without, a separate -monitor build with).
- 2026-10-25 (user): right switch in Notes = Latch (like TouchPulsar's Hold); elsewhere still Hold slow state.
- 2026-10-25 (user): Scale goes deeper: radius x up to 4 at the top (S^8 bend, presets unchanged), tau extends to x=1.5 (~0.25 s) with energy compensation, F_MIN 60 Hz, tuned range from C2.
- 2026-10-25 (user): USB MIDI in normal releases (notes in every mode, mod wheel/aftertouch = Surge, CCs 7/20-27/14/15, PC = presets, sustain, bend +-2); the monitor is now the separate `make monitor` build (CreekTouch-v*-monitor.bin). Emulator has Web MIDI.
- 2026-10-25 (user): filter from TouchPulsar (P11 + right fader cutoff, P11 + left fader resonance, CC 102/103, TPT SVF in OutputStage before the limiter). USB MIDI build patches its own PID (0x5743) + bcdDevice so iOS/Audiobus stops showing the cached name "PulsarTouch" (iOS remembered the name per VID/PID/serial). Knob layers now generic (kLayers in creek_ui.cpp).
- 2026-10-25, user: confirmed Audiobus (iOS) now lists the v10 MIDI build as "Creek" (own USB PID worked).
- 2026-10-25 (user): Scale deeper (8x, ~60 Hz); Attack P11+S33 / Release P10+S34 as TouchPulsar (pour envelope, CC 107/72), release tails stay tuned; Width -> P10+S30 (as Pulsar); Tone P10+S31 (radius-distribution skew, CC 28). Flash 122/128 KB.
- 2026-10-25 (user): stress test only in monitor build (CREEK_STRESS=1: monitor + host tests); monitor build no longer released, `make -C daisy monitor` on demand. Emulator hides P09 (ctl.stressEnabled=false).
