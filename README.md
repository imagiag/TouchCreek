# Creek

A sample-free procedural stereo creek for the [Synthux Academy Touch 2](https://www.synthux.academy/store/simple-touch-2) (Daisy Seed): from a quiet trickle to a lively shallow stream, built from a continuous flow bed and hundreds of tiny bubble resonators per second, after van den Doel's *Physically-based Models for Liquid Sounds* (2005).

- **Firmware and manual**: [daisy/README.md](daisy/README.md), downloads in [daisy/releases/](daisy/releases/)
- **Browser emulator**: `python3 web/serve.py 8766` → http://localhost:8766
- **Engine**: portable C++ in [daisy/creek/](daisy/creek/) (no hardware code), with a JS twin in [web/engine/](web/engine/); all tuning constants in [web/engine/creek-config.js](web/engine/creek-config.js)
- **Renders and tests**: `make -C daisy/test` (checks + C++/JS comparison), `make -C daisy/test renders` (deterministic WAVs from [scenarios/](scenarios/))

Credits: [CREDITS.md](CREDITS.md). License: MIT.
