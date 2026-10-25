// Deterministic offline renderer (desktop host for the C++ engine).
//   creek_render scenarios/trickle.txt [-o out.wav] [--block N] [--seconds S] [--seed N] [--f32 raw.f32]
// Writes a 24-bit stereo WAV and, next to it, a .json with the seed, sample
// rate, block size, build version, the scenario (= all control automation),
// engine stats and a short signal analysis.
#include "scenario.h"
#include <chrono>

#ifndef CREEK_VERSION
#define CREEK_VERSION "dev"
#endif

static void writeWav24(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path.c_str()); exit(1); }
    const uint32_t n = (uint32_t)L.size(), data = n * 6;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + data); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32((uint32_t)sr); u32((uint32_t)sr * 6); u16(6); u16(24);
    fwrite("data", 1, 4, f); u32(data);
    std::vector<uint8_t> buf(data);
    for (uint32_t i = 0; i < n; i++) for (int c = 0; c < 2; c++) {
        const float v = (c ? R[i] : L[i]);
        long q = lroundf(fmaxf(-1.f, fminf(1.f, v)) * 8388607.f);
        uint8_t* p = &buf[(size_t)i * 6 + c * 3];
        p[0] = (uint8_t)(q & 0xFF); p[1] = (uint8_t)((q >> 8) & 0xFF); p[2] = (uint8_t)((q >> 16) & 0xFF);
    }
    fwrite(buf.data(), 1, buf.size(), f);
    fclose(f);
}

static std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c == '\n') o += "\\n"; else if ((unsigned char)c >= 0x20) o += c; }
    return o + "\"";
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: creek_render scenario.txt [-o out.wav] [--block N] [--seconds S] [--seed N] [--f32 raw.f32]\n"); return 2; }
    std::string scen = argv[1], out, f32;
    int block = 48; float secs = -1; long seed = -1;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a == "--block" && i + 1 < argc) block = atoi(argv[++i]);
        else if (a == "--seconds" && i + 1 < argc) secs = (float)atof(argv[++i]);
        else if (a == "--seed" && i + 1 < argc) seed = atol(argv[++i]);
        else if (a == "--f32" && i + 1 < argc) f32 = argv[++i];
    }
    const float sr = 48000.f;
    scenario::Scenario sc;
    try { sc = scenario::parse(scenario::readFile(scen), sr); } catch (std::exception& e) { fprintf(stderr, "%s: %s\n", scen.c_str(), e.what()); return 1; }
    if (secs > 0) sc.seconds = secs;
    if (seed >= 0) sc.seed = (uint32_t)seed;
    if (out.empty()) {
        std::string base = scen.substr(scen.find_last_of('/') == std::string::npos ? 0 : scen.find_last_of('/') + 1);
        out = base.substr(0, base.find_last_of('.')) + ".wav";
    }

    static creek::CreekEngine eng;
    eng.Init(sr, sc.seed);
    std::vector<float> L, R;
    double worst = 0; long lim = 0;
    const auto t0 = std::chrono::steady_clock::now();
    auto tb = std::chrono::steady_clock::now();
    scenario::run(sc, eng, sr, block, L, R, [&](long, long) {
        const auto t = std::chrono::steady_clock::now();
        worst = std::max(worst, std::chrono::duration<double>(t - tb).count()); tb = t; (void)lim;
    });
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // analysis (after the first second)
    const size_t n = L.size(), from = std::min(n, (size_t)sr);
    double peak = 0, sl = 0, srr = 0, sm = 0, ml = 0, mr = 0; long nan = 0;
    for (size_t i = from; i < n; i++) {
        const float a = L[i], b = R[i];
        if (!std::isfinite(a) || !std::isfinite(b)) { nan++; continue; }
        peak = std::max(peak, (double)std::max(fabsf(a), fabsf(b)));
        sl += a * a; srr += b * b; sm += 0.25 * (a + b) * (a + b); ml += a; mr += b;
    }
    const double cnt = (double)std::max<size_t>(1, n - from), rms = sqrt((sl + srr) / (2 * cnt));
    creek::Stats st; eng.GetStats(st);

    writeWav24(out, L, R, (int)sr);
    if (!f32.empty()) {
        FILE* f = fopen(f32.c_str(), "wb");
        for (size_t i = 0; i < n; i++) { fwrite(&L[i], 4, 1, f); fwrite(&R[i], 4, 1, f); }
        fclose(f);
    }
    const std::string js = out.substr(0, out.find_last_of('.')) + ".json";
    FILE* j = fopen(js.c_str(), "w");
    fprintf(j, "{\n \"engine\": \"c++\", \"version\": %s, \"scenario\": %s,\n \"seed\": %u, \"sampleRate\": %d, \"block\": %d, \"seconds\": %g,\n",
            jsonStr(CREEK_VERSION).c_str(), jsonStr(scen).c_str(), sc.seed, (int)sr, block, sc.seconds);
    fprintf(j, " \"automation\": %s,\n", jsonStr(sc.text).c_str());
    fprintf(j, " \"stats\": {\"births\": %u, \"drops\": %u, \"steals\": %u, \"maxVoices\": %d, \"queueDrops\": %u, \"limiterSamples\": %u, \"meanBubbleHz\": %.1f},\n",
            st.births, st.drops, st.steals, st.maxActive, st.queueDrops, st.limited, st.meanF);
    fprintf(j, " \"analysis\": {\"peakDb\": %.2f, \"rmsDb\": %.2f, \"monoToStereoRms\": %.3f, \"dcL\": %.3g, \"dcR\": %.3g, \"nonFinite\": %ld},\n",
            20 * log10(std::max(peak, 1e-9)), 20 * log10(std::max(rms, 1e-9)), sqrt(sm / cnt) / (rms + 1e-12), ml / cnt, mr / cnt, nan);
    fprintf(j, " \"renderSecondsPerSecond\": %.5f\n}\n", el / sc.seconds);
    fclose(j);
    printf("%-26s peak %6.1f dB  rms %6.1f dB  births %5.1f/s  meanF %5.0f Hz  maxV %2d  steals %5u  drops %4u  lim %u  %s%s\n",
           out.c_str(), 20 * log10(std::max(peak, 1e-9)), 20 * log10(std::max(rms, 1e-9)), st.births / sc.seconds, st.meanF,
           st.maxActive, st.steals, st.drops, st.limited, nan ? "NONFINITE " : "", "");
    (void)worst;
    return nan ? 1 : 0;
}
