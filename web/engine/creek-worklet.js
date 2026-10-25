// AudioWorklet wrapper. app.js concatenates creek-config.js + creek-engine.js +
// this file into one Blob module (worklet modules don't share globals).
// Equivalent on Daisy: the audio callback in daisy/CreekTouch.cpp.

class CreekProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const seed = (options && options.processorOptions && options.processorOptions.seed) || 1;
    this.eng = new CreekEngine(sampleRate, seed);
    this.lastLog = 0; this.lastPost = 0; this.peak = 0;
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.type === 'macros') this.eng.setMacros(m.m);
      else if (m.type === 'trigger') this.eng.trigger(m.ev);
      else if (m.type === 'reset') { this.eng.reset(m.seed >>> 0); this.lastLog = 0; }
    };
  }
  process(inputs, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0];
    this.eng.process(L, R, L.length);
    for (let i = 0; i < L.length; i++) { const a = Math.abs(L[i]), b = Math.abs(R[i]); if (a > this.peak) this.peak = a; if (b > this.peak) this.peak = b; }
    if (currentTime - this.lastPost > 0.033) {
      const p = this.eng.pool, w = p.logW;
      let n = (w - this.lastLog) >>> 0; if (n > CR_LOG) n = CR_LOG;
      const f = new Float32Array(n), a = new Float32Array(n), pan = new Float32Array(n);
      for (let k = 0; k < n; k++) { const j = (w - n + k) & (CR_LOG - 1); f[k] = p.logF[j]; a[k] = p.logA[j]; pan[k] = p.logP[j]; }
      this.lastLog = w; this.lastPost = currentTime;
      this.port.postMessage({ type: 'stats', s: this.eng.stats(), f, a, pan, peak: this.peak });
      this.peak = 0;
    }
    return true;
  }
}
registerProcessor('creek', CreekProcessor);
