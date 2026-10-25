# Creek — working notes

Shared log for Claude Code (and Codex, if a website follows, as in TouchPulsar's `NOTES.md`). Keep entries short, dated and attributed. The user's current instructions take precedence.

## Layout
- `web/engine/` — portable JS engine (`creek-engine.js`), constants (`creek-config.js`, the one config file), panel logic (`creek-controls.js`), scenario runner, AudioWorklet wrapper.
- `web/` — browser audition page (`python3 web/serve.py 8766`, open http://localhost:8766).
- `daisy/creek/` — C++ port: `FlowState`, `EventQueue`, `BubbleScheduler`, `BubblePool`, `FlowBed` (noise layers), `SpatialMixer` + `OutputStage`, `CreekEngine`, `Controls`. `config.h` is generated: `node tools/gen-config.js`.
- `daisy/test/` — `host_test` (acceptance checks), `creek_render` (deterministic offline WAV renders), `compare.js` (C++ vs JS). `make -C daisy/test`, `make -C daisy/test renders`.
- `scenarios/` — seed + automation for every render (the record the brief asks for).

## Status
- 2026-10-25, Claude Code: Phase 1 (audible core) — flow bed, three-zone flow state, seeded hazard scheduler, 24/32-voice bubble pool, output stage, browser audition, C++ port with tests. Firmware v1 next.

## Decisions
- 2026-10-25: audition host = browser page (JS twin of the engine) + C++ offline renderer; the C++/JS comparison test keeps them equal (user's choice).
- 2026-10-25: first firmware right after Phase 1 (user's choice).
- 2026-10-25: when the firmware lands, the web page becomes a Touch 2 **emulator** layout like TouchPulsar's, as a guide to the controls (user's request).
- Trims: brief's "trim 1..4" = top row S31 Scale, S32 Aeration, S33 Gain, S34 Width; S30/S35 reserved. Switches: left = Performance/Diagnostics, right = Live/Hold. Pads P03–P06 = presets; diagnostics P03–P07 = layers, P08 = all. (Ask the user if they'd like a different arrangement.)

## Known limitations / reproducibility
- C++ renders are bit-exact across runs, `Reset(seed)` and block sizes (1, 37, 48, 1000).
- JS and C++ match sample by sample for the first ~10 s (correlation > 0.996); in longer runs a rare 1-ulp float/double difference moves one bubble by a sample and later random draws reshuffle. Levels stay equal. Documented float-tolerance difference, as the brief allows.

## Parked ideas
- Collective low modes during dense bubble clusters (brief, after baseline passes).
- Optional subtle ambience (disabled for evaluation).
- LED flicker on loud bubble births.
- Use S30/S35 for calibration or "bubble glide amount" if listening calls for it.
