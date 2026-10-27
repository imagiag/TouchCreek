# Creek

### 🌐 Website: **[creek.imagiag.com](https://creek.imagiag.com)**

**[Download firmware](https://github.com/imagiag/TouchCreek/releases/latest)** · **[Manual](daisy/README.md)** · **[Website](https://creek.imagiag.com)**

A sample-free procedural stereo creek for the [Synthux Academy Touch 2](https://www.synthux.academy/store/simple-touch-2) (Daisy Seed): from a quiet trickle to a lively shallow stream, built from hundreds of tiny bubble resonators per second, slow eddies, small splashes and fine surface ticks, after van den Doel's *Physically-based Models for Liquid Sounds* (2005).

- **Firmware and manual**: [daisy/README.md](daisy/README.md), downloads in [daisy/releases/](daisy/releases/)
- **Browser emulator**: Windows: `py -3 web/serve.py 8766`; macOS/Linux: `python3 web/serve.py 8766` → http://localhost:8766 (hardware monitor: `/monitor.html`)
- **Manual**: [daisy/manual.html](daisy/manual.html) · **Validation report**: [REPORT.md](REPORT.md)
- **Engine**: portable C++ in [daisy/creek/](daisy/creek/) (no hardware code), with a JS twin in [web/engine/](web/engine/); all tuning constants in [web/engine/creek-config.js](web/engine/creek-config.js)
- **Renders and tests**: `make -C daisy/test` (checks + C++/JS comparison), `make -C daisy/test renders` (deterministic WAVs from [scenarios/](scenarios/)), `make -C daisy/test soak` (30-minute stability run)

## Get it

- **Website:** [creek.imagiag.com](https://creek.imagiag.com): overview, sounds and the manual.
- **Firmware for the Touch 2:** download the newest `.bin` from [Releases](https://github.com/imagiag/TouchCreek/releases/latest) (or `daisy/releases/`) and flash it at [flash.daisy.audio](https://flash.daisy.audio/). Full manual: [`daisy/manual.html`](daisy/manual.html) (open locally) or [`daisy/README.md`](daisy/README.md).
- **Build from source:** `git clone --recurse-submodules https://github.com/imagiag/TouchCreek.git`, then see [`daisy/README.md`](daisy/README.md#project-setup).

Credits: [CREDITS.md](CREDITS.md). License: MIT.
