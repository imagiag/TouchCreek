# Credits

**Creek** is a sample-free procedural creek synthesizer for the Synthux Academy **Touch 2** (Daisy Seed).

## Research
- **Kees van den Doel**, *Physically-based Models for Liquid Sounds*, ACM Transactions on Applied Perception 2(4), 2005 — the single-bubble model (f₀ ≈ 3/r, damped sinusoids) and stochastic bubble aggregation this instrument starts from.
- **Andy Farnell**, *Designing Sound*, MIT Press, 2010 — the method of decomposing a sound into causes, excitations and resonances.
- Later listening experiments may draw on Langlois, Zheng & James (2016) and Xue, Aronson, Wang, Langlois & James (2023); Creek approximates their effects statistically and does not implement their solvers.

## Hardware and code from others
Creek's own code is MIT (see [LICENSE](LICENSE)); the third-party parts below keep their own terms.

- **[Synthux Academy](https://www.synthux.academy/)** — the [Touch 2](https://www.synthux.academy/store/simple-touch-2) hardware. The pad, knob and switch drivers in `daisy/touch/` and the board photo `daisy/touch.jpeg` come from Synthux Academy's [TouchString](https://github.com/Synthux-Academy/TouchString) (DaisySP dependency replaced by a local clamp), via TouchPulsar. See also [simple-touch-instruments](https://github.com/Synthux-Academy/simple-touch-instruments) (MIT).
  - TouchString contributors: [Vincent Maurer](https://github.com/vincent-maurer), [Rosa Schuurmans](https://github.com/vitrinekast), [Vlad Litvinenko](https://github.com/bleeptools).
- **[libDaisy](https://github.com/electro-smith/libDaisy)** by Electrosmith (MIT), used as a submodule via [Synthux's fork](https://github.com/Synthux-Academy/libDaisy), pinned at `5c86d21`.

## Development
Designed and played by the Creek author; engine, firmware, browser prototype and documentation written with Claude Code (Anthropic), following the author's implementation brief.
