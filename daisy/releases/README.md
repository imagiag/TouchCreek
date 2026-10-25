# Creek releases

Releases use USB for MIDI. The monitor build (CPU report for `web/monitor.html` + P09 stress test) is no longer released from v12 on; build it with `make -C daisy monitor` when needed. Older `-monitor` files are kept.

Flash these with https://flash.daisy.audio/ (hold BOOT while plugging in, or BOOT + RESET).
`../build/CreekTouch.bin` is only a working build and may be newer than any release: flash from here.

| File | Date | Changes |
|---|---|---|
| `CreekTouch-v13.bin` | 2026-10-25 | **Release moved to P11 + S34**, next to Attack (P11 + S33): both envelope controls on one modifier. CCs unchanged (107 / 72). |
| `CreekTouch-v12.bin` | 2026-10-25 | Same sound and controls as v11. The P09 stress test is now only in the monitor build (saves flash); monitor builds are no longer released. |
| `CreekTouch-v11-attack-release.bin` | 2026-10-25 | **Attack** (P11 + S33, 4 ms – 3 s) and **Release** (P11 + S34, 20 ms – 4 s) for the note pour, as TouchPulsar (CC 107 / 72); release tails stay on the played notes. **Tone** (P10 + S31): dark ↔ bright mix (CC 28). **Deeper Scale**: up to 8× bubble size, ~60 Hz. **Width moved to P10 + S30** (as TouchPulsar). |
| `CreekTouch-v11-monitor.bin` | 2026-10-25 | Same as v11, USB = CPU report instead of MIDI. |
| `CreekTouch-v10-filter.bin` | 2026-10-25 | **Filter** as TouchPulsar: P11 + right fader = low-pass cutoff (30 Hz – 18 kHz, open at power-up), P11 + left fader = resonance (CC 102/103). USB now has its own product ID, so iOS / Audiobus should list it as **Creek** instead of a remembered name. |
| `CreekTouch-v10-monitor.bin` | 2026-10-25 | Same as v10-filter, USB = CPU report for `web/monitor.html` instead of MIDI. |
| `CreekTouch-v9-midi.bin` | 2026-10-25 | **USB MIDI**: notes play Creek in every mode (drop + pour, velocity, sustain, bend ±2), **mod wheel / aftertouch = Surge** (the water rushes), CCs for every knob (7 Level, 20–27), CC 14/15 scale/root, Program Change 1–4 = presets. The USB monitor moved to a separate build: |
| `CreekTouch-v9-monitor.bin` | 2026-10-25 | Same as v9-midi but USB sends the CPU / voices / stack report for `web/monitor.html` instead of MIDI. |
| `CreekTouch-v8-deep.bin` | 2026-10-25 | **Deeper Scale**: the last quarter of S31 bends up to 4× bubble size (was 1.8×) for bass, deepest bubbles ~125 Hz, ringing longer at matched energy. Notes can tune down to C2. Presets unchanged. |
| `CreekTouch-v7-latch.bin` | 2026-10-25 | **Latch** in Notes: right switch down keeps touched notes pouring after release (up to 4); tap a latched note again to release it. In Presets / Diagnostics the right switch still holds the slow creek state. |
| `CreekTouch-v6-pour.bin` | 2026-10-25 | **Free flow** (S30, Notes): how much the creek plays by itself, fully left = still; holding a note now **pours** water onto it. **Variety** (S35): left = one note, noon = default, right = across the active scale. **Decay** (S34): ¼× … 4×, noon = default. **Width moved to P11 + S34.** |
| `CreekTouch-v5-monitor-fix.bin` | 2026-10-25 | Fixes the USB report for `web/monitor.html` (lines were too long for libDaisy's logger and got cut off). Sound unchanged from v4. |
| `CreekTouch-v4-monitor.bin` | 2026-10-25 | Sound as v3 (large bubbles slightly less loud, for headroom). Reports CPU, voices and stack over USB once a second for `web/monitor.html`. **Diagnostics P09 = stress test** (worst case, LED blinks fast). USB shows up as "Creek". |
| `CreekTouch-v3-layers.bin` | 2026-10-25 | **Phase 2 layers**: eddies (slow swells that make the rock wake surge), small waterfalls (a soft low plop at the drop, then bubbly settling), surface detail (sparse quiet high ticks). **Noise wash off** completely. Controls unchanged from v2. Diagnostics P05–P07 now mute eddies / falls / surface. |
| `CreekTouch-v2-notes.bin` | 2026-10-25 | **Notes mode** (left switch centre): P03–P09 play a scale, tap = tuned drop, hold = the creek's bubbles gather onto the held notes; P00/P02 octave, P11 + pad = scale, P10 + pad = root (as TouchPulsar). **Left fader = Level** (as TouchPulsar), **Flow moved to S33**. The noisy wash now only comes in with Roughness above about half way. Left switch: up = presets, centre = notes, down = diagnostics. |
| `CreekTouch-v1.bin` | 2026-10-25 | First hardware version (Phase 1, audible core): flow bed + bubble pool on a three-zone creek state. Faders = Flow / Roughness, S31–S34 = Scale / Aeration / Gain / Width, P03–P06 = presets with soft pickup, left switch = diagnostics (P03/P04 mute flow / bubbles), right switch = hold. |
