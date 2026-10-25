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
    CHECK(fabsf(KnobToGain(0.5f) - 1.f) < 1e-5f, "gain noon = 0 dB");
    CHECK(KnobToGain(0.f) == 0.f, "gain bottom = off");
    CHECK(fabsf(KnobToWidth(0.5f) - 1.f) < 1e-6f, "width noon = 1");
    c.Tick(0.0);
    c.PadDown(kPadPreset0 + 2);  // Small cascade
    c.Tick(0.1);
    CHECK(c.value[M_FLOW] > 0.2f && c.value[M_FLOW] < 0.75f, "preset glide in progress (%f)", c.value[M_FLOW]);
    c.Tick(0.3);
    CHECK(fabsf(c.value[M_FLOW] - 0.75f) < 1e-6f && fabsf(c.value[M_AER] - 0.85f) < 1e-6f, "preset reached after 250 ms");
    CHECK(!c.Gliding(), "glide finished");
    c.SetMode(true);
    c.PadDown(kPadLayer0 + 1);   // mute bubbles
    Macros m; c.Compute(m);
    CHECK(m.mask == (0x1Fu & ~2u), "diag: P04 mutes bubbles (mask %x)", m.mask);
    c.PadDown(kPadAll); c.Compute(m);
    CHECK(m.mask == 0x1Fu, "diag: P08 restores all");
    c.PadDown(kPadLayer0); c.SetMode(false); c.Compute(m);
    CHECK(m.mask == 0x1Fu, "performance mode always plays all layers");
    c.SetMode(true); c.Compute(m);
    CHECK(m.mask == 0x1Eu, "diag mask remembered");
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
    for (const char* k : {"trickle", "rocky", "cascade", "deeper", "single_bubble", "presets_walk"}) {
        auto sc = load(k, strcmp(k, "presets_walk") && strcmp(k, "single_bubble") ? 10.f : -1.f);
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
