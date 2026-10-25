// Host test for the portable C++ engine + controls (no Daisy needed).
//   make -C daisy/test        (builds and runs, then compares with the JS engine)
// 1. Controls checks.
// 2. Engine acceptance checks from the brief that can run offline
//    (reproducibility, block partitions, radius -> pitch, Flow -> silence,
//    Aeration / Scale direction, stress: finite, peaks, DC).
// 3. Writes out/<name>.f32 (interleaved stereo float) for compare.js, which
//    renders the same scenarios with the JS engine and compares.
#include "scenario.h"
#include <chrono>

using namespace creek;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const float SR = 48000.f;
static CreekEngine eng, eng2;
static const char* kDir = "../../scenarios/";

static scenario::Scenario load(const char* name, float secs = -1) {
    auto sc = scenario::parse(scenario::readFile(std::string(kDir) + name + ".txt"), SR);
    if (secs > 0) sc.seconds = secs;
    return sc;
}
static void render(CreekEngine& e, const scenario::Scenario& sc, int block, std::vector<float>& L, std::vector<float>& R) {
    scenario::run(sc, e, SR, block, L, R, [](long, long) {});
}
static void writeF32(const char* name, const std::vector<float>& L, const std::vector<float>& R) {
    std::string p = std::string("out/") + name + ".f32";
    FILE* f = fopen(p.c_str(), "wb");
    for (size_t i = 0; i < L.size(); i++) { fwrite(&L[i], 4, 1, f); fwrite(&R[i], 4, 1, f); }
    fclose(f);
}

static void controlsTests() {
    Controls c;
    CHECK(fabsf(LevelToGain(0.7071f) - 1.f) < 1e-3f, "level ~70 %% = 0 dB");
    CHECK(LevelToGain(0.f) == 0.f, "level bottom = silent");
    CHECK(fabsf(KnobToWidth(0.5f) - 1.f) < 1e-6f, "width noon = 1");
    c.Tick(0.0);
    c.PadDown(kPadPreset0 + 2, .7f); c.PadUp(kPadPreset0 + 2);  // Small cascade
    c.Tick(0.1);
    CHECK(c.value[M_FLOW] > 0.2f && c.value[M_FLOW] < 0.75f, "preset glide in progress (%f)", c.value[M_FLOW]);
    c.Tick(0.3);
    CHECK(fabsf(c.value[M_FLOW] - 0.75f) < 1e-6f && fabsf(c.value[M_AER] - 0.85f) < 1e-6f, "preset reached after 250 ms");
    CHECK(!c.Gliding(), "glide finished");
    c.SetMode(MODE_DIAG);
    c.PadDown(kPadLayer0 + 1, .7f);   // mute bubbles
    Macros m; c.Compute(m);
    CHECK(m.mask == (0x1Fu & ~2u), "diag: P04 mutes bubbles (mask %x)", m.mask);
    c.PadDown(kPadAll, .7f); c.Compute(m);
    CHECK(m.mask == 0x1Fu, "diag: P08 restores all");
    c.PadDown(kPadLayer0, .7f); c.SetMode(MODE_PRESETS); c.Compute(m);
    CHECK(m.mask == 0x1Fu, "presets mode always plays all layers");
    c.SetMode(MODE_DIAG); c.Compute(m);
    CHECK(m.mask == 0x1Eu, "diag mask remembered");

    // notes
    Controls n; n.SetMode(MODE_NOTES);
    Drop d;
    n.PadDown(3, 0.9f); CHECK(n.PopDrop(d) && fabsf(d.hz - MidiToHz(72)) < 0.01f && d.vel == 0.9f, "P03 = C5 drop (Maj Pent)");
    n.Compute(m); CHECK(m.tune && fabsf(m.tuneHz[0] - MidiToHz(72)) < 0.01f, "held note tunes the creek");
    n.PadDown(5, 0.5f); n.PopDrop(d); CHECK(fabsf(d.hz - MidiToHz(76)) < 0.01f, "P05 = E5");
    n.Compute(m); CHECK(m.tuneHz[1] > 0 && m.tuneHz[2] == 0, "two held notes");
    n.PadUp(3); n.PadUp(5); n.Compute(m); CHECK(!m.tune, "release: no tuning");
    n.PadDown(kPadOctUp, .7f); n.PadUp(kPadOctUp);
    n.PadDown(3, .7f); n.PopDrop(d); n.PadUp(3); CHECK(fabsf(d.hz - MidiToHz(84)) < 0.01f, "octave up = C6");
    n.PadDown(kPadScale, .7f); n.PadDown(4, .7f); n.PadUp(4); n.PadUp(kPadScale);
    CHECK(n.scale == 1 && !n.PopDrop(d), "P11 + P04 = Major, no drop");
    n.PadDown(kPadRoot, .7f); n.PadDown(7, .7f); n.PadUp(7); n.PadUp(kPadRoot);
    CHECK(n.root == 7, "P10 + P07 (G) sets root G (%d)", n.root);
    n.PadDown(3, .7f); n.PopDrop(d); n.PadUp(3); CHECK(fabsf(d.hz - MidiToHz(84 + 7)) < 0.01f, "G major, octave +1: P03 = G6");
    n.Compute(m); CHECK(m.scaleMask == ((1u << 7) | (1u << 9) | (1u << 11) | (1u << 0) | (1u << 2) | (1u << 4) | (1u << 6)), "G major scale mask (%x)", m.scaleMask);
    n.value[M_FREE] = 0.2f; n.Compute(m); CHECK(m.freeFlow == 0.2f, "Free flow knob acts in Notes");
    n.SetMode(MODE_PRESETS); n.Compute(m); CHECK(!m.tune, "leaving notes clears tuning");
    CHECK(m.freeFlow == 1.f, "Presets mode always flows");

    // latch: Notes + right switch down
    {
        Controls l; l.SetMode(MODE_NOTES); l.SetRightSwitch(true); Drop dl; Macros ml;
        CHECK(l.latch && !l.hold, "right switch in Notes = Latch, not Hold");
        l.PadDown(4, .7f); l.PadUp(4); l.Compute(ml);
        CHECK(ml.tune && l.nHeld == 1, "latched note keeps pouring after release");
        l.PadDown(6, .7f); l.PadUp(6); l.Compute(ml);
        CHECK(l.nHeld == 2 && ml.tuneHz[1] > 0, "latch builds a chord");
        while (l.PopDrop(dl)) {}
        l.PadDown(4, .7f); l.PadUp(4);
        CHECK(l.nHeld == 1 && !l.PopDrop(dl), "tapping a latched note releases it (no drop)");
        l.PadDown(8, .7f);                       // still touching P08
        l.SetRightSwitch(false);
        CHECK(l.nHeld == 1 && l.held[0] == 8, "latch off keeps only what is touched");
        l.PadUp(8); l.Compute(ml); CHECK(!ml.tune, "then releases normally");
        Controls h; h.SetMode(MODE_PRESETS); h.SetRightSwitch(true);
        CHECK(h.hold && !h.latch, "right switch in Presets = Hold slow state");
    }

    // MIDI
    {
        Controls c; Macros mm; Drop dm;
        c.Midi(0x90, 69, 127); c.Compute(mm);
        CHECK(c.PopDrop(dm) && fabsf(dm.hz - 440.f) < 0.01f && dm.vel == 1.f, "MIDI note on = drop at its pitch, velocity");
        CHECK(mm.tune && fabsf(mm.tuneHz[0] - 440.f) < 0.01f, "MIDI notes pour in Presets mode too");
        c.Midi(0x80, 69, 0); c.Compute(mm); CHECK(!mm.tune, "note off releases");
        c.Midi(0xB0, 64, 127); c.Midi(0x91, 60, 80); c.Midi(0x81, 60, 0); c.Compute(mm);
        CHECK(mm.tune, "sustain pedal holds");
        c.Midi(0xB0, 64, 0); c.Compute(mm); CHECK(!mm.tune, "pedal up releases");
        c.Midi(0xB0, 20, 0); ControlEvent ce; int8_t ca; bool sawCc = false;
        while (c.PopEvent(ce, ca)) if (ce == CE_CC && ca == M_FLOW) sawCc = true;
        CHECK(c.value[M_FLOW] == 0.f && sawCc, "CC 20 sets Flow (and asks the knob to wait for pickup)");
        c.Midi(0xB0, 1, 127); c.Compute(mm);
        CHECK(mm.flow > 0.99f && mm.roughness > c.value[M_ROUGH], "mod wheel = Surge (Flow and Roughness up)");
        c.Midi(0xB0, 1, 0); c.Compute(mm); CHECK(mm.flow == 0.f, "wheel back = no change");
        c.Midi(0xC0, 3, 0); CHECK(c.preset == 3 && c.Gliding(), "Program Change 4 = Deeper run");
        while (c.PopDrop(dm)) {}
        c.Midi(0xE0, 0x7F, 0x7F); c.Midi(0x90, 69, 100); c.PopDrop(dm);
        CHECK(fabsf(dm.hz - 440.f * exp2f(2.f / 12.f)) < 0.1f, "pitch bend +2 semitones");
        c.Midi(0xB0, 123, 0); c.Compute(mm); CHECK(!mm.tune, "all notes off");
        c.Midi(0xB0, 14, 127); CHECK(c.scale == 6, "CC 14 picks the scale");
        Controls lt; lt.SetMode(MODE_NOTES); lt.SetRightSwitch(true);
        lt.Midi(0x90, 64, 90); lt.Midi(0x80, 64, 0); lt.Compute(mm); CHECK(mm.tune, "latch holds MIDI notes");
        lt.Midi(0x90, 64, 90); lt.Compute(mm); CHECK(!mm.tune, "playing a latched MIDI note again releases it");
    }

    // stress test (diagnostics P09)
    Controls st; st.SetMode(MODE_DIAG); st.Tick(10.0);
    st.PadDown(kPadStress, .7f); st.PadUp(kPadStress);
    CHECK(st.stress, "P09 starts the stress test");
    int nd = 0, changes = 0; float lastFlow = -1;
    for (int i = 0; i < 500; i++) {                 // 2 s of 4 ms UI ticks
        st.Tick(10.0 + i * 0.004); Drop dd; while (st.PopDrop(dd)) nd++;
        Macros sm; st.Compute(sm); if (sm.flow != lastFlow) changes++; lastFlow = sm.flow;
        if (sm.mask != 0x1Fu) { CHECK(false, "stress plays all layers"); break; }
    }
    CHECK(nd >= 30 && nd <= 50, "stress: ~20 drops per second (%d in 2 s)", nd);
    CHECK(changes >= 2, "stress: settings jump (%d flow changes)", changes);
    st.SetMode(MODE_PRESETS); CHECK(!st.stress, "leaving diagnostics stops the stress test");
}

static double rmsOf(const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; i++) s += v[i] * v[i]; return sqrt(s / std::max<size_t>(1, b - a)); }

static void engineTests() {
    std::vector<float> L1, R1, L2, R2;
    // --- reproducibility and block partitions (bit-exact) ---
    auto tr = load("trickle", 10);
    eng.Init(SR, 1);
    render(eng, tr, 48, L1, R1);
    for (int blk : {37, 1, 1000, 48}) {
        eng2.Init(SR, 99);   // different init seed: scenario sets seed 1 via Reset
        render(eng2, tr, blk, L2, R2);
        CHECK(L1 == L2 && R1 == R2, "block size %d gives identical output", blk);
    }
    render(eng, tr, 48, L2, R2); // Reset on the same engine
    CHECK(L1 == L2 && R1 == R2, "Reset(seed) reproduces the render");
    { auto o = tr; o.seed = 2; render(eng2, o, 48, L2, R2); CHECK(!(L1 == L2), "a different seed gives a different render"); }

    // --- muting a layer leaves the other layers unchanged ---
    {
        auto a = scenario::parse("seconds 4\nat 0 preset rocky\nat 0 set mask 2\n", SR);
        auto b = scenario::parse("seconds 4\nat 0 preset rocky\nat 0 set mask 3\n", SR);
        auto c = scenario::parse("seconds 4\nat 0 preset rocky\nat 0 set mask 1\n", SR);
        std::vector<float> L3, R3;
        render(eng, a, 48, L1, R1); render(eng, b, 48, L2, R2); render(eng, c, 48, L3, R3);
        double md = 0; // output stage is linear until the limiter: bubbles + flow = both
        for (size_t i = 0; i < L1.size(); i++) md = std::max(md, (double)fabsf(L1[i] + L3[i] - L2[i]));
        CHECK(md < 1e-4, "solo layers add up to the two-layer mix (max diff %g)", md);
    }

    // --- radius -> pitch (zero crossings 2..40 ms after each first bubble) ---
    {
        auto rs = load("radius_steps");
        render(eng, rs, 48, L1, R1);
        const float radii[5] = {0.5f, 1, 2, 4, 6};
        float prev = 1e9f;
        for (int k = 0; k < 5; k++) {
            int zc = 0; const size_t a = (size_t)((k + 0.002) * SR), b = (size_t)((k + 0.040) * SR);
            for (size_t i = a + 1; i < b; i++) if ((L1[i - 1] < 0) != (L1[i] < 0)) zc++;
            const float f = zc / 2.f / 0.038f, want = 3000.f / radii[k];
            CHECK(fabsf(f - want) / want < 0.03f, "radius %.1f mm -> %.0f Hz (3/r = %.0f)", radii[k], f, want);
            CHECK(f < prev, "pitch falls as radius grows");
            prev = f;
        }
        writeF32("radius_steps", L1, R1);
    }

    // --- Flow 0 -> silence within 3 s ---
    {
        auto fz = load("flow_to_zero");
        render(eng, fz, 48, L1, R1);
        size_t last = 0;
        for (size_t i = (size_t)(4 * SR); i < L1.size(); i++) if (fabsf(L1[i]) > 1e-4f || fabsf(R1[i]) > 1e-4f) last = i;
        const float t = last / SR - 4.f;
        CHECK(t < 3.f, "Flow 0: below -80 dBFS after %.2f s (< 3 s)", t);
    }

    // --- Aeration 0 removes bubbles; Aeration raises count; Scale lowers pitch ---
    {
        auto a0 = scenario::parse("seconds 5\nat 0 set flow 0.7 roughness 0.7 scale 0.5 aeration 0\n", SR);
        render(eng, a0, 48, L1, R1);
        Stats s; eng.GetStats(s);
        CHECK(s.births == 0, "Aeration 0: no bubble events (%u)", s.births);
        CHECK(rmsOf(L1, (size_t)SR, L1.size()) > 1e-3, "Aeration 0 still flows");
    }
    for (const char* k : {"sweep_aeration", "sweep_scale", "sweep_flow"}) {
        auto sc = load(k);
        uint32_t b[4] = {0, 0, 0, 0}; float lf[4] = {0, 0, 0, 0};
        Stats prev; memset(&prev, 0, sizeof prev);
        int q = 0;
        scenario::run(sc, eng, SR, 48, L1, R1, [&](long pos, long n) {
            if ((pos + n) % (long)(5 * SR) == 0 && q < 4) {
                Stats s; eng.GetStats(s);
                b[q] = s.births - prev.births; lf[q] = s.sumLog2F - prev.sumLog2F; prev = s; q++;
            }
        });
        if (!strcmp(k, "sweep_aeration")) CHECK(b[3] > 2 * b[0], "Aeration raises bubble count (%u -> %u per 5 s)", b[0], b[3]);
        if (!strcmp(k, "sweep_flow")) CHECK(b[3] > 2 * b[0], "Flow raises bubble count (%u -> %u per 5 s)", b[0], b[3]);
        if (!strcmp(k, "sweep_scale")) {
            const float f0 = exp2f(lf[0] / b[0]), f3 = exp2f(lf[3] / b[3]);
            CHECK(f3 < 0.7f * f0, "Scale lowers typical bubble pitch (%.0f -> %.0f Hz)", f0, f3);
        }
        if (!strcmp(k, "sweep_flow")) writeF32("sweep_flow", L1, R1);
    }

    // --- the noise wash is switched off (BED_ON = 0) ---
    {
        auto hi = scenario::parse("seconds 3\nat 0 set flow 0.9 roughness 1 scale 0.5 aeration 0.5 mask 1\n", SR);
        render(eng, hi, 48, L1, R1); const double rh = rmsOf(L1, (size_t)SR, L1.size());
        CHECK(rh < 1e-7, "wash off: flow-bed layer silent even at Roughness 1 (rms %g)", rh);
    }

    // --- Phase 2 layers: eddies, small waterfalls, surface detail ---
    {
        auto layerRms = [&](const char* preset, int mask, Stats& st) {
            char txt[160]; snprintf(txt, sizeof txt, "seconds 20\nat 0 preset %s\nat 0 set mask %d\n", preset, mask);
            auto sc = scenario::parse(txt, SR);
            float eMax = 0;
            scenario::run(sc, eng, SR, 48, L1, R1, [&](long, long) { Stats s; eng.GetStats(s); eMax = fmaxf(eMax, s.eddy); });
            eng.GetStats(st); st.eddy = eMax;
            return rmsOf(L1, (size_t)SR, L1.size()) + rmsOf(R1, (size_t)SR, R1.size());
        };
        Stats all, bub, falls, surf, noEddy;
        layerRms("rocky", 31, all);
        const double rb = layerRms("rocky", 2, bub), rf = layerRms("rocky", 8, falls), rs = layerRms("rocky", 16, surf);
        layerRms("rocky", 31 & ~4, noEddy);
        CHECK(all.eddy > 0.2f, "eddies swell in the rock wake (max %.2f)", all.eddy);
        CHECK(noEddy.eddy == 0.f, "muting eddies removes them");
        const float spl = all.splashes / 20.f, sfr = all.surface / 20.f;
        CHECK(spl >= 0.2f && spl <= 4.f, "splashes %.2f/s within 0.2-4/s", spl);
        CHECK(sfr >= 2.f && sfr <= 40.f, "surface ticks %.1f/s within 2-40/s", sfr);
        CHECK(rf > 1e-4 && rs > 1e-5, "falls and surface layers are audible (rms %g, %g)", rf, rs);
        CHECK(rs < 0.2 * rb, "surface detail does not dominate (%.1f dB under the bubbles)", 20 * log10(rb / rs));
        Stats tr, ca;
        layerRms("trickle", 31, tr); layerRms("cascade", 31, ca);
        CHECK(ca.splashes > 2 * tr.splashes && ca.surface > 2 * tr.surface, "more splashes and surface activity as the creek gets livelier");
        printf("rocky: bubbles %.0f/s, splashes %.1f/s, surface %.1f/s; layer rms bubbles %.1f, falls %.1f, surface %.1f dBFS\n",
               all.births / 20.f, spl, sfr, 20 * log10(rb / 2), 20 * log10(rf / 2), 20 * log10(rs / 2));
    }

    // --- note mode: tuned drop pitch, and held notes tune the creek ---
    {
        eng.Init(SR, 1);
        Macros m; m.aeration = 0; eng.SetMacros(m);
        Event e; e.type = EV_DROP; e.radiusMm = 3000.f / 880.f; e.amp = 1; e.pan = 0;
        eng.Trigger(e);
        std::vector<float> a(4800), b(4800);
        eng.Process(a.data(), b.data(), 4800);
        int zc = 0; for (size_t i = 97; i < 960; i++) if ((a[i - 1] < 0) != (a[i] < 0)) zc++;
        const float f = zc / 2.f / (863.f / SR);
        CHECK(fabsf(f - 880.f) / 880.f < 0.05f, "tuned drop at 880 Hz sounds at %.0f Hz", f);
        Stats s; eng.GetStats(s);
        CHECK(s.births >= 2 && s.births <= 4, "drop = bubble + 1..3 children (%u)", s.births);

        eng.Init(SR, 1);
        Macros t; t.flow = 0.6f; t.roughness = 0.5f; t.scale = 0.5f; t.aeration = 0.7f; t.tune = true; t.tuneHz[0] = 1000.f;
        eng.SetMacros(t);
        std::vector<float> L(48), R(48);
        for (int i = 0; i < 1000; i++) eng.Process(L.data(), R.data(), 48);       // 1 s to gather
        uint32_t last = eng.Pool().logW; int on = 0, tot = 0;
        for (int i = 0; i < 2000; i++) {
            eng.Process(L.data(), R.data(), 48);
            const BubblePool& p = eng.Pool();
            for (; last != p.logW; last++) {
                const float l2 = log2f(p.logF[last & (kLog - 1)] / 1000.f);
                if (fabsf(l2 - roundf(l2)) < 0.01f) on++;
                tot++;
            }
        }
        CHECK(tot > 50 && on > 0.75f * tot, "holding a note tunes most creek bubbles (%d of %d)", on, tot);
    }

    // --- note mode: Free flow, pouring, Variety, Decay ---
    {
        // births' pitches in a time window, as MIDI notes
        auto births = [&](const char* txt, float from, std::vector<float>& midi) {
            auto sc = scenario::parse(txt, SR);
            uint32_t last = 0; midi.clear();
            scenario::run(sc, eng, SR, 48, L1, R1, [&](long pos, long n) {
                const BubblePool& p = eng.Pool();
                for (; last != p.logW; last++)
                    if ((pos + n) / SR >= from) midi.push_back(69.f + 12.f * log2f(p.logF[last & (kLog - 1)] / 440.f));
            });
        };
        std::vector<float> mid;
        births("seconds 4\nat 0 preset rocky\nat 0 set freeflow 0\n", 1.f, mid);
        CHECK(mid.empty() && rmsOf(L1, (size_t)(2 * SR), L1.size()) < 1e-5, "Free flow 0, no notes: still water (%zu bubbles)", mid.size());
        // C major pentatonic = C D E G A
        const char* pour = "seconds 6\nat 0 preset rocky\nat 0 set freeflow 0 variety 0 scalemask 661\nat 1 tune 523.25\n";
        births(pour, 2.f, mid);
        int exact = 0; for (float m : mid) if (fabsf(m - 72.f) < 0.05f) exact++;
        CHECK(mid.size() > 50 && exact == (int)mid.size(), "holding a note pours; Variety 0 = all on the note (%d of %zu)", exact, mid.size());
        const char* wide = "seconds 6\nat 0 preset rocky\nat 0 set freeflow 0 variety 1 scalemask 661\nat 1 tune 523.25\n";
        births(wide, 2.f, mid);
        int inScale = 0, pcs = 0; bool seen[12] = {false};
        for (float m : mid) { const int n = (int)lroundf(m); if (fabsf(m - n) < 0.05f && ((661 >> (n % 12)) & 1)) inScale++; if (!seen[n % 12]) { seen[n % 12] = true; pcs++; } }
        if (inScale != (int)mid.size()) for (float m : mid) { const int n = (int)lroundf(m); if (!(fabsf(m - n) < 0.05f && ((661 >> (n % 12)) & 1))) printf("  off-scale bubble: midi %.2f\n", m); }
        CHECK(inScale == (int)mid.size() && pcs >= 5, "Variety 1: all notes in the scale, %d pitch classes", pcs);
        // Decay: same bubble, longer tail
        auto tail = [&](float dec) {
            char txt[200]; snprintf(txt, sizeof txt, "seconds 1\nat 0 set flow 0.2 aeration 0 mask 2 decay %g\nat 0.1 bubble zone 0 radius 2 amp 1 rise 0 pan 0\n", dec);
            auto sc = scenario::parse(txt, SR); render(eng, sc, 48, L1, R1);
            return rmsOf(L1, (size_t)(0.15 * SR), (size_t)(0.25 * SR));
        };
        const double t0 = tail(0.f), t5 = tail(0.5f), t1 = tail(1.f);
        CHECK(t0 < t5 && t5 < t1 && t1 > 3 * t5, "Decay knob shortens / lengthens tails (%.2g < %.2g < %.2g)", t0, t5, t1);
    }

    // --- filter (as TouchPulsar): low cutoff removes the high bubbles; open is transparent-ish ---
    {
        auto rms = [&](float cut, float res) {
            char txt[160]; snprintf(txt, sizeof txt, "seconds 3\nat 0 preset rocky\n"); auto sc = scenario::parse(txt, SR);
            eng.Init(SR, 1);
            Macros fm; fm.flow = 0.5f; fm.roughness = 0.6f; fm.scale = 0.45f; fm.aeration = 0.55f; fm.cutoff = cut; fm.resonance = res;
            eng.SetMacros(fm);
            std::vector<float> a(48), b(48); double s2 = 0;
            for (int i = 0; i < 3000; i++) { eng.Process(a.data(), b.data(), 48); if (i > 1000) for (int j = 0; j < 48; j++) s2 += a[j] * a[j]; }
            return sqrt(s2 / (2000 * 48));
        };
        const double open = rms(1.f, 0.f), mid = rms(0.45f, 0.f), low = rms(0.15f, 0.f);
        CHECK(low < 0.2 * open && mid < open, "low-pass: cutoff down removes the bubbles (%.1f / %.1f dB)", 20 * log10(mid / open), 20 * log10(low / open));
        Controls fc; fc.Midi(0xB0, 102, 0); Macros fm; fc.Compute(fm);
        CHECK(fm.cutoff == 0.f && fc.value[M_RES] == 0.f, "CC 102 = cutoff");
    }

    // --- Tone, Attack / Release, tuned release tails ---
    {
        auto meanF = [&](float tone) {
            eng.Init(SR, 1); Macros tm; tm.flow = 0.6f; tm.roughness = 0.5f; tm.scale = 0.5f; tm.aeration = 0.6f; tm.tone = tone;
            eng.SetMacros(tm); std::vector<float> a(48), b(48);
            for (int i = 0; i < 10000; i++) eng.Process(a.data(), b.data(), 48);
            Stats st; eng.GetStats(st); return st.meanF;
        };
        const float fd = meanF(0.f), fn = meanF(0.5f), fb = meanF(1.f);
        CHECK(fd < 0.75f * fn && fb > 1.3f * fn, "Tone: dark %.0f < default %.0f < bright %.0f Hz", fd, fn, fb);
        // pouring with Free flow 0: births in the first 100 ms after the note, and after release
        auto pour = [&](float att, float rel, uint32_t& onset, uint32_t& tail, int& tailTuned) {
            eng.Init(SR, 1); Macros pm; pm.flow = 0.6f; pm.roughness = 0.6f; pm.scale = 0.5f; pm.aeration = 0.7f;
            pm.freeFlow = 0.f; pm.attack = att; pm.release = rel; pm.variety = 0.f; eng.SetMacros(pm);
            std::vector<float> a(48), b(48);
            for (int i = 0; i < 500; i++) eng.Process(a.data(), b.data(), 48);           // 0.5 s settle
            pm.tune = true; pm.tuneHz[0] = 440.f; eng.SetMacros(pm);
            Stats s0; eng.GetStats(s0);
            for (int i = 0; i < 100; i++) eng.Process(a.data(), b.data(), 48);           // 100 ms
            Stats s1; eng.GetStats(s1); onset = s1.births - s0.births;
            for (int i = 0; i < 1900; i++) eng.Process(a.data(), b.data(), 48);          // hold ~2 s
            pm.tune = false; pm.tuneHz[0] = 0; eng.SetMacros(pm);
            Stats s2; eng.GetStats(s2); uint32_t last = eng.Pool().logW; tailTuned = 0;
            for (int i = 0; i < 1500; i++) {
                eng.Process(a.data(), b.data(), 48);
                for (; last != eng.Pool().logW; last++) if (fabsf(eng.Pool().logF[last & (kLog - 1)] - 440.f) < 1.f) tailTuned++;
            }
            Stats s3; eng.GetStats(s3); tail = s3.births - s2.births;
        };
        uint32_t onF, tailS, onS, tailL; int ttS, ttL;
        pour(0.004f, 0.02f, onF, tailS, ttS);
        pour(3.f, 4.f, onS, tailL, ttL);
        CHECK(onF > 2 * onS + 1, "Attack: a fast attack pours sooner (%u vs %u births in 100 ms)", onF, onS);
        CHECK(tailL > 3 * tailS + 3, "Release: a long release keeps pouring (%u vs %u births after release)", tailL, tailS);
        CHECK(tailL > 0 && ttL == (int)tailL, "release tail stays on the played note (%d of %u)", ttL, tailL);
        Controls ac; ac.Midi(0xB0, 107, 0); ac.Midi(0xB0, 72, 127); Macros am; ac.Compute(am);
        CHECK(fabsf(am.attack - 0.004f) < 1e-4f && fabsf(am.release - 4.f) < 1e-3f, "CC 107 / 72 = Attack / Release");
        Controls dc; dc.Compute(am);
        CHECK(fabsf(am.attack - ck::TUNE_ATTACK_SEC) < 1e-3f && fabsf(am.release - ck::TUNE_RELEASE_SEC) < 1e-3f, "Attack / Release default to the old pour feel");
    }

    // --- stress: finite, protected peaks, DC ---
    {
        auto st = load("stress");
        const auto t0 = std::chrono::steady_clock::now();
        render(eng, st, 48, L1, R1);
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        long bad = 0; float pk = 0;
        for (size_t i = 0; i < L1.size(); i++) { if (!std::isfinite(L1[i]) || !std::isfinite(R1[i])) bad++; pk = fmaxf(pk, fmaxf(fabsf(L1[i]), fabsf(R1[i]))); }
        CHECK(bad == 0, "stress: no NaN/Inf (%ld)", bad);
        CHECK(pk <= ck::LIMIT_CEIL + 1e-6f, "stress: protected peak %.2f dBFS <= -1 dBFS", 20 * log10f(pk));
        double dl = 0, dr = 0; const size_t a = (size_t)(40 * SR), b = (size_t)(50 * SR);
        for (size_t i = a; i < b; i++) { dl += L1[i]; dr += R1[i]; }
        dl /= (b - a); dr /= (b - a);
        CHECK(fabs(dl) < 1e-3 && fabs(dr) < 1e-3, "stress: DC mean over 10 s %.2g / %.2g < 0.001", dl, dr);
        Stats s; eng.GetStats(s);
        printf("stress: %.1f s rendered at %.0fx realtime on this computer; max voices %d, steals %u, drops %u, queue drops %u, limiter %u samples\n",
               st.seconds, st.seconds / el, s.maxActive, s.steals, s.drops, s.queueDrops, s.limited);
        writeF32("stress", L1, R1);
    }
    // --- reference renders for the JS comparison ---
    for (const char* k : {"trickle", "rocky", "cascade", "deeper", "single_bubble", "presets_walk", "notes", "notes_pour"}) {
        auto sc = load(k, strcmp(k, "presets_walk") && strcmp(k, "single_bubble") && strcmp(k, "notes") && strcmp(k, "notes_pour") ? 10.f : -1.f);
        render(eng, sc, 48, L1, R1);
        writeF32(k, L1, R1);
    }
    printf("engine static size: %zu bytes (%.1f KiB; brief: < 256 KiB)\n", sizeof(CreekEngine), sizeof(CreekEngine) / 1024.0);
    CHECK(sizeof(CreekEngine) < 256 * 1024, "engine working set < 256 KiB");
}

int main() {
    controlsTests();
    engineTests();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
