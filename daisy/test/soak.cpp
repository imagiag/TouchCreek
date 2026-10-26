// 30-minute stability run (brief, "Stability"): maximum activity, corner
// presets, rapid control changes and the hardware stress test, driven through
// the same Controls code as the firmware. Checks for NaN/Inf, clicks, stuck
// voices, limiter pumping, peaks, DC and mono compatibility.
//   make -C daisy/test soak        (MINUTES=30 by default)
#include "../creek/creek_engine.h"
#include "../creek/controls.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <chrono>

using namespace creek;

static int g_fail = 0;
#define CHECK(cond, ...) do { printf("%s ", (cond) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(cond)) g_fail++; } while (0)

int main(int argc, char** argv) {
    const double minutes = argc > 1 ? atof(argv[1]) : 30.0;
    const float sr = 48000.f;
    const int B = 48;
    static CreekEngine eng;
    static float dl[2][120000];
    eng.SetDelayBuffers(dl[0], dl[1], CreekEngine::DelayLen(sr));
    eng.Init(sr, 11);
    Controls ctl;
    float L[B], R[B];

    const long blocks = (long)(minutes * 60.0 * sr / B);
    long nonFinite = 0, clicks = 0, limitedAtUnity = 0, unityBlocks = 0;
    float peak = 0;
    double dcL = 0, dcR = 0, worstDc = 0, sumS = 0, sumM = 0;
    long dcN = 0;
    float prevL = 0, prevR = 0;
    int worstStuck = 0;
    double worstBlockUs = 0;
    uint32_t lastLim = 0;
    const auto t0 = std::chrono::steady_clock::now();

    // Phases, each 60 s, repeating: 0 stress test, 1 all maximum, 2 preset corners every 2 s,
    // 3 random knob + delay jumps every 20 ms, 4 notes: chords + drops with delay (1: full delay at maximum), 5 Flow 0 for 5 s (stuck-voice check) then max
    for (long b = 0; b < blocks; b++) {
        const double t = (double)b * B / sr;
        const int phase = (int)(t / 60.0) % 6;
        const double pt = fmod(t, 60.0);
        ctl.Tick(t);
        if (pt < 1e-9 || b == 0) {                       // phase start
            ctl.SetMode(phase == 0 ? MODE_DIAG : (phase == 4 ? MODE_NOTES : MODE_PRESETS));
            if (phase == 0 && !ctl.stress) ctl.PadDown(kPadStress, 0.7f), ctl.PadUp(kPadStress);
            ctl.SetKnob(M_LEVEL, 0.7071f); ctl.SetKnob(M_WIDTH, 0.5f);
            ctl.SetKnob(M_DAMT, phase == 1 ? 1.f : phase == 4 ? 0.7f : 0.f);   // delay: full at maximum, on for notes, else off
            ctl.SetKnob(M_DTIME, phase == 1 ? 1.f : 0.5f);
        }
        if (phase == 1) for (int k = 0; k < 4; k++) ctl.SetKnob(k, 1.f);
        if (phase == 2 && fmod(pt, 2.0) < (double)B / sr) { const int p = (int)(pt / 2.0) % 4; ctl.PadDown(kPadPreset0 + p, .7f); ctl.PadUp(kPadPreset0 + p); }
        if ((phase == 2 || phase == 4) && pt < 54.0 && b % 1500 == 0) { ctl.PadDown(kPadStone, (float)rand() / (float)RAND_MAX); ctl.PadUp(kPadStone); }   // stones
        if (phase == 1 && b % 3000 == 0) { ctl.PadDown(kPadStone, 1.f); ctl.PadUp(kPadStone); }                                  // a full stone every 3 s at maximum
        if (phase == 3 && b % 20 == 0) { for (int k = 0; k < 4; k++) ctl.SetKnob(k, (float)rand() / (float)RAND_MAX);
            if (pt < 50.0) { ctl.SetKnob(M_DTIME, (float)rand() / (float)RAND_MAX); ctl.SetKnob(M_DAMT, (float)rand() / (float)RAND_MAX); }   // delay jumps (glides)
            else ctl.SetKnob(M_DAMT, 0.f); }
        if (phase == 4) {
            for (int k = 0; k < 4; k++) ctl.SetKnob(k, k == M_SCALE ? 0.5f : 0.8f);
            if (b % 100 == 0) { const int p = 3 + rand() % 7; ctl.PadDown(p, (float)rand() / (float)RAND_MAX); if (rand() % 3) ctl.PadUp(p); }
            if (b % 997 == 0) for (int p = 3; p < 10; p++) ctl.PadUp(p);
        }
        if (phase == 5) { for (int k = 1; k < 4; k++) ctl.SetKnob(k, 1.f); ctl.SetKnob(M_FLOW, pt < 5.0 ? 0.f : 1.f); }

        Macros m; ctl.Compute(m);
        eng.SetMacros(m);
        float sv;
        while (ctl.PopStone(sv)) { Event e; e.type = EV_STONE; e.amp = 0.3f + 0.7f * sv; eng.Trigger(e); }
        Drop d;
        while (ctl.PopDrop(d)) { Event e; e.type = EV_DROP; e.radiusMm = 3000.f / d.hz; e.amp = 0.25f + 0.75f * d.vel; eng.Trigger(e); }

        const auto tb = std::chrono::steady_clock::now();
        eng.Process(L, R, B);
        worstBlockUs = fmax(worstBlockUs, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - tb).count());

        for (int i = 0; i < B; i++) {
            if (!std::isfinite(L[i]) || !std::isfinite(R[i])) { nonFinite++; L[i] = R[i] = 0; }
            peak = fmaxf(peak, fmaxf(fabsf(L[i]), fabsf(R[i])));
            // click = a jump far beyond what the band-limited content can do in one sample
            if (fabsf(L[i] - prevL) > 0.6f || fabsf(R[i] - prevR) > 0.6f) clicks++;
            prevL = L[i]; prevR = R[i];
            dcL += L[i]; dcR += R[i]; dcN++;
            const float mid = 0.5f * (L[i] + R[i]);
            sumM += mid * mid; sumS += 0.5 * (L[i] * L[i] + R[i] * R[i]);
        }
        if (dcN >= (long)(10 * sr)) { worstDc = fmax(worstDc, fmax(fabs(dcL / dcN), fabs(dcR / dcN))); dcL = dcR = 0; dcN = 0; }

        Stats s; eng.GetStats(s);
        // limiter pumping at unity gain with everything at maximum (phase 1)
        if (phase == 1) { unityBlocks++; if (s.limited != lastLim) limitedAtUnity++; }
        lastLim = s.limited;
        // stuck voices: 3.5 s after Flow 0 nothing may still sound
        if (phase == 5 && pt > 3.5 && pt < 5.0) worstStuck = s.active > worstStuck ? s.active : worstStuck;
        if (phase == 5 && pt > 3.5 && pt < 5.0) for (int i = 0; i < B; i++) if (fabsf(L[i]) > 1e-4f) { worstStuck = worstStuck > 0 ? worstStuck : 1; }
    }
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    Stats s; eng.GetStats(s);
    printf("soak: %.0f min rendered in %.1f s (%.0fx realtime); worst block %.1f us on this computer\n", minutes, el, minutes * 60 / el, worstBlockUs);
    printf("      %u bubbles, %u steals, %u drops, %u queue drops, %u splashes, %u surface ticks, peak voices %d\n",
           s.births, s.steals, s.drops, s.queueDrops, s.splashes, s.surface, s.maxActive);
    CHECK(nonFinite == 0, "no NaN/Inf (%ld)", nonFinite);
    CHECK(peak <= ck::LIMIT_CEIL + 1e-6f, "protected peak %.2f dBFS <= -1 dBFS", 20 * log10f(peak));
    CHECK(clicks == 0, "no sample jumps > 0.6 (clicks: %ld)", clicks);
    CHECK(worstStuck == 0, "no stuck voices / sound 3.5 s after Flow 0");
    CHECK(unityBlocks > 0 && limitedAtUnity < 0.01 * unityBlocks, "no sustained limiting at maximum settings, 0 dB level (%.2f %% of blocks)",
          100.0 * limitedAtUnity / (unityBlocks ? unityBlocks : 1));
    CHECK(worstDc < 0.001, "DC mean per 10 s < 0.001 (worst %.2g)", worstDc);
    CHECK(sqrt(sumM / sumS) > 0.8, "mono sum keeps the body (mid/stereo rms %.2f)", sqrt(sumM / sumS));
    CHECK(s.queueDrops == 0, "no queue drops");
    return g_fail ? 1 : 0;
}
