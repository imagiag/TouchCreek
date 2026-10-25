// Renders the host_test scenarios with the JS engine and compares them with the
// C++ output in out/. Run via `make -C daisy/test`.
// The engines use the same integer random streams and the same algorithms, but
// JS computes in double and C++ in float, so samples differ slightly; the test
// accepts high correlation and matching level.
const fs = require('fs'), path = require('path');
Object.assign(globalThis, require('../../web/engine/creek-config.js'));
const { CreekEngine } = require('../../web/engine/creek-engine.js');
const { CREEK_PRESETS } = require('../../web/engine/creek-controls.js');
const { csParse, csRun } = require('../../web/engine/creek-scenario.js');
const SR = 48000, OUT = path.join(__dirname, 'out'), SCN = path.join(__dirname, '../../scenarios');
let fail = 0;

function compare(name, secs) {
  const sc = csParse(fs.readFileSync(path.join(SCN, name + '.txt'), 'utf8'), SR);
  if (secs) sc.seconds = secs;
  const eng = new CreekEngine(SR, sc.seed);
  const js = csRun(sc, eng, SR, 48, CREEK_PRESETS);
  const cpp = new Float32Array(fs.readFileSync(path.join(OUT, name + '.f32')).buffer.slice(0));
  const n = js.L.length;
  if (cpp.length !== 2 * n) { console.log(`FAIL ${name}: length ${cpp.length / 2} vs ${n}`); fail++; return; }
  // Sample match over the first 10 s; level match over the whole render. After
  // long runs a rare 1-ulp difference (float vs double) can move one bubble by a
  // sample, after which the random draws of later bubbles reshuffle: the two
  // renders stay statistically the same but stop matching sample by sample.
  // (The C++ engine alone is bit-exact across runs, resets and block sizes.)
  const stat = (to) => { let sab = 0, ea = 0, eb = 0;
    for (let i = 0; i < to; i++) for (let c = 0; c < 2; c++) { const a = c ? js.R[i] : js.L[i], b = cpp[2 * i + c]; sab += a * b; ea += a * a; eb += b * b; }
    return { corr: sab / Math.sqrt(ea * eb + 1e-30), ratio: Math.sqrt(eb / (ea + 1e-30)) }; };
  const head = stat(Math.min(n, 10 * SR)), all = stat(n);
  const st = eng.stats();
  const ok = head.corr > 0.99 && Math.abs(all.ratio - 1) < 0.02;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name.padEnd(14)} correlation ${head.corr.toFixed(5)} (first 10 s)  rms C++/JS ${all.ratio.toFixed(4)}  JS births ${st.births}`);
  if (!ok) fail++;
}

for (const [k, s] of [['trickle', 10], ['rocky', 10], ['cascade', 10], ['deeper', 10], ['single_bubble'], ['radius_steps'],
  ['sweep_flow'], ['presets_walk'], ['stress'], ['notes'], ['notes_pour'], ['sweep_tone']]) compare(k, s);
console.log(fail ? `${fail} comparison(s) failed` : 'C++ and JS engines match');
process.exitCode = fail ? 1 : 0;
